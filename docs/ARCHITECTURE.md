# MemorySentinel Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        USER SPACE                               │
│                                                                 │
│   ┌──────────────────┐   Unix Socket    ┌──────────────────┐   │
│   │  sentinel (CLI)  │ ◄──────────────► │ sentinel-daemon  │   │
│   │  sentinel_cli.cpp│                  │sentinel_daemon.cpp│  │
│   └──────────────────┘                  └────────┬─────────┘   │
│                                                  │             │
│                                    ┌─────────────▼──────────┐  │
│                                    │   /proc filesystem     │  │
│                                    │  /proc/meminfo         │  │
│                                    │  /proc/*/smaps_rollup  │  │
│                                    │  /proc/buddyinfo       │  │
│                                    │  /proc/slabinfo        │  │
│                                    │  /proc/sentinel  ◄──┐  │  │
│                                    └────────────────────┐─┘  │  │
└────────────────────────────────────────────────────────┼──────┘
                                                         │
┌────────────────────────────────────────────────────────┼──────┐
│                       KERNEL SPACE                     │      │
│                                                        │      │
│   ┌─────────────────────────────────────────────────┐  │      │
│   │           sentinel_mod.ko (LKM)                 │──┘      │
│   │                                                 │         │
│   │  ┌──────────────┐  ┌──────────────────────────┐│         │
│   │  │  proc_ops    │  │  seq_file interface       ││         │
│   │  │  sentinel_   │  │  sentinel_show()          ││         │
│   │  │  open/read   │  │                           ││         │
│   │  └──────────────┘  └──────────────────────────┘│         │
│   │                                                 │         │
│   │  Kernel Subsystems Accessed:                    │         │
│   │  ┌────────────┐ ┌──────────┐ ┌───────────────┐ │         │
│   │  │ mm/mmzone  │ │  slab    │ │  task_struct  │ │         │
│   │  │ (buddy     │ │ (kmem_   │ │  (oom_score,  │ │         │
│   │  │  allocator)│ │  cache)  │ │   mm_struct)  │ │         │
│   │  └────────────┘ └──────────┘ └───────────────┘ │         │
│   └─────────────────────────────────────────────────┘         │
└───────────────────────────────────────────────────────────────┘
```

## Component Descriptions

### 1. Kernel Module (`kernel/sentinel_mod.c`)
A Linux Loadable Kernel Module (LKM) that creates `/proc/sentinel`.

**Kernel Driver Concepts Used:**
| Concept | Usage |
|---------|-------|
| `proc_fs` / `seq_file` | Creates `/proc/sentinel` read interface |
| `mm_zone` / `free_area` | Traverses buddy allocator per-zone free lists |
| `si_meminfo()` | Reads system-wide memory statistics |
| `for_each_online_pgdat` | Iterates NUMA nodes and memory zones |
| `for_each_process` / `rcu_read_lock` | Safe process list traversal |
| `get_mm_rss()` | Reads per-process RSS from `mm_struct` |
| `task->signal->oom_score_adj` | Reads OOM adjustment per process |
| `module_init` / `module_exit` | Standard LKM lifecycle hooks |

### 2. Daemon (`userspace/sentinel_daemon.cpp`)
A C++ background process that:
- Polls `/proc` files every 5 seconds
- Maintains per-process RSS history (ring buffer, 16 samples)
- Detects leaks: 6 consecutive monotonic RSS growth samples
- Computes OOM risk as `(used / total_capacity) * 100`
- Serves CLI queries over a Unix domain socket

### 3. CLI (`userspace/sentinel_cli.cpp`)
A C++ command-line tool that connects to the daemon socket and displays formatted, colour-coded output.

### 4. Shared Protocol (`include/sentinel_proto.h`)
Packed C structs defining the binary protocol between daemon and CLI.

## Data Flow

```
Kernel Module                  Daemon                    CLI
     │                           │                        │
     │  /proc/sentinel           │                        │
     │◄──────────────────────────│                        │
     │                           │  /proc/meminfo         │
     │                           │  /proc/*/smaps_rollup  │
     │                           │  /proc/buddyinfo       │
     │                           │  /proc/slabinfo        │
     │                           │                        │
     │                           │◄── sentinel_req_t ─────│
     │                           │─── sentinel_resp_hdr_t+│
     │                           │    payload ───────────►│
```

## Leak Detection Algorithm

```
For each process, maintain a ring buffer of 16 RSS samples.
After LEAK_WINDOW (6) samples are collected:
  If rss[i] < rss[i+1] for all i in last 6 samples → LEAK SUSPECT
```

## OOM Risk Formula

```
oom_risk_pct = ((MemTotal + SwapTotal - MemAvailable - SwapFree)
                / (MemTotal + SwapTotal)) * 100

Severity:
  0-69%  → OK       (green)
  70-89% → WARNING  (yellow)
  90%+   → CRITICAL (red)
```

## Fragmentation Score

```
frag_score = (free_pages_in_orders_0_to_3 / total_free_pages) * 100

Higher score = more fragmented (memory split into small chunks)
```
