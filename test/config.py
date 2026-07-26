#!/usr/bin/env python3
import os
from pathlib import Path
from typing import Any, Dict, List

CWD = Path(__file__).resolve().parent
BIN = CWD / "bin"

INPUTS = CWD / "inputs"
OPENSSL_INPUT = INPUTS / "openssl_enc.in"
OPENSSL_INPUT_GB = 5
OPENSSL_INPUT_MB = OPENSSL_INPUT_GB << 10

TESTS = (
    "conjugate_gradient",
    "heat_diffusion",
    "nbody",
    "jacobi",
    "spectral_pde",
    "nas_lu_c",
    "nas_bt_c",
    "nas_cg_c",
    "openssl_enc",
)

XND_EXECUTABLES = ("xnd_launch", "xnd_restart")

CONFIG = {
    "conjugate_gradient": {
        "name": "Conjugate Gradient",
        "path": str(BIN / "01_conjugate_gradient"),
        "input": None,
        "args": ["-e", "15"],
    },
    "heat_diffusion": {
        "name": "Heat Diffusion (OpenMP)",
        "path": str(BIN / "02_heat_diffusion"),
        "input": None,
        "args": ["-g", "1024,1024", "-t", "8"],
    },
    "nbody": {
        "name": "N-Body",
        "path": str(BIN / "03_nbody"),
        "input": None,
        "args": ["-s", "3000", "-n", "2048"],
    },
    "jacobi": {
        "name": "Jacobi",
        "path": str(BIN / "04_jacobi"),
        "input": None,
        "args": [],
    },
    "spectral_pde": {
        "name": "Spectral PDE",
        "path": "python3",
        "input": None,
        "args": [str(CWD / "05_spectral_pde.py")],
    },
    "nas_lu_c": {
        "name": "NAS LU Class C",
        "path": str(BIN / "lu.C.x"),
        "input": None,
        "args": [],
    },
    "nas_bt_c": {
        "name": "NAS BT Class C",
        "path": str(BIN / "bt.C.x"),
        "input": None,
        "args": [],
    },
    "nas_cg_c": {
        "name": "NAS CG Class C",
        "path": str(BIN / "cg.C.x"),
        "input": None,
        "args": [],
    },
    "openssl_enc": {
        "name": "OpenSSL AES-256-CBC enc",
        "path": "/usr/bin/openssl",
        "input": str(OPENSSL_INPUT),
        "args": [
            "enc", "-aes-256-cbc", "-pbkdf2", "-k", "testpassword123",
            "-in", str(OPENSSL_INPUT), "-out", "/dev/null",
        ],
    },
}

def config_display():
    for test_id in TESTS:
        cfg = CONFIG[test_id]
        input = "None" if cfg["input"] is None else cfg["input"]
        print(
            f"{test_id}:\n"
            f"   name: {cfg['name']}\n"
            f"   path: {cfg['path']}\n"
            f"   args: {cfg['args']}\n"
            f"  input: {input}\n"
        )

def config_prepare_test(test_id: str):
    if test_id == "openssl_enc" and not OPENSSL_INPUT.exists():
        INPUTS.mkdir(parents=True, exist_ok=True)
        os.system(f"dd if=/dev/urandom of={OPENSSL_INPUT} "
                  f"bs=1m count={OPENSSL_INPUT_MB} 2>/dev/null")

def config_cleanup_test(test_id: str):
    if test_id == "openssl_enc" and OPENSSL_INPUT.exists():
        os.system(f"rm -f {OPENSSL_INPUT}")

def config_get_test_argv(test_id: str) -> List[str]:
    cfg = CONFIG[test_id]
    return [cfg["path"]] + cfg["args"]

def config_xnd_executables() -> Dict[str, str]:
    found: Dict[str, str] = {}
    search = [".", ".."] + os.getenv("PATH", "").split(":")
    idx = 0
    while idx < len(search):
        s = search[idx]
        idx += 1
        if not s:
            continue
        d = Path(s)
        try:
            for entry in d.iterdir():
                if entry.is_dir() and "xnd" in str(entry):
                    search.append(str(entry))
                    continue
                name = entry.name
                if name in XND_EXECUTABLES and name not in found \
                        and entry.is_file() and os.access(entry, os.X_OK):
                    found[name] = "./" + str(entry) if s == "." else str(entry)
        except OSError:
            continue
    return found
