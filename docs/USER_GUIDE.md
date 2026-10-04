# MemorySentinel — User Guide

## Starting the System

```bash
sudo bash scripts/run.sh
```

Or manually:
```bash
sudo insmod kernel/sentinel_mod.ko
sudo ./sentinel-daemon &
```

---

## CLI Reference

```
./sentinel <command>
```

| Command | Description |
|---------|-------------|
| `status` | Overall memory health summary |
| `leaks` | Processes suspected of leaking memory |
| `frag` | Buddy allocator fragmentation per zone |
| `oom` | OOM risk percentage and top victim |
| `slab` | Top 20 kernel slab cache consumers |
| `watch` | Live top-25 processes by RSS |
| `stop` | Gracefully stop the daemon |

---

## Command Details

### `status`
Shows system-wide memory totals, OOM risk %, and a severity indicator.

```
Severity : WARNING
Summary  : OOM risk: 74% | Leaks: 2 | Zones: 3
  MemTotal    :     7812 MB
  MemFree     :      512 MB
  MemAvailable:     1100 MB
  OOM Risk    : 74%
```

Severity colours:
- Green `OK` — OOM risk < 70%
- Yellow `WARNING` — OOM risk 70–89%
- Red `CRITICAL` — OOM risk ≥ 90%

### `leaks`
Lists processes with 6 consecutive RSS growth samples.

```
Leak Suspects (1 process(es))
PID      Name                   RSS(kB)    PSS(kB) Growth(kB)
1842     my_app                   512000     510000       4096
```

A process appears here only after the daemon has collected at least 6 samples (~30 seconds after startup).

### `frag`
Shows fragmentation score per memory zone. Higher score = more fragmented.

```
Zone       Score   FreePg   Orders 0..10
DMA          12%     1024   [128,64,32,16,8,4,2,1,0,0,0]
Normal       71%   131072   [65536,32768,...]
```

Score > 70% indicates significant fragmentation that may cause large allocation failures.

### `oom`
Shows OOM risk and the process most likely to be killed by the kernel OOM killer.

```
OOM Risk : 74%  (WARNING)
Top OOM Candidate: PID 1842  my_app  score=650
```

### `slab`
Lists the top 20 kernel slab caches by total memory consumed.

```
Name                  ActiveObjs  TotalObjs  ObjSize  TotalKB
dentry                    120000     125000      192   24000
inode_cache                45000      48000      608   29280
```

### `watch`
Refreshes every 5 seconds showing the top 25 processes by RSS.

```
PID      Name          RSS(kB)   PSS(kB)  Heap(kB)  Leak?
1842     my_app         512000    510000    480000    YES
 934     firefox        320000    290000    260000     no
```

Press `Ctrl+C` to exit watch mode.

---

## Interpreting Results

| Signal | Meaning | Action |
|--------|---------|--------|
| Leak suspect | Process RSS grows every sample | Investigate with `valgrind` or `heaptrack` |
| Frag score > 70% | Memory heavily fragmented | Consider `echo 1 > /proc/sys/vm/compact_memory` |
| OOM risk > 90% | System near OOM | Kill or restart high-RSS processes |
| Slab cache > 1 GB | Kernel cache leak | Check for inode/dentry accumulation |

---

## Stopping

```bash
./sentinel stop        # graceful daemon shutdown
sudo rmmod sentinel_mod
```
