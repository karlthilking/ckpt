# CPU Security: ARM PAC and Intel CET
### Hardware-Enforced Control Flow Integrity

This document summarizes the current state (as of 2026) of **Pointer Authentication (PAC)** and **Control-flow Enforcement Technology (CET)** across major operating systems.

---

## 1. Feature Overview

| Feature | Architecture | Mechanism | Primary Defense |
| :--- | :--- | :--- | :--- |
| **PAC** | ARM (v8.3+) | Cryptographic Keys/Signatures | Pointer Tampering |
| **CET** | Intel (11th Gen+) | Shadow Stack & Landing Pads | ROP/JOP Attacks |

> **The "Key or Crash" Rule:** If a pointer's signature (PAC) or the return address stack (CET) does not match the hardware's expected value, the CPU will immediately terminate the process to prevent an exploit.

---

## 2. Operating System Verification

### Windows 11
Windows refers to this as **Hardware-enforced Stack Protection**.

* **System-Wide Check:**
    1. Open **Windows Security**.
    2. Navigate to **Device Security** > **Core isolation details**.
    3. Ensure **Kernel-mode Hardware-enforced Stack Protection** is toggled **On**.
* **Per-Process Check:**
    1. Open **Task Manager** > **Details** tab.
    2. Right-click a header > **Select columns**.
    3. Enable **Hardware-enforced Stack Protection**.

### macOS (Apple Silicon)
PAC is a foundational requirement for the `arm64e` architecture used in M-series chips.

* **Architecture Check:**
    * Run `uname -m` in Terminal. If it returns `arm64e`, PAC is active at the system level.
* **Binary Check:**
    * Run `vtool -show-build /usr/bin/login`.
    * Look for the `arm64e` tag, which indicates the binary is compiled to require pointer authentication.

### Linux
Linux support depends on both the Kernel version (6.6+) and the specific distribution's compiler flags.

* **Check CPU Capability:**
    ```bash
    cat /proc/cpuinfo | grep -E "shstk|ibt|pac"
    ```
* **Check Application Support:**
    ```bash
    readelf -n /bin/ls | grep -i "x86 feature"
    ```
    *(Look for `SHSTK` for Shadow Stack or `IBT` for Indirect Branch Tracking)*

---

## 3. Troubleshooting "Disabled" Status
If your hardware supports these features but they are marked as **Disabled**:

1.  **Incompatible Drivers:** Older kernel-mode drivers (common in anti-virus or anti-cheat) often block CET/PAC.
2.  **BIOS/UEFI Settings:** Ensure "Control-flow Enforcement" or "Virtualization Extensions" are enabled in your motherboard firmware.
3.  **Legacy Apps:** Some older applications use "Just-In-Time" (JIT) compilation that mimics attack behavior, causing the OS to disable protection for compatibility.

---

**Would you like me to explain the specific cryptographic algorithm (QARMA) that ARM uses to generate these pointer keys?**
