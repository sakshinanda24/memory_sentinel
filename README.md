# MemorySentinel

**Kernel/User-Space Memory Leak, Fragmentation & OOM Diagnosis Platform**

MemorySentinel is a Linux diagnostic tool written in C/C++ that detects abnormal memory usage, identifies memory leaks and buddy allocator fragmentation, and assesses Out-Of-Memory (OOM) risk — using a custom Linux Kernel Module (LKM) paired with a userspace daemon and CLI.

---

## Features

| Feature | Description |
|---------|-------------|
| Kernel Module | LKM exposes `/proc/sentinel` with buddy, vmalloc, and OOM data |
| Leak Detection | Per-process RSS ring-buffer tracking; flags 6 consecutive growth samples |
| Fragmentation | Buddy allocator free-list analysis per memory zone with score 0–100 |
| OOM Assessment | Risk percentage + top OOM candidate identification via `oom_score` |
| Slab Analysis | Top 20 kernel slab cache consumers by memory from `/proc/slabinfo` |
| Live Watch | Top-25 processes by RSS with leak flag |
| Daemon IPC | Background polling over Unix domain socket |
| Colour CLI | Severity-coded terminal output (green/yellow/red) |

---

## Project Structure

```
MemorySentinel/
├── kernel/
│   ├── sentinel_mod.c      # Linux Kernel Module (LKM)
│   └── Makefile            # Kernel build rules
├── userspace/
│   ├── sentinel_daemon.cpp # Monitoring daemon
│   └── sentinel_cli.cpp    # CLI tool
├── include/
│   └── sentinel_proto.h    # Shared binary protocol (packed structs)
├── scripts/
│   ├── run.sh              # Build + load module + start daemon
│   └── cleanup.sh          # Stop daemon + unload module + clean
├── docs/
│   └── ARCHITECTURE.md     # Architecture diagram and algorithm docs
├── .gitignore
├── Makefile                # Top-level build
└── README.md
```

---

## Prerequisites

| Requirement | Notes |
|-------------|-------|
| Linux kernel 5.x+ | Tested on 5.15 and 6.x |
| GCC / G++ 9+ | C++17 required |
| Linux kernel headers | Must match the running kernel |
| make | Standard build tool |
| Root / sudo | Required for LKM load and daemon socket |

**Ubuntu / Debian:**
```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

**Fedora / RHEL:**
```bash
sudo dnf install kernel-devel kernel-headers gcc-c++ make
```

---

## Build

```bash
git clone https://github.com/<your-username>/MemorySentinel.git
cd MemorySentinel

# Build everything (kernel module + userspace)
make all

# Or build only userspace if kernel headers are unavailable
make userspace
```

Build outputs:
- `kernel/sentinel_mod.ko` — kernel module
- `sentinel-daemon` — userspace daemon
- `sentinel` — CLI tool

---

## Quick Start

```bash
# One-shot: build, load module, start daemon, show status
chmod +x scripts/run.sh
sudo bash scripts/run.sh
```

**Manual steps:**

```bash
# 1. Load the kernel module
sudo insmod kernel/sentinel_mod.ko

# Verify /proc/sentinel is available
cat /proc/sentinel

# 2. Start the daemon in the background
sudo ./sentinel-daemon &

# 3. Run CLI commands
./sentinel status
./sentinel leaks
./sentinel frag
./sentinel oom
./sentinel slab
./sentinel watch
```

---

## CLI Commands

```
./sentinel <command>

  status   Overall memory health summary
  leaks    Per-process memory leak suspects
  frag     Buddy allocator fragmentation per zone
  oom      OOM risk assessment + top victim
  slab     Top 20 kernel slab cache consumers
  watch    Live top-25 processes by RSS with leak flag
  stop     Gracefully stop the daemon
```

### Example Output

**`./sentinel status`**
```
╔══════════════════════════════════════════╗
║       MemorySentinel v1.0.0             ║
╚══════════════════════════════════════════╝
Severity : OK
Summary  : OOM risk: 42% | Leaks: 1 | Zones: 3

  MemTotal    :     7812 MB
  MemFree     :     1024 MB
  MemAvailable:     3200 MB
  SwapTotal   :     2048 MB
  SwapFree    :     2048 MB
  OOM Risk    : 42%
```

**`./sentinel leaks`**
```
Leak Suspects (1 process(es))
PID      Name                   RSS(kB)    PSS(kB) Growth(kB)
-------- -------------------- ---------- ---------- ----------
1842     my_app                   512000     510000       4096
```

**`./sentinel frag`**
```
Buddy Allocator Fragmentation (3 zone(s))
Zone                    Score     FreePg  Orders 0..10
----                    -----     ------  -------------------------------------------
DMA                       12%       1024  [128,64,32,16,8,4,2,1,0,0,0]
DMA32                     38%      32768  [4096,2048,1024,512,256,128,64,32,16,8,4]
Normal                    71%     131072  [65536,32768,16384,8192,4096,2048,1024,512,256,128,64]
```

**`./sentinel watch`**
```
Live Process Memory (Top 25 by RSS)
PID      Name                   RSS(kB)    PSS(kB)   Heap(kB)  Leak?
-------- -------------------- ---------- ---------- ---------- ------
1842     my_app                   512000     510000     480000    YES
 934     firefox                  320000     290000     260000     no
```

---

## Kernel Module Details

The LKM (`sentinel_mod.c`) uses the following Linux kernel driver concepts:

| Kernel API | Purpose |
|------------|---------|
| `proc_create()` / `proc_ops` | Register `/proc/sentinel` read interface |
| `seq_file` / `single_open()` | Efficient buffered sequential read |
| `for_each_online_pgdat()` | Iterate NUMA nodes |
| `zone->free_area[order].nr_free` | Read buddy allocator free lists per order |
| `si_meminfo()` | System-wide memory statistics |
| `for_each_process()` + `rcu_read_lock()` | Safe process list traversal |
| `get_mm_rss()` | Per-process RSS from `mm_struct` |
| `task->signal->oom_score_adj` | Per-process OOM adjustment score |
| `LINUX_VERSION_CODE` | Conditional vmalloc API for kernel 5.8+ |
| `module_init` / `module_exit` | Standard LKM lifecycle hooks |

---

## Troubleshooting

**`Cannot connect to sentinel daemon`**
The daemon is not running. Start it with:
```bash
sudo ./sentinel-daemon &
```

**`insmod: ERROR: could not insert module`**
Kernel headers may not match the running kernel. Verify with:
```bash
uname -r
ls /lib/modules/$(uname -r)/build
```

**`/proc/sentinel` not found**
The kernel module is not loaded. Load it with:
```bash
sudo insmod kernel/sentinel_mod.ko
dmesg | tail -5   # check for sentinel: module loaded
```

**Permission denied on `/var/run/sentinel.sock`**
The daemon must be started with `sudo`. The socket is created with `0666` permissions so the CLI can be run as a regular user after the daemon starts.

**`make userspace` fails with C++17 errors**
Ensure GCC 9+ is installed:
```bash
g++ --version
sudo apt install g++-9   # Ubuntu
```

---

## Cleanup

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

---

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full architecture diagram, data flow, and algorithm descriptions.

---

## License

GPL-2.0 (kernel module) / MIT (userspace)
