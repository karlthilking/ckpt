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
#include <vector>
#include <string>

using namespace xnd;

static void create_child_process(xnd_restart_target *);
static void create_orphan_process(xnd_restart_target *, bool);
static void create_process(xnd_restart_target *, bool);
[[noreturn]] static void process_restart_targets(void);

static std::vector<xnd_restart_target *> targets;
static std::vector<xnd_restart_target *> roots;

static xnd_restart_info *info   = nullptr;
static xnd_restart_dag  *dag    = nullptr;

extern char **environ;

[[noreturn]] void xnd_restart_target::exec_restart(void) const noexcept
{
        int                     err;
        short                   flags;
        pid_t                   pid;
        posix_spawnattr_t       attr;
        char                    *argv[3];

        posix_spawnattr_init(&attr);
        xnd_assert(setenv("DYLD_SHARED_REGION", "private", 1) == 0);

        flags = POSIX_SPAWN_SETEXEC | POSIX_SPAWN_DISABLE_ASLR;
        if ((err = posix_spawnattr_setflags(&attr, flags)) != 0) {
                xnd_error("posix_spawnattr_setflags: %s\n", strerror(err));
                exit(XND_EXIT_FAILURE);
        }
        
        argv[0] = info->restart;
        argv[1] = const_cast<char *>(ckptpath);
        argv[2] = nullptr;

        err = posix_spawn(&pid, info->restart, nullptr, &attr, argv, environ);
        if (err != 0) {
                posix_spawnattr_destroy(&attr);
                xnd_error("posix_spawn: %s\n", strerror(err));
                exit(XND_EXIT_FAILURE);
        }

        unreachable();
}

static void create_child_process(xnd_restart_target *self)
{
        switch (fork()) {
        case -1:
                xnd_error("fork: %s\n", strerror(errno));
                exit(XND_EXIT_FAILURE);
        case 0:
                create_process(self, false);
                unreachable();
        default:
                return;
        }
}

static void create_orphan_process(xnd_restart_target *self, bool create_roots)
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
                        create_process(self, create_roots);
                        unreachable();
                default:
                        exit(0);
                }
        default:
                xnd_assert(waitpid(child, NULL, 0) == child);
                return;
        }
}

static void create_process(xnd_restart_target *self, bool create_roots)
{
        if (dag->has_children(self)) {
                for (auto child : dag->children_of(self)) {
                        xnd_assert(child != self);
                        if (child->sid() != self->pid()) {
                                create_child_process(child);
                        }
                }
        }

        if (create_roots) {
                for (auto root : roots) {
                        if (root == self) {
                                continue;
                        }
                        create_orphan_process(root, false);
                }
        }

        if (self->is_session_leader()) {
                if (getsid(0) != self->pid()) {
                        setsid();
                }
        }

        for (auto t : targets) {
                if (t == self) {
                        continue;
                } else if (t->sid() == self->pid()) {
                        if (self->is_parent_of(t)) {
                                create_child_process(t);
                        } else if (t->is_root_of_tree()) {
                                create_orphan_process(t, false);
                        }
                }
        }

        self->exec_restart(); 
}

[[noreturn]] static void process_restart_targets(void)
{
        targets.reserve(info->manifest.entry_count);
        for (u32 idx = 0; idx < info->manifest.entry_count; idx++) {
                auto xnd_pid = info->manifest.xnd_pids[idx];
                auto t = new xnd_restart_target(info->ckptdir, xnd_pid);
                targets.push_back(t);
        }

        dag = new xnd_restart_dag(targets);
        for (auto t : targets) {
                if (dag->indegree_of(t) == 0) {
                        roots.push_back(t);
                }
        }

        xnd_assert(roots.size() != 0);
        if (roots.size() == 1) {
                create_process(roots[0], true);
        } else {
                create_orphan_process(roots[0], true);
        }

        unreachable();
}

int main(int argc, char *argv[])
{
        pid_t coord_pid;
        
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
        process_restart_targets();
        unreachable();
}
