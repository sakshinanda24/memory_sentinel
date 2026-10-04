# MemorySentinel — Installation Guide

## Prerequisites

| Requirement | Version | Check |
|-------------|---------|-------|
| Linux kernel | 5.x or 6.x | `uname -r` |
| GCC / G++ | 9+ | `g++ --version` |
| Kernel headers | Must match running kernel | `ls /lib/modules/$(uname -r)/build` |
| make | Any | `make --version` |
| Root / sudo | — | required for insmod and daemon |

### Install dependencies

**Ubuntu / Debian**
```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

**Fedora / RHEL**
```bash
sudo dnf install kernel-devel kernel-headers gcc-c++ make
```

**Arch Linux**
```bash
sudo pacman -S base-devel linux-headers
```

## Build

```bash
git clone https://github.com/<your-username>/MemorySentinel.git
cd MemorySentinel
make all
```

Expected outputs:

| File | Description |
|------|-------------|
| `kernel/sentinel_mod.ko` | Kernel module |
| `sentinel-daemon` | Monitoring daemon |
| `sentinel` | CLI tool |

If kernel headers are unavailable (e.g., CI environment):
```bash
make userspace   # builds daemon and CLI only
```

## Load the Kernel Module

```bash
sudo insmod kernel/sentinel_mod.ko
# Verify
cat /proc/sentinel
dmesg | tail -5   # should show: sentinel: module loaded
```

## Start the Daemon

```bash
sudo ./sentinel-daemon &
```

The daemon creates `/var/run/sentinel.sock` with `0666` permissions. The CLI can then be run as a regular user.

## Verify Installation

```bash
./sentinel status
```

Expected: a colour-coded memory health summary.

## Automated Setup

```bash
chmod +x scripts/run.sh
sudo bash scripts/run.sh
```

This script builds, loads the module, and starts the daemon in one step.

## Uninstall / Cleanup

```bash
chmod +x scripts/cleanup.sh
sudo bash scripts/cleanup.sh
```

Or manually:
```bash
./sentinel stop
sudo rmmod sentinel_mod
make clean
```

## Secure Boot Note

If Secure Boot is enabled, the kernel module must be signed with a trusted key before loading. Refer to your distribution's documentation for module signing.
