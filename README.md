XND: Transparent Checkpoint-Restart for macOS on Apple Silicon
===============================================================

XND provides user-level process checkpointing capabilities on macOS, focusing on modern, ARM-based Macs. For an in-depth
overview on the internal architecture of XND, see `internals.md`. 

XND Source Tree
================

  * `src` - Source code for the checkpoint library, restart binary, and other utilities.
  * `include` - Header files used by relevant checkpoint-restart components.
  * `test` - Test/example programs that can be checkpointed by XND.

Building XND
=============

```
git clone https://github.com/karlthilking/xnd
cd xnd && make all
```

Feature Support
================

| Feature | Status |
|---------|--------|
| Single-threaded checkpointing | supported |
| Multi-threaded checkpointing | POSIX threads supported (see example applications) |
| PAC return address re-siging | supported |
| File desciptor restoration | regular files only |
| Restoring thread local storage | supported |
| Signal state restoration | masks and handlers are restored (pending signals are lost) |
| Cross-reboot restarts | not supported |
| Checkpointing arm64e binaries | not supported |
| Multi-process computations | not supported |
| Restoring IPC constructs (pipes, sockets) | not supported |

Supported Applications
======================
