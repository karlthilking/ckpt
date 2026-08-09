/* xnd_restart.h */
#ifndef XND_RESTART_INTERNAL
#define XND_RESTART_INTERNAL

#include "xnd.h"
#include "ckptfile.h"
#include "util/path.h"
#include "util/io.h"
#include "util/compress.h"
#include "platform/exe.h"
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <vector>
#include <unordered_map>

#define XND_RESTART_BINARY "xnd_restart_internal"

#ifndef POSIX_SPAWN_DISABLE_ASLR
# define POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

namespace xnd {
struct xnd_restart_info {
        struct xnd_manifest     manifest;
        char                    ckptdir[XND_CKPTDIR_MAXLEN];
        char                    restart[PATH_MAX];

        xnd_restart_info(const char *dir) noexcept
        {
                int     err;
                char    path[XND_MANIFEST_MAXLEN];

                strncpy(ckptdir, dir, XND_CKPTDIR_MAXLEN);

		err = xnd_exe_path_of(restart, sizeof(restart),
				      XND_RESTART_BINARY);
                if (err != 0) {
                        xnd_error("xnd_exe_path_of failed\n");
                        exit(XND_EXIT_FAILURE);
                }
                
                err = xnd_path_join(path, sizeof(path),
                                    ckptdir, XND_MANIFEST_NAME);
                if (err != 0) {
                        xnd_error("xnd_path_join failed: %s/%s\n",
                                  ckptdir, XND_MANIFEST_NAME);
                        exit(XND_EXIT_FAILURE);
                }
                
                err = xnd_ckptfile_extract_manifest(path, &manifest);
                if (err != 0) {
                        xnd_error("xnd_ckptfile_extract_manifest failed\n");
                        exit(XND_EXIT_FAILURE);
                }
        }

        ~xnd_restart_info(void) = default;

        [[noreturn]] void process_targets(void) const noexcept;
};

class xnd_restart_target {
private:
        struct xnd_ckpt_header  header;
        char                    ckptpath[XND_CKPTPATH_MAXLEN];
        u32                     xnd_pid_;

public:
        xnd_restart_target(void) = default;

        xnd_restart_target(const char *dir, u32 id) noexcept
                : xnd_pid_(id)
        {
                int     err, fd, dirfd;
                ssize_t bytes;
                char    ckptfile[XND_CKPTFILE_MAXLEN];

                bzero(ckptpath, sizeof(ckptpath));
                sprintf(ckptpath, "%s/ckpt-%u.xnd", dir, id);
                snprintf(ckptfile, sizeof(ckptfile), "ckpt-%u.xnd", id);

                dirfd = open(dir, O_DIRECTORY | O_RDONLY);
                if (faccessat(dirfd, ckptfile, F_OK, 0) == 0) {
                        if ((fd = openat(dirfd, ckptfile, O_RDONLY)) < 0) {
                                xnd_error("openat: %s\n", strerror(errno));
                                exit(XND_EXIT_FAILURE);
                        }
                } else {
                        err = xnd_decompress_ckpt(dirfd, ckptfile);
                        if (err != 0) {
                                exit(XND_EXIT_FAILURE);
                        }
                        xnd_assert(faccessat(dirfd, ckptfile, F_OK, 0) == 0);
                        if ((fd = openat(dirfd, ckptfile, O_RDONLY)) < 0) {
                                xnd_error("openat: %s\n", strerror(errno));
                                exit(XND_EXIT_FAILURE);
                        }
                }
                
                bytes = readall(fd, &header, sizeof(header));
                if (bytes != sizeof(header)) {
                        exit(XND_EXIT_FAILURE);
                }

                xnd_assert(xnd_pid_ == header.xnd_pid);
                close(fd);
                close(dirfd);
        }

        [[noreturn]] void exec_restart(void) const noexcept;
        void create_child(void) const noexcept;
        void create_orphan(bool) const noexcept;
        void create_process(bool) const noexcept;

        auto path_to_ckpt(void) const noexcept -> char *
        {
                return const_cast<char *>(ckptpath);
        }
        
        auto pid(void) const noexcept -> pid_t
        {
                return header.pid;
        }

        auto ppid(void) const noexcept -> pid_t
        {
                return header.ppid;
        }

        auto sid(void) const noexcept -> pid_t
        {
                return header.sid;
        }

        auto pgid(void) const noexcept -> pid_t
        {
                return header.pgid;
        }

        auto xnd_pid(void) const noexcept -> u32
        {
                return xnd_pid_;
        }

        auto xnd_ppid(void) const noexcept -> u32
        {
                return header.xnd_ppid;
        }

        auto xnd_pgid(void) const noexcept -> u32
        {
                return header.xnd_pgid;
        }

        auto was_orphan(void) const noexcept -> bool
        {
                return ppid() == 1;
        }

        auto was_not_orphan(void) const noexcept -> bool
        {
                return !(was_orphan());
        }

        auto was_root_of_tree(void) const noexcept -> bool
        {
                return header.is_root_of_tree;
        }

        auto was_group_leader(void) const noexcept -> bool
        {
                return pid() == pgid();
        }

        auto was_group_leader_of(xnd_restart_target *other)
        const noexcept -> bool
        {
                return pid() == other->pgid();
        }

        auto was_group_leader_of(const xnd_restart_target &other)
        const noexcept -> bool
        {
                return pid() == other.pgid();
        }

        auto was_session_leader(void) const noexcept -> bool
        {
                return pid() == sid();
        }

        auto was_session_leader_of(xnd_restart_target *other)
        const noexcept -> bool
        {
                return pid() == other->sid();
        }

        auto was_session_leader_of(const xnd_restart_target &other)
        const noexcept -> bool
        {
                return pid() == other.sid();
        }

        auto was_child_of(xnd_restart_target *other) 
        const noexcept -> bool
        {
                return xnd_ppid() == other->xnd_pid();
        }

        auto was_child_of(const xnd_restart_target &other) 
        const noexcept -> bool
        {
                return xnd_ppid() == other.xnd_pid();
        }

        auto was_parent_of(xnd_restart_target *other) 
        const noexcept -> bool
        {
                return xnd_pid() == other->xnd_ppid();
        }

        auto was_parent_of(const xnd_restart_target &other) 
        const noexcept -> bool
        {
                return xnd_pid() == other.xnd_ppid();
        }
};

class xnd_restart_dag {
private:
        std::unordered_map<xnd_restart_target *, uint> _indegree;
        std::unordered_map<xnd_restart_target *, 
                           std::vector<xnd_restart_target *>> _map;
public:
        xnd_restart_dag(std::vector<xnd_restart_target *> &targets) noexcept
        {
                for (auto t : targets) {
                        _indegree[t] = 0;
                        for (auto c : targets) {
                                if (t == c) {
                                        continue;
                                } else if (c->was_child_of(t)) {
                                        _map[t].push_back(c);
                                }
                        }
                }

                for (auto &[t, children] : _map) {
                        for (auto c : children) {
                                _indegree[c]++;
                        }
                }
        }
        
        auto indegree_of(xnd_restart_target *t) 
        const noexcept -> unsigned int
        {
                if (auto it = _indegree.find(t); it != _indegree.end()) {
                        return it->second;
                }

                return 0u;
        }

        auto has_children(xnd_restart_target *t) const noexcept -> bool
        {
                if (auto it = _map.find(t); it != _map.end()) {
                        if (it->second.empty()) {
                                return false;
                        }
                        return true;
                }

                return false;
        }

        auto children_of(xnd_restart_target *t)
        const noexcept -> std::vector<xnd_restart_target *>
        {
                if (auto it = _map.find(t); it != _map.end()) {
                        return it->second;
                }

                return {};
        }
};
} /* namespace xnd */
#endif /* XND_RESTART_INTERNAL */
