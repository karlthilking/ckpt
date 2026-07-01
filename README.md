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

Architecture/Internals of XND
=============================

In order for XND to obtain checkpoint control, a loader environment variable, `DYLD_INSERT_LIBRARIES`, is set to `libxnd.dylib` before 
a program is executed. This way, `libxnd.dylib` (XND's checkpoint library) is preloaded before all other libraries, allowing XND to 
create a 'checkpoint thread' at initialization time. The checkpoint thread establishes connection to a global coordinator (who listens 
on a UNIX socket) and sets up a signal handler for a given checkpoint signal (SIGUSR2 by default). As the name implies, the checkpoint 
thread will become responsible for handling checkpointing its own process (once a checkpoint is requested).

For the most part, a program injected with `libxnd.dylib` will mostly run as usual without any intervention by XND. An exception to this
are stateful events such as thread creation, process creation, and opening of files, among other things. In order to be aware of such
events, `libxnd.dylib` interposes relevant library functions and system calls which require additional bookkeeping or arbitration.

When a checkpoint is requested (via `./xnd_command --checkpoint`), the coordinator will be the first entity to be notified of the 
request. Then, the coordinator will broadcast the `XND_CKPT_REQUEST` message to the checkpoint thread for each process, who should
be waiting for a coordinator message over their socket connection. Each checkpoint thread will acknowledge the request, enter a global
barrier, and wait for the coordinator to release the barrier. Once this global barrier is released, each checkpoint thread will be
responsible for managing the checkpoint of their process.

In each process, the checkpoint thread must now suspend all user threads. Remember that, during initialization, the checkpoint thread
established a signal handler to run for a given checkpoint signal. Thus, the checkpoint thread will signal each user thread in the
process, causing them to jump to `libxnd.dylib:thread_sighandler`. In this signal handler, each thread will save their register context,
signal state, and TLS (tpidrro_el0), and after, enter a process-local barrier. Now that each user thread is suspended in a barrier, the
checkpoint thread is safe to serialize all of userspace memory, its own register context, and other metadata to a checkpoint file. 
Thus, the checkpoint has completed. Everything needed by XND to recreate the state of one process is included in a checkpoint file, and
thus, to recreate a computation of N processes, N new processes will created and will restore their state from their respective 
checkpoint.
