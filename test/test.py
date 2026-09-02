#!/usr/bin/env python3
import os
import sys
import pty
import signal
import subprocess
import config
import time
from typing import Dict, List, Optional

USAGE = \
f"Usage: {sys.argv[0]} [options] ...\n" \
 "Options:\n" \
 " -t, --test ID_0,ID_1,...\n" \
 "  Run only the tests specified in this comma separated list of ids\n" \
 " -a, --all\n" \
 "  Run all registered tests (default)\n" \
 " -i, --ckpt-interval SECONDS\n" \
 "  Checkpoint interval to use for tests (default: 5)\n" \
 " -n, --iterations NUMBER\n" \
 "  Number of checkpoint/restart cycles per test (default: 3)\n" \
 " -h, --help\n" \
 "  Display this help message\n" \
 " -s, --show\n" \
 "  Show available tests and test configurations\n"

CKPT_MSG = "Checkpoint complete: "
CKPT_ENABLED = False

def print_exit_status(name: str, code: int) -> bool:
    if code == 0:
        print(f"[{name}] PASS: exited with exit code 0")
        return True
    else:
        print(f"[{name}] FAIL: exited with exit code {code}")
        return False

def try_set_ckpt_interval(xnd_command: str, interval: int) -> int:
    code = subprocess.run(
        [xnd_command, "--ckpt-interval", str(interval)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode
    return code

def set_ckpt_interval(
        xnd_command: str, interval: int, timeout: int) -> bool:
    code = 255
    deadline = time.time() + timeout
    while code != 0:
        time.sleep(1)
        code = try_set_ckpt_interval(xnd_command, interval)
        if code != 0 and time.time() >= deadline:
            return False
    return True

def enable_checkpoint(
        xnd: Dict[str, str], interval: int, timeout: int) -> bool:
    global CKPT_ENABLED
    xnd_command = xnd["xnd_command"]
    success = set_ckpt_interval(xnd_command, interval, timeout)
    if success:
        CKPT_ENABLED = True
    return success

def disable_checkpoint(xnd: Dict[str, str], timeout: int) -> bool:
    global CKPT_ENABLED
    xnd_command = xnd["xnd_command"]
    success = set_ckpt_interval(xnd_command, 0, timeout)
    if success:
        CKPT_ENABLED = False
    return success

def do_checkpoint(xnd: Dict[str, str]) -> bool:
    args = [xnd["xnd_command"], "--checkpoint"]
    code = subprocess.run(
        args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode
    if code != 0:
        print(f"{' '.join(args)} exited with exit code {code}")
        return False
    return True

def parse_checkpoint(master_fd: int) -> Optional[str]:
    with os.fdopen(master_fd, "r", errors="ignore") as pipe:
        for line in pipe:
            if CKPT_MSG in line:
                l = line.find(CKPT_MSG) + len(CKPT_MSG)
                return line[l:].rstrip('\n')
    return None

def await_checkpoint(
        xnd: Dict[str, str], fd: int, interval: int
) -> Optional[str]:
    global CKPT_ENABLED
    if not CKPT_ENABLED:
        time.sleep(interval)
        do_checkpoint(xnd)
    return parse_checkpoint(fd)

def kill_computation(pgid: int):
    try:
        os.killpg(pgid, signal.SIGTERM)
    except:
        pass
    ret = subprocess.run(['pgrep', 'xnd'], capture_output=True, text=True)
    pidlist = [int(p) for p in ret.stdout.split('\n') if len(p) != 0]
    for pid in pidlist:
        try:
            os.kill(pid, signal.SIGTERM)
        except:
            pass

def verify(
    test_id: str,
    ckpt_interval: int,
    iterations: int,
    xnd: Dict[str, str]
) -> bool:
    global CKPT_ENABLED
    name = config.CONFIG[test_id]["name"]
    config.prepare_test(test_id)
    argv = config.get_test_argv(test_id)

    print(f"Launch {name} (interval: {ckpt_interval}s)")

    master_fd, slave_fd = pty.openpty()
    proc = subprocess.Popen(
        [xnd["xnd_launch"], "--interval", str(ckpt_interval)] + argv,
        stdout=slave_fd, stderr=slave_fd,
        start_new_session=True, text=True, close_fds=True
    )
    CKPT_ENABLED = True
    os.close(slave_fd)

    ckpt_dir = await_checkpoint(xnd, master_fd, ckpt_interval)
    if ckpt_dir is None:
        print(f"[{name}] Exit before first checkpoint\n")
        code = proc.wait()
        return print_exit_status(name, code)

    top_level_dir = None
    if ckpt_dir.rfind('/') != -1:
        top_level_dir = ckpt_dir[:ckpt_dir.rfind('/')]

    kill_computation(proc.pid)
    proc.wait()

    for cycle in range(iterations):
        last = True if cycle == iterations - 1 else False
        print(f"[{name}] restart {cycle + 1}/{iterations}"
              f"{' (final, running to completion)' if last else ''}")

        if last:
            ret = False
            proc = subprocess.Popen(
                [xnd["xnd_restart"], ckpt_dir],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                start_new_session=True, close_fds=True
            )
            disable_checkpoint(xnd, timeout=5)

            code = proc.wait()
            ret = print_exit_status(name, code)
            if top_level_dir is not None:
                os.system(f"rm -rf {top_level_dir}")
            return ret

        master_fd, slave_fd = pty.openpty()
        proc = subprocess.Popen(
            [xnd["xnd_restart"], ckpt_dir],
            stdout=slave_fd, stderr=slave_fd,
            start_new_session=True, text=True, close_fds=True
        )
        os.close(slave_fd)

        next_ckpt_dir = await_checkpoint(xnd, master_fd, ckpt_interval)
        if next_ckpt_dir is None:
            code = proc.wait()
            ret = print_exit_status(name, code)
            if top_level_dir is not None:
                os.system(f"rm -rf {top_level_dir}")
            return ret

        kill_computation(proc.pid)
        proc.wait()
        ckpt_dir = next_ckpt_dir

    if top_level_dir is not None:
        os.system(f"rm -rf {top_level_dir}")
    return True

if __name__ == "__main__":
    test_ids = list(config.TESTS)
    ckpt_interval = 5
    iterations = 3

    argc = len(sys.argv)
    i = 1
    while i < argc:
        arg = sys.argv[i]
        if arg in ("-t", "--test"):
            test_ids = sys.argv[i + 1].split(",")
            i += 2
        elif arg in ("-a", "--all"):
            test_ids = list(config.TESTS)
            i += 1
        elif arg in ("-i", "--ckpt-interval"):
            ckpt_interval = int(sys.argv[i + 1])
            i += 2
        elif arg in ("-n", "--iterations"):
            iterations = int(sys.argv[i + 1])
            i += 2
        elif arg in ("-h", "--help"):
            print(USAGE)
            sys.exit(0)
        elif arg in ("-s", "--show"):
            config.display()
            sys.exit(0)
        else:
            print(f"Unrecognized argument: {arg}")
            print(USAGE)
            sys.exit(-1)

    unknown = [t for t in test_ids if t not in config.CONFIG]
    if unknown:
        print(f"Unknown test id(s): {', '.join(unknown)}")
        sys.exit(-1)

    xnd = config.get_xnd_executables()
    missing = [n for n in config.XND_EXECUTABLES if n not in xnd]
    if missing:
        print(f"Failed to find: {', '.join(missing)}")
        sys.exit(-1)

    print(f"Running regression tests for {len(test_ids)} program(s), "
          f"{iterations} checkpoint/restart cycle(s) each:")
    results = {}
    for test_id in test_ids:
        results[test_id] = verify(test_id, ckpt_interval, iterations, xnd)

    print("\nSummary:")
    failed = 0
    for test_id in test_ids:
        passed = results[test_id]
        failed += not passed
        print(f"  [{'PASS' if passed else 'FAIL'}] "
              f"{config.CONFIG[test_id]['name']} ({test_id})")
        config.cleanup_test(test_id)

    sys.exit(1 if failed else 0)
