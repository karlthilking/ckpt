/* xnd_restart.cpp */
#include "xnd/xnd.h"
#include "xnd/xnd_restart.h"
#include "xnd/ckptfile.h"
#include "xnd/util/path.h"
#include "xnd/platform/exe.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <spawn.h>
#include <errno.h>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <string>

using namespace xnd;

extern char **environ;

static std::vector<xnd_restart_target *> targets;
static std::vector<xnd_restart_target *> independent_roots;

static xnd_restart_info *info   = nullptr;
static xnd_restart_dag  *dag    = nullptr;

[[noreturn]] void xnd_restart_target::exec_restart(void) const noexcept
{
        int                     err;
        short                   flags;
        pid_t                   pid;
        posix_spawnattr_t       attr;

        posix_spawnattr_init(&attr);
        xnd_assert(setenv("DYLD_SHARED_REGION", "private", 1) == 0);

        flags = POSIX_SPAWN_SETEXEC | POSIX_SPAWN_DISABLE_ASLR;
        if ((err = posix_spawnattr_setflags(&attr, flags)) != 0) {
                xnd_error("posix_spawnattr_setflags: %s\n", strerror(err));
                exit(XND_EXIT_FAILURE);
        }

        char *argv[] = { info->restart, this->path_to_ckpt(), nullptr };
        err = posix_spawn(&pid, info->restart, nullptr, &attr, argv, environ);
        if (err != 0) {
                posix_spawnattr_destroy(&attr);
                xnd_error("posix_spawn: %s\n", strerror(err));
                exit(XND_EXIT_FAILURE);
        }

        unreachable();
}

void xnd_restart_target::create_child(void) const noexcept
{
        switch (fork()) {
        case -1:
                xnd_error("fork: %s\n", strerror(errno));
                exit(XND_EXIT_FAILURE);
        case 0:
                this->create_process(false);
                unreachable();
        default:
                return;
        }
}

void xnd_restart_target::create_orphan(bool create_roots) const noexcept
{
        pid_t child;

        child = fork();
        switch (child) {
        case -1:
                xnd_error("fork: %s\n", strerror(errno));
                exit(XND_EXIT_FAILURE);
        case 0:
                switch (fork()) {
                case -1:
                        xnd_error("fork: %s\n", strerror(errno));
                        exit(XND_EXIT_FAILURE);
                case 0:
                        this->create_process(create_roots);
                        unreachable();
                default:
                        exit(0);
                }
        default:
                xnd_assert(waitpid(child, nullptr, 0) == child);
                return;
        }
}

void xnd_restart_target::create_process(bool create_roots) const noexcept
{
        auto self = const_cast<xnd_restart_target *>(this);
        auto has_children = dag->has_children(self);

        if (has_children) {
                for (auto child : dag->children_of(self)) {
                        xnd_assert(child != self);
                        if (self->pid() != child->sid()) {
                                child->create_child();
                        }
                }
        }

        if (create_roots) {
                for (auto ir : independent_roots) {
                        if (ir != self) {
                                ir->create_orphan(false);
                        }
                }
        }

        if (self->is_session_leader()) {
                if (getpid() != getsid(0)) {
                        xnd_assert(getpgrp() != getpid());
                        setsid();
                }
        }

        if (has_children) {
                for (auto child : dag->children_of(self)) {
                        if (self->pid() == child->sid()) {
                                child->create_child();
                        }
                }
        }

        self->exec_restart();
}

[[noreturn]] void xnd_restart_info::process_targets(void) const noexcept
{
        targets.reserve(this->manifest.entry_count);
        for (u32 idx = 0; idx < this->manifest.entry_count; idx++) {
                auto xnd_pid = this->manifest.xnd_pids[idx];
                auto t = new xnd_restart_target(this->ckptdir, xnd_pid);
                targets.push_back(t);
        }

        dag = new xnd_restart_dag(targets);
        std::ranges::for_each(targets, [&](auto t) {
                if (dag->indegree_of(t)) {
                        return;
                }
                auto it = std::ranges::find_if(targets, [&](auto s) {
                        return s != t && s->is_session_leader_of(t);
                });
                if (it == targets.end()) {
                        independent_roots.push_back(t);
                }
        });
        
        xnd_assert(independent_roots.size() != 0);
        auto it = std::ranges::find_if(independent_roots, [&](auto ir) {
                return ir->is_non_orphan();
        });
        if (it != independent_roots.end()) {
                static_cast<xnd_restart_target *>(*it)->create_process(true);
        } else {
                independent_roots[0]->create_orphan(true);
        }

        unreachable();
}

int main(int argc, char *argv[])
{
        pid_t coord_pid, child;
        
        xnd_log_setup();
        if (argc != 2) {
                xnd_error("Usage: ./xnd_restart <ckpt-dir>\n");
                xnd_log_cleanup();
                exit(XND_EXIT_SUCCESS);
        }

        if ((coord_pid = launch_coordinator()) == -1) {
                xnd_error("launch_coordinator() failed\n");
                xnd_log_cleanup();
                exit(XND_EXIT_SUCCESS);
        } else {
                char buf[11];
                snprintf(buf, sizeof(buf), "%d", coord_pid);
                xnd_assert(setenv("XND_COORD", buf, 1) == 0);
        }
        
        info = new xnd_restart_info(argv[1]);
        child = fork();
        switch (child) {
        case -1:
                xnd_error("fork: %s\n", strerror(errno));
                exit(XND_EXIT_FAILURE);
        case 0:
                info->process_targets();
                unreachable();
        default:
                break;
        }

        xnd_assert(waitpid(child, nullptr, 0) == child);
        delete info;
        for (auto t : targets) {
                delete t;
        }
        
        xnd_log_cleanup();
        exit(XND_EXIT_SUCCESS);
}
