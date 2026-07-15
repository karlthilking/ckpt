#!/usr/bin/env python3
import os, sys, time, pty, signal
import subprocess
from pathlib import Path
from typing import List

USAGE = \
f"Usage: {sys.argv[0]} [options] program ...\n"   \
 "Options:\n"                                       \
 "  -c, --ckpt-iterations NUMBER\n"                 \
 "  -i, --ckpt-interval SECONDS\n"                  \
 "  -r, --run-iterations NUMBER\n"                  \
 "  -o, --output PATH\n"

xnd_launch_path, xnd_restart_path = "", ""
run_iterations, ckpt_iterations, ckpt_interval = 10, 25, 10

def find_xnd_executables():
    global xnd_launch_path, xnd_restart_path
    found_launch, found_restart = "", ""
    path_dirs = [".", ".."]
    path_dirs.extend(os.getenv("PATH").split(":"))
    for s in path_dirs:
        d = Path(s)
        try:
            for file in d.iterdir():
                if not file.is_file():
                    continue
                path = str(file)
                if "xnd_launch" in path and os.access(path, os.X_OK):
                    xnd_launch_path = "./" + path if s == "." else path
                    found_launch = True
                elif "xnd_restart" in path and os.access(path, os.X_OK):
                    if "xnd_restart_internal"  in path:
                        continue
                    xnd_restart_path = "./" + path if s == "." else path
                    found_restart = True
                elif file.is_dir() and "xnd" in path:
                    path_dirs.append(path)
        except:
            continue
    return found_launch and found_restart

def bench_runtime(
    program: str, no_xnd_runtimes: List[float], with_xnd_runtimes: List[float]
):
    assert(len(no_xnd_runtimes) == run_iterations)
    assert(len(with_xnd_runtimes) == run_iterations)

    for itr in range(run_iterations):
        start = time.time()
        proc = subprocess.run(
            [program],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        end = time.time()
        no_xnd_runtimes[itr] = float(end - start)
        print("{} runtime native #{}: {}".format(
            program, itr, no_xnd_runtimes[itr]
        ))

        start = time.time()
        proc = subprocess.run(
            [xnd_launch_path, program],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        end = time.time()
        with_xnd_runtimes[itr] = float(end - start)
        print("{} runtime with libxnd #{}: {}".format(
            program, itr, with_xnd_runtimes[itr]
        ))

def bench_ckpt_restart(
    program: str, use_compression: bool,
    ckpt_sizes: List[int], ckpt_times: List[int], restart_times: List[int]
):
    ckpt_msg = "Checkpoint complete: "
    ckpt_time_msg = "Checkpoint took: "
    restart_time_msg = "Restart took: "
    ckpt_dir = ""
    zlib_arg = "--use-zlib" if use_compression else "--no-zlib"
    itr = 0

    assert(len(ckpt_sizes) == ckpt_iterations and \
           len(ckpt_times) == ckpt_iterations and \
           len(restart_times) == ckpt_iterations)

    while itr < ckpt_iterations:
        master_fd, slave_fd = pty.openpty()
        proc = subprocess.Popen(
            [xnd_launch_path, "-i", str(ckpt_interval), zlib_arg, program],
            stdout=slave_fd, stderr=slave_fd,
            start_new_session=True, text=True, close_fds=True
        )
        os.close(slave_fd)
        with os.fdopen(master_fd, "r", errors="ignore") as pipe:
            for line in pipe:
                if ckpt_msg in line:
                    l = line.find(ckpt_msg) + len(ckpt_msg)
                    ckpt_dir = line[l:line.rfind('/')]
                elif ckpt_time_msg in line:
                    os.killpg(proc.pid, signal.SIGINT)
                    break
        running = True
        while running and itr < ckpt_iterations:
            master_fd, salve_fd = pty.openpty()
            proc = subprocess.Popen(
                [xnd_restart_path, ckpt_dir],
                stdout=slave_fd, stderr=slave_fd,
                start_new_session=True, text=True, close_fds=True
            )
            os.close(slave_fd)
            with os.fdopen(master_fd, "r", errors="ignore") as pipe:
                found_ckpt_msg, found_ckpt_time = False, False
                for line in pipe:
                    if ckpt_msg in line:
                        found_ckpt_msg = True
                        l = line.find(ckpt_msg) + len(ckpt_msg)
                        ckpt_file = line[l:line.rfind('\n')]
                        ckpt_dir = line[l:line.rfind('/')]
                        ckpt_size = os.path.getsize(ckpt_file)
                        ckpt_sizes[itr] = ckpt_size
                    elif restart_time_msg in line:
                        l = line.find(restart_time_msg) + len(restart_time_msg)
                        restart_times[itr] = int(line[l:line.rfind('ms')])
                    elif ckpt_time_msg in line:
                        found_ckpt_time = True
                        l = line.find(ckpt_time_msg) + len(ckpt_time_msg)
                        ckpt_times[itr] = int(line[l:line.rfind('ms')])
                        os.killpg(proc.pid, signal.SIGINT)
                        break
                if not found_ckpt_msg or not found_ckpt_time:
                    running = False

            if not running:
                top_level_dir = ckpt_dir[:ckpt_dir.find('/')]
                os.system(f"rm -rf {top_level_dir}")
                break

            print(
                f"Iteration #{itr}:\n"
                f" checkpoint size: {ckpt_sizes[itr]}\n"
                f" checkpoint time: {ckpt_times[itr]}\n"
                f"    restart time: {restart_times[itr]}\n",
                end="", flush=True
            )
            itr += 1

    top_level_dir = ckpt_dir[:ckpt_dir.rfind('/')]
    os.system(f"rm -rf {top_level_dir}")
    for itr in range(ckpt_iterations):
        assert(ckpt_sizes[itr] != 0)
        assert(ckpt_times[itr] != 0)
        assert(restart_times[itr] != 0)

def bench(program: str, output_file: str):
    ckpt_sizes_compressed = [0] * ckpt_iterations
    ckpt_sizes_uncompressed = [0] * ckpt_iterations

    ckpt_times_compressed = [0] * ckpt_iterations
    ckpt_times_uncompressed = [0] * ckpt_iterations

    restart_times_compressed = [0] * ckpt_iterations
    restart_times_uncompressed = [0] * ckpt_iterations

    no_xnd_runtimes = [0.0] * run_iterations
    with_xnd_runtimes = [0.0] * run_iterations

    progname = program
    if program.rfind('/') != -1:
        progname = program[program.rfind('/') + 1:]

    output_file_handle = open(output_file, "w")
    output_file_handle.write("{}: {}\n\n".format(output_file, progname))

    # Time native runtime against runtime with libxnd.dylib loaded
    bench_runtime(
        program=program,
        no_xnd_runtimes=no_xnd_runtimes,
        with_xnd_runtimes=with_xnd_runtimes
    )

    # Checkpoint-restart benchmarks without using compression
    bench_ckpt_restart(
        program=program,
        use_compression=False,
        ckpt_sizes=ckpt_sizes_uncompressed,
        ckpt_times=ckpt_times_uncompressed,
        restart_times=restart_times_uncompressed
    )

    # Checkpoint-restart benchmarks with compressed checkpoint files
    bench_ckpt_restart(
        program=program,
        use_compression=True,
        ckpt_sizes=ckpt_sizes_compressed,
        ckpt_times=ckpt_times_compressed,
        restart_times=restart_times_compressed
    )

    # Checkpoint sizes without compression
    output_file_handle.write("Checkpoint sizes (without compression):\n")
    for itr in range(ckpt_iterations):
        size = ckpt_sizes_uncompressed[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"     BYTES: {size / (1 << 0)}\n"
            f" KILOBYTES: {size / (1 << 10)}\n"
            f" MEGABYTES: {size / (1 << 20)}\n"
            f" GIGABYTES: {size / (1 << 30)}\n"
        )
    output_file_handle.write('\n')

    # Checkpoint sizes with compression
    output_file_handle.write("Checkpoint sizes (with compression):\n")
    for itr in range(ckpt_iterations):
        size = ckpt_sizes_compressed[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"     BYTES: {size / (1 << 0)}\n"
            f" KILOBYTES: {size / (1 << 10)}\n"
            f" MEGABYTES: {size / (1 << 20)}\n"
            f" GIGABYTES: {size / (1 << 30)}\n"
        )
    output_file_handle.write('\n')

    # Checkpoint times without compression
    output_file_handle.write("Checkpoint times (without compression):\n")
    for itr in range(ckpt_iterations):
        ckpt_time = ckpt_times_uncompressed[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"      SECONDS: {ckpt_time / 1000}\n"
            f" MILLISECONDS: {ckpt_time}\n"
            f" MICROSECONDS: {ckpt_time * 1000}\n"
        )
    output_file_handle.write('\n')

    # Checkpoint times with compression
    output_file_handle.write("Checkpoint times (with compression):\n")
    for itr in range(ckpt_iterations):
        ckpt_time = ckpt_times_compressed[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"      SECONDS: {ckpt_time / 1000}\n"
            f" MILLISECONDS: {ckpt_time}\n"
            f" MICROSECONDS: {ckpt_time * 1000}\n"
        )
    output_file_handle.write('\n')

    # Restart times without compression
    output_file_handle.write("Restart times (without compression):\n")
    for itr in range(ckpt_iterations):
        restart_time = restart_times_uncompressed[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"      SECONDS: {restart_time / 1000}\n"
            f" MILLISECONDS: {restart_time}\n"
            f" MICROSECONDS: {restart_time * 1000}\n"
        )
    output_file_handle.write('\n')

    # Restart times with compression
    output_file_handle.write("Restart times (with compression):\n")
    for itr in range(ckpt_iterations):
        restart_time = restart_times_compressed[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"      SECONDS: {restart_time / 1000}\n"
            f" MILLISECONDS: {restart_time}\n"
            f" MICROSECONDS: {restart_time * 1000}\n"
        )
    output_file_handle.write('\n')

    # Runtimes without libxnd.dylib loaded
    output_file_handle.write("Runtimes (without libxnd):\n")
    for itr in range(run_iterations):
        runtime = no_xnd_runtimes[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"      MINUTES: {runtime / 60}\n"
            f"      SECONDS: {runtime}\n"
            f" MILLISECONDS: {runtime * 1000}\n"
        )
    output_file_handle.write('\n')
    
    # Runtimes with libxnd.dylib loaded
    output_file_handle.write("Runtimes (with libxnd):\n")
    for itr in range(run_iterations):
        runtime = with_xnd_runtimes[itr]
        output_file_handle.write(
            f"Iteration {itr}:\n"
            f"      MINUTES: {runtime / 60}\n"
            f"      SECONDS: {runtime}\n"
            f" MILLISECONDS: {runtime * 1000}\n"
        )
    output_file_handle.write('\n')

    output_file_handle.write('\n')
    output_file_handle.close()

if __name__ == "__main__":
    argc = len(sys.argv)
    program, output_file = "", ""
    if argc < 2:
        print(USAGE)
        sys.exit(0)

    if not find_xnd_executables():
        print("Failed to find xnd_launch and xnd_restart")
        sys.exit(-1)

    assert(len(xnd_launch_path) >= len("xnd_launch"))
    assert(len(xnd_restart_path) >= len("xnd_restart"))

    idx = 1
    while idx < argc:
        if "-c" in sys.argv[idx] or "--ckpt-iterations" in sys.argv[idx]:
            ckpt_iterations = int(sys.argv[idx + 1])
            idx += 2
        elif "-r" in sys.argv[idx] or "--run-iterations" in sys.argv[idx]:
            run_iterations = int(sys.argv[idx + 1])
            idx += 2
        elif "-o" in sys.argv[idx] or "--output" in sys.argv[idx]:
            output_file = sys.argv[idx + 1]
            idx += 2
        elif "-i" in sys.argv[idx] or "--ckpt-interval" in sys.argv[idx]:
             ckpt_interval = int(sys.argv[idx + 1])
             idx += 2
        else:
            program = sys.argv[idx]
            break

    if len(output_file) == 0:
        print(USAGE)
        sys.exit(-1)
    elif len(program) == 0:
        print(USAGE)
        sys.exit(-1)

    bench(program, output_file)
    sys.exit(0)
