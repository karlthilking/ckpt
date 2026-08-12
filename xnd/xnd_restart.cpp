/* xnd_restart.cpp */
#include "xnd_restart.h"
#include "ckptfile.h"
#include "util/env.h"
#include "util/path.h"
#include "platform/exe.h"
#include "coordinator/xnd_coord_api.h"

#include <mach/mach.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
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

static bool restart_pause = false;
static xnd_restart_dag *dag = nullptr;
static xnd_restart_info *info = nullptr;

static void restore_parent_cleanup(void)
{
        delete dag;
        delete info;
        for (auto t : targets)
                delete t;

        xnd_log_cleanup();
}

static void restore_parent_handler(int sig)
{
        if (killpg(getpgrp(), sig) != 0 && errno != ESRCH)
                xnd_perror("killpg");

        restore_parent_cleanup();
        kill(getpid(), sig);
}

static int restore_parent_setup(void)
{
        struct sigaction sa;

        sigfillset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;
        sa.sa_handler = restore_parent_handler;

        if (sigaction(SIGINT, &sa, NULL) != 0) {
                xnd_perror("sigaction");
                return -1;
        }
        
        if (sigaction(SIGTERM, &sa, NULL) != 0) {
                xnd_perror("sigaction");
                return -1;
        }
        
        if (sigaction(SIGQUIT, &sa, NULL) != 0) {
                xnd_perror("sigaction");
                return -1;
        }

        if (launch_coordinator(true) == -1) {
                xnd_error("launch_coordinator() failed\n");
                return -1;
        }

        xnd_log_mach_port_info();
        env_set_dyld_shared_region_private();
        return 0;
}

[[noreturn]] void xnd_restart_target::exec_restart(void) const noexcept
{
	int err;
	short flags;
	posix_spawnattr_t attr;
	const char* argv[4] = { info->restart, this->path_to_ckpt() };

	argv[2] = (restart_pause ? "--pause" : nullptr);
	argv[3] = nullptr;
        xnd_log_mach_port_info();

	posix_spawnattr_init(&attr);
	flags = POSIX_SPAWN_SETEXEC | POSIX_SPAWN_DISABLE_ASLR;

	err = posix_spawnattr_setflags(&attr, flags);
	if (err) {
		xnd_error("posix_spawnattr_setflags: %s\n", strerror(err));
		exit(XND_EXIT_FAILURE);
	}

	err = posix_spawn(nullptr, argv[0], nullptr, &attr,
			  (char *const *)argv, environ);
	if (err) {
		xnd_error("posix_spawn: %s\n", strerror(err));
		posix_spawnattr_destroy(&attr);
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

        if (create_roots) {
                for (auto ir : independent_roots) {
                        if (ir != self) {
                                ir->create_orphan(false);
                        }
                }
        }

        for (auto child : dag->children_of(self)) {
                xnd_assert(child != self);
                if (self->was_session_leader_of(child)) {
                        continue;
                } else if (self->was_group_leader_of(child)) {
                        continue;
                }
                child->create_child();
        }

        if (self->was_session_leader()) {
                if (getpid() != getsid(0)) {
                        xnd_assert(getpid() != getpgrp());
                        if (setsid() == -1) {
                                xnd_error("setsid: %s\n", strerror(errno));
                        }
                }
        } else if (self->was_group_leader()) {
                if (getpid() != getpgrp()) {
                        if (setpgid(0, 0) == -1) {
                                xnd_error("setpgid: %s\n", strerror(errno));
                        }
                }
        }

        for (auto t : targets) {
                if (t == self) {
                        continue;
                } else if (self->was_session_leader_of(t)) {
                        if (self->was_parent_of(t)) {
                                t->create_child();
                        } else if (t->was_root_of_tree()) {
                                t->create_orphan(false);
                        }
                } else if (self->was_group_leader_of(t)) {
                        if (self->was_parent_of(t)) {
                                t->create_child();
                        } else if (t->was_root_of_tree()) {
                                t->create_orphan(false);
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
                        xnd_assert(t->was_root_of_tree() == false);
                        return;
                }
                xnd_assert(t->was_root_of_tree());
                auto it = std::ranges::find_if(targets, [&](auto s) {
                        return s != t && s->was_session_leader_of(t);
                });
                if (it == targets.end()) {
                        independent_roots.push_back(t);
                }
        });

        xnd_assert(independent_roots.size() != 0);
        auto it = std::ranges::find_if(independent_roots, [&](auto ir) {
                return ir->was_not_orphan();
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
	int stat, code;
	pid_t ret, child;
	const char *ckptdir = nullptr;

        xnd_log_setup();
	for (char **p = argv + 1; *p != NULL; p++) {
		if (strncmp(*p, "--pause", sizeof("--pause")) == 0) {
			restart_pause = true;
		} else if (access(*p, F_OK) == 0) {
			ckptdir = *p;
		} else {
			xnd_error("Invalid argument: %s\n", *p);
			exit(XND_EXIT_FAILURE);
		}
	}

	restore_parent_setup();
	info = new xnd_restart_info(ckptdir);

	child = fork();
        switch (child) {
        case -1:
                xnd_error("fork: %s\n", strerror(errno));
                goto fail;
        case 0:
                info->process_targets();
                unreachable();
        default:
                break;
        }

        do {
                ret = waitpid(child, &stat, 0);
                if (ret == -1 && errno != EINTR) {
                        xnd_perror("waitpid");
                        goto fail;
                } else if (WIFEXITED(stat)) {
                        if ((code = WEXITSTATUS(stat)) != 0)
                                xnd_warn("%d exited: %d\n", child, code);
                        break;
                } else {
                        xnd_assert(WIFSIGNALED(stat));
                        code = 128 + WTERMSIG(stat);
                        xnd_warn("%d signaled: %d\n", child, code - 128);
                        break;
                }
        } while (true);

        restore_parent_cleanup();
        exit(code);

fail:
        restore_parent_cleanup();
        exit(XND_EXIT_FAILURE);
}
