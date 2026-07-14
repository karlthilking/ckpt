#!/usr/bin/env python3
# bench.py
import os, sys, pty, time, signal
from pathlib import Path
import subprocess

USAGE = \
    f"USAGE: python3 {sys.argv[0]} [options] program ...\n"             \
     "OPTIONS:\n"                                                       \
     "  -c, --bench-checkpoint NUMBER (default: 10)\n"                  \
     "     Benchmark checkpoint/restart time and checkpoint file\n"     \
     "     size for a total of NUMBER checkpoint/restart cycles\n"      \
     "  -r, --bench-runtime NUMBER (default: 10)\n"                     \
     "     Record native runtime of program as well as runtime with\n"  \
     "     libxnd.dylib loaded, running the program in each context\n"  \
     "     for NUMBER iterations\n"                                     \
     "  -i, --ckpt-interval SECONDS (default: 10)\n"                    \
     "     Specify the checkpoint interval when testing checkpoint\n"   \
     "     time, restart time, and checkpoint file size\n"              \
     "     (Only meaningful with --bench-checkpoint)\n"                 \
     "  -o, --output-dir DIRECTORY (default: ./benchmarks)\n"           \
     "     Directory to write benchmarks to\n"

PROGRAM, BENCHMARK_DIR = "", "./benchmarks"
XND_LAUNCH_PATH, XND_RESTART_PATH = "", ""
CKPT_ITERATIONS, RUNTIME_ITERATIONS, TIMEOUT = 10, 10, 10

def find_xnd_executables():
    global XND_LAUNCH_PATH, XND_RESTART_PATH
    found_launch, found_restart = False, False
    for s in [".", ".."]:
        d = Path(s)
        try:
            for file in d.iterdir():
                if not file.is_file():
                    continue
                path = str(file)
                if "xnd_launch" in path and ".c" not in path:
                    XND_LAUNCH_PATH = s + "/" + path
                    print(f"xnd_launch: {XND_LAUNCH_PATH}")
                    found_launch = True
                elif "xnd_restart" in path and ".c" not in path:
                    if "xnd_restart_internal" in path:
                        continue
                    XND_RESTART_PATH = s + "/" + path
                    print(f"xnd_restart: {XND_RESTART_PATH}")
                    found_restart = True
        except:
            continue
    if found_launch and found_restart:
        return True

    path_dirs = os.getenv("PATH").split(":")
    for s in path_dirs:
        d = Path(s)
        if not d.is_dir():
            continue
        try:
            for file in d.iterdir():
                if file.is_file():
                    path = str(file)
                    if "xnd_launch" in path and ".c" not in path:
                        XND_LAUNCH_PATH = path
                        found_launch = True
                    elif "xnd_restart" in path and ".c" not in path:
                        XND_RESTART_PATH = path
                        found_restart = True
                elif file.is_dir() and "xnd" in str(file):
                    path_dirs.append(str(file))
        except:
            continue
    if found_launch and found_restart:
        return True
    else:
        return False

def bench_overhead():
    progname = ""
    iterations = RUNTIME_ITERATIONS
    if PROGRAM.rfind("/") != -1:
        progname = PROGRAM[PROGRAM.rfind("/") + 1:]

    TIME_MSG = "Time in seconds = "

    NO_XND_TIMES, WITH_XND_TIMES = [0] * iterations, [0] * iterations
    NO_XND_FILE = BENCHMARK_DIR + "/" + progname + "_no_xnd.out"
    WITH_XND_FILE = BENCHMARK_DIR + "/" + progname + "_with_xnd.out"

    no_xnd_handle = open(NO_XND_FILE, "w")
    with_xnd_handle = open(WITH_XND_FILE, "w")

    no_xnd_handle.write(
        f"{NO_XND_FILE}\n\n"
        f"Native runtimes for {progname} (without libxnd.dylib)\n"
        f" ITERATIONS={iterations}\n\n"
    )

    with_xnd_handle.write(
        f"{WITH_XND_FILE}\n\n"
        f"Runtimes for {progname} with libxnd.dylib\n"
        f" ITERATIONS={iterations}\n\n"
    )

    for i in range(iterations):
        master_fd, slave_fd = pty.openpty()
        proc = subprocess.Popen(
            [PROGRAM],
            stdout=slave_fd, stderr=slave_fd,
            start_new_session=True, text=True, close_fds=True
        )
        os.close(slave_fd)
        with os.fdopen(master_fd, "r", errors="ignore") as pipe:
            for line in pipe:
                idx = line.find(TIME_MSG)
                if idx != -1:
                    l = idx + len(TIME_MSG)
                    while line[l] == " ":
                        l += 1
                    NO_XND_TIMES[i] = float(line[l:line.rfind("\n")])
                    no_xnd_handle.write(
                        f"Runtime #{i}: {NO_XND_TIMES[i]}\n"
                    )
                    break

        master_fd, slave_fd = pty.openpty()
        proc = subprocess.Popen(
            [XND_LAUNCH_PATH, PROGRAM],
            stdout=slave_fd, stderr=slave_fd,
            start_new_session=True, text=True, close_fds=True
        )
        os.close(slave_fd)
        with os.fdopen(master_fd, "r", errors="ignore") as pipe:
            for line in pipe:
                idx = line.find(TIME_MSG)
                if idx != -1:
                    l = idx + len(TIME_MSG)
                    while line[l] == " ":
                        l += 1
                    WITH_XND_TIMES[i] = float(line[l:line.rfind("\n")])
                    with_xnd_handle.write(
                        f"Runtime #{i}: {WITH_XND_TIMES[i]}\n"
                    )
                    break

    no_xnd_runtime_avg = sum(NO_XND_TIMES) / len(NO_XND_TIMES)
    with_xnd_runtime_avg = sum(WITH_XND_TIMES) / len(WITH_XND_TIMES)

    no_xnd_handle.write(
        "\n"
        "Average runtime:\n"
        f" MILLISECONDS: {no_xnd_runtime_avg * 1000}\n"
        f"      SECONDS: {no_xnd_runtime_avg}\n"
        f"      MINUTES: {no_xnd_runtime_avg / 60}\n"
    )

    with_xnd_handle.write(
        "\n"
        "Average runtime:\n"
        f" MILLISECONDS: {with_xnd_runtime_avg * 1000}\n"
        f"      SECONDS: {with_xnd_runtime_avg}\n"
        f"      MINUTES: {with_xnd_runtime_avg / 60}\n"
    )

    no_xnd_handle.close()
    with_xnd_handle.close()


def bench_ckpt_restart():
    iterations = CKPT_ITERATIONS
    CKPT_MSG = "Checkpoint complete: "
    CKPT_TIME_MSG = "Checkpoint took: "
    RESTART_TIME_MSG = "Restart took: "
    CKPT_DIR = ""
    CKPT_SIZES_FILE, CKPT_TIMES_FILE, RESTART_TIMES_FILE = "", "", ""
    CKPT_SIZES = [0] * iterations
    CKPT_TIMES = [0] * iterations
    RESTART_TIMES = [0] * iterations

    progname = ""
    if PROGRAM.rfind("/") != -1:
        progname = PROGRAM[PROGRAM.rfind("/") + 1:]

    CKPT_SIZES_FILE = BENCHMARK_DIR + "/" + progname + "_ckpt_sizes.out"
    CKPT_TIMES_FILE = BENCHMARK_DIR + "/" + progname + "_ckpt_times.out"
    RESTART_TIMES_FILE = BENCHMARK_DIR + "/" + progname + "_restart_times.out"

    ckpt_sizes_handle = open(CKPT_SIZES_FILE, "w")
    ckpt_times_handle = open(CKPT_TIMES_FILE, "w")
    restart_times_handle = open(RESTART_TIMES_FILE, "w")

    ckpt_sizes_handle.write(
        f"{CKPT_SIZES_FILE}\n\n"
        f"Checkpoint sizes for {progname}\n"
        f" ITERATIONS={iterations}\n"
        f"   INTERVAL={TIMEOUT}\n\n"
    )

    ckpt_times_handle.write(
        f"{CKPT_TIMES_FILE}\n\n"
        f"Checkpoint times for {progname}\n"
        f" ITERATIONS={iterations}\n"
        f"   INTERVAL={TIMEOUT}\n\n"
    )

    restart_times_handle.write(
        f"{RESTART_TIMES_FILE}\n\n"
        f"Restart times for {progname}\n"
        f" ITERATIONS={iterations}\n"
        f"   INTERVAL={TIMEOUT}\n\n"
    )

    i = 0
    while i < iterations:
        master_fd, slave_fd = pty.openpty()
        proc = subprocess.Popen(
            [XND_LAUNCH_PATH, "-i", str(TIMEOUT), PROGRAM],
            stdout=slave_fd, stderr=slave_fd,
            start_new_session=True, text=True, close_fds=True
        )
        os.close(slave_fd)
        with os.fdopen(master_fd, "r", errors="ignore") as pipe:
            for line in pipe:
                if CKPT_MSG in line:
                    l = line.find(CKPT_MSG) + len(CKPT_MSG)
                    CKPT_DIR = line[l:line.rfind("/")]
                elif CKPT_TIME_MSG in line:
                    os.killpg(proc.pid, signal.SIGINT)
                    break
        running = True
        while running and i < iterations:
            master_fd, slave_fd = pty.openpty()
            proc = subprocess.Popen(
                [XND_RESTART_PATH, CKPT_DIR],
                stdout=slave_fd, stderr=slave_fd,
                start_new_session=True, text=True, close_fds=True
            )
            os.close(slave_fd)
            with os.fdopen(master_fd, "r", errors="ignore") as pipe:
                ckpt_msg_found = False
                ckpt_time_found = False
                for line in pipe:
                    if CKPT_MSG in line:
                        ckpt_msg_found = True
                        l = line.find(CKPT_MSG) + len(CKPT_MSG)
                        ckpt_file = line[l:line.rfind("\n")]
                        CKPT_DIR = line[l:line.rfind("/")]
                        ckpt_size = os.path.getsize(ckpt_file)
                        CKPT_SIZES[i] = ckpt_size
                    elif RESTART_TIME_MSG in line:
                        l = line.find(RESTART_TIME_MSG) + len(RESTART_TIME_MSG)
                        RESTART_TIMES[i] = int(line[l:line.rfind("ms")])
                    elif CKPT_TIME_MSG in line:
                        ckpt_time_found = True
                        l = line.find(CKPT_TIME_MSG) + len(CKPT_TIME_MSG)
                        CKPT_TIMES[i] = int(line[l:line.rfind("ms")])
                        os.killpg(proc.pid, signal.SIGINT)
                        break
                if not ckpt_msg_found and not ckpt_time_found:
                    running = False

            if not running:
                top_level_dir = CKPT_DIR[:CKPT_DIR.find("/")]
                os.system(f"rm -rf {top_level_dir}")
                break

            ckpt_size_out_msg = \
                f"Checkpoint size #{i}: {CKPT_SIZES[i]} bytes\n"
            print(ckpt_size_out_msg, end="", flush=True)
            ckpt_sizes_handle.write(ckpt_size_out_msg)

            ckpt_time_out_msg = \
                f"Checkpoint time #{i}: {CKPT_TIMES[i]}ms\n"
            print(ckpt_time_out_msg, end="", flush=True)
            ckpt_times_handle.write(ckpt_time_out_msg)

            restart_time_out_msg = \
                f"Restart time #{i}: {RESTART_TIMES[i]}ms\n"
            print(restart_time_out_msg, end="\n", flush=True)
            restart_times_handle.write(restart_time_out_msg)
            i += 1

    top_level_dir = CKPT_DIR[:CKPT_DIR.find("/")]
    os.system(f"rm -rf {top_level_dir}")
    assert(len(CKPT_SIZES) == iterations)
    assert(len(CKPT_TIMES) == iterations)
    assert(len(RESTART_TIMES) == iterations)

    ckpt_size_avg = sum(CKPT_SIZES) / len(CKPT_SIZES)
    ckpt_time_avg = sum(CKPT_TIMES) / len(CKPT_TIMES)
    restart_time_avg = sum(RESTART_TIMES) / len(RESTART_TIMES)

    ckpt_sizes_handle.write(
        "\n"
        "Average checkpoint size:\n"
        f"     BYTES: {ckpt_size_avg}\n"
        f" KILOBYTES: {ckpt_size_avg / (1 << 10)}\n"
        f" MEGABYTES: {ckpt_size_avg / (1 << 20)}\n"
        f" GIGABYTES: {ckpt_size_avg / (1 << 30)}\n"
    )

    ckpt_times_handle.write(
        "\n"
        "Average checkpoint time:\n"
        f" MICROSECONDS: {ckpt_time_avg * 1000}\n"
        f" MILLISECONDS: {ckpt_time_avg}\n"
        f"      SECONDS: {ckpt_time_avg / 1000}\n"
    )

    restart_times_handle.write(
        "\n"
        f" MICROSECONDS: {ckpt_time_avg * 1000}\n"
        f" MILLISECONDS: {ckpt_time_avg}\n"
        f"      SECONDS: {ckpt_time_avg / 1000}\n"
    )

    ckpt_sizes_handle.close()
    ckpt_times_handle.close()
    restart_times_handle.close()

if __name__ == "__main__":
    argc = len(sys.argv)
    if argc < 2:
        print(USAGE)
        sys.exit(0)

    idx = 1
    do_bench_ckpt, do_bench_runtime = False, False
    while idx < argc:
        if "-c" in sys.argv[idx] or "--bench-checkpoint" in sys.argv[idx]:
            CKPT_ITERATIONS = int(sys.argv[idx + 1])
            do_bench_ckpt = True
            idx += 2
        elif "-r" in sys.argv[idx] or "--bench-runtime" in sys.argv[idx]:
            RUNTIME_ITERATIONS = int(sys.argv[idx + 1])
            do_bench_runtime = True
            idx += 2
        elif "-i" in sys.argv[idx] or "--ckpt-interval" in sys.argv[idx]:
            TIMEOUT = int(sys.argv[idx + 1])
            idx += 2
        elif "-o" in sys.argv[idx] or "--output-dir" in sys.argv[idx]:
            BENCHMARK_DIR = sys.argv[idx + 1]
            idx += 2
        else:
            PROGRAM = sys.argv[idx]
            idx += 1

    if len(PROGRAM) == 0:
        print(USAGE)
        sys.exit(0)

    if not find_xnd_executables():
        print("Failed to find xnd_launch and xnd_restart paths")
        sys.exit(-1)
    else:
        assert(len(XND_LAUNCH_PATH) != 0)
        assert(len(XND_RESTART_PATH) != 0)

    if do_bench_ckpt == True:
        bench_ckpt_restart()
    if do_bench_runtime == True:
        bench_overhead()

    sys.exit(0)
