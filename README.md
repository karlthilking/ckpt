MCND: Transparent Checkpoint-Restart for macOS on Apple Silicon
===============================================================

MCND provides user-level process checkpointing capabilities on macOS, with a focus on modern, ARM-based Macs. For an in-depth
overview on the internal architecture of MCND, see `internals.md`. 

MCND Source Tree
================

  * `src` - Source code for the checkpoint library, restart binary, and other utilities.
  * `include` - Header files used by relevant checkpoint-restart components.
  * `test` - Test/example programs that can be checkpointed by MCND.

Building MCND
=============

Compiling MCND is as straightforward as `make all`.
MCND does not use any external libraries or require any kernel modifications. The only prerequisites to compiling MCND are
access to the relevant system headers and a version of the clang compiler; Both should be shipped with any macOS SDK that would
be available a recent-enough macOS versions.

| Feature | Support |
|---------|---------|
| Single-threaded checkpointing | yes |
| Multi-threaded checkpointing | yes |
| PAC return address re-signing | yes |
| Checkpointing file descriptor state | yes |
| Thread local storage restoration | yes |
| Signal state checkpoint/restore | yes |
| Cross-reboot restarts | no |
| Checkpointing arm64e binaries | no |
| Multi-process computations | no |
| IPC (pipes, sockets) | no |
