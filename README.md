XND: Transparent Checkpoint-Restart for macOS on Apple Silicon
===============================================================

XND provides user-level and transparent checkpoint-restart capabilities for arm64 macOS. By executing a program with XND, the state of 
all processes under XND's control can be checkpointed. A checkpoint serializes the live state of one or more processes to 'checkpoint 
images' on-disk, allowing for each process to be restarted. For more details on using XND, see `XND Usage` below. For a high-level 
description of how XND works, see `Architecture/Internals of XND`.

XND Source Tree
================

  * `xnd` - Source code of core components and utilities
  * `include` - Generic header files
  * `test` - Test/example programs

Building XND
=============

```
git clone https://github.com/karlthilking/xnd
cd xnd && make all
```

XND Usage
=========

How does one use XND?
```
./xnd_launch a.out args ...

# Wait until you would like to take a checkpoint
./xnd_command --checkpoint
[xnd]: Checkpoint complete: <uuid>-checkpoints/epoch-<epoch>/ckpt-<xnd-id>.xnd

# Once you would like to restart
./xnd_restart path/to/ckpt-directory
```

Using XND's checkpoint-restart capabilities mainly involves the use of three binaries: `xnd_launch`, `xnd_command`, and `xnd_restart`.
`xnd_launch` executes a program with `libxnd.dylib` preloaded, allowing for the program to be checkpointed at any time during its 
execution. Thereafter, `xnd_command` can be used to issue a command to a program that was started with `xnd_launch`. Most relevantly, 
`xnd_command --checkpoint` will arrange for a checkpoint to be taken. Lastly, `xnd_restart path/to/ckpt` will restore the state of the 
checkpointed computation, continuing execution where the program left off.

A demonstration of how to use XND is provided:

<p align="center">
 <a href=https://imgur.com/am7lvaP">
  <img src="https://imgur.com/am7lvaP.gif" width="600px" height="400px"/>
 </a>
</p>

XND Checkpoint Directory
========================

```
.
└── <uuid>-checkpoints
         ├── epoch-0
         │     ├── ckpt-1001.xnd
         │     ├── ckpt-1002.xnd
         │     ├── ...
         │     ├── ckpt-N.xnd
         │     └── ckpt-manifest.xnd
         ├── epoch-1
         ├── ...
         └── epoch-n
```

By default, XND will use a directory hierarchy to organize checkpoints. The top-level directory is named as `<uuid>-checkpoints` where 
`uuid` is used as an identifier for one computation group (one or more processes). Within this directory, one subdirectory will appear 
for each time this computation has been checkpointed. For example, the first time of computation is checkpointed, a collection of 
checkpoint files will be created in `epoch-0`, then `epoch-1`, then `epoch-2` and so on. This is an important detail because a path of 
the form `<uuid>-checkpoints/epoch-<epoch>` is a required argument to `xnd_restart`. 

Note that one is free to change the names of checkpoint directories for their own purposes. `xnd_restart` will function as expected given any directory which contains valid, untampered checkpoint files. 

For example, the following usage does not create any issues:

```sh
./xnd_command --checkpoint
[xnd]: Checkpoint complete: 25EA37D1-C418-44CA-91C3-1DAB4541218F-checkpoints/epoch-5/ckpt-1001.xnd
[xnd]: Checkpoint complete: 25EA37D1-C418-44CA-91C3-1DAB4541218F-checkpoints/epoch-5/ckpt-1002.xnd
...
[xnd]: Checkpoint complete: 25EA37D1-C418-44CA-91C3-1DAB4541218F-checkpoints/epoch-5/ckpt-1008.xnd

mv 25EA37D1-C418-44CA-91C3-1DAB4541218F-checkpoints/epoch-5/ these-are-my-checkpoints
./xnd_restart these-are-my-checkpoints
```

XND General Information and Feature Support
===========================================

| Category/Feature | Information/Status |
|------------------|--------------------|
| OS | macOS (tested on Sonoma 14.5, Darwin 23.5.0) |
| Arch | arm64 |
| Operates in userspace? | Yes |
| Requires root privilege? | No |
| Can checkpoint unmodified programs? | Yes |
| Handles pointer authentication? | Yes, to the extent that PAC is used in an arm64 process (arm64e binaries are not supported) |
| Can checkpoint arm64e binaries? | Not supported |
| Multithread support | Yes |
| Thread-local variables and TSD | Yes |
| Multiprocess support | `fork()` is handled, `exec*()` is not yet supported |
| PIDs, PGIDs, SIDs, etc. | PIDs remain consistent to programs through resource virtualization (see `xnd/pid/`) |
| Parallel computing | XND can checkpoint OpenMP, but parallel frameworks which use IPC/shared resources are not yet supported |
| Parent-child relationships | Yes, `xnd_restart` recreates process hierarchies and relationships |
| Process groups and sessions | Yes, `xnd_restart` recreates process groups and sessions |
| Restores signal state? | Yes, signal handlers, blocked signals, and alternate stacks are restored |
| Regular files | File descriptors and offsets are restored (semantics of inherited file descriptors may be lost in a child process) |
| Pipes/Sockets | Not yet supported |
| Process migration | In theory, yes, if dyld_shared_version* version is equivalent |
| Shared memory or other shared resources | Not yet supported |

Example Progams/Applications
============================

XND has been tested for C, C++, OpenMP, and Python/Python3 (with external numerical libraries), however, it is likely that there are 
more programming environments and applications which may be checkpointed by XND.

Examples of programs that XND cannot checkpoint include arm64e binaries, applications built with macOS's 'Hardened Runtime', as well as 
applications and programs which rely on resources that cannot yet be checkpointed by XND (e.g. IPC and shared memory). For the most 
part, any application that does not strip `DYLD*` environment variables (loader environment variables), and is not compiled for the 
`arm64e` architecture can, currently or possibly in the future, be checkpointed by XND.
