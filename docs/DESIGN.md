# MemorySentinel — Technical Design

## Module Design: `sentinel_mod.c`

The LKM registers a single `/proc/sentinel` entry using `proc_create()` with a `proc_ops` struct pointing to `single_open()` + `sentinel_show()`.

`sentinel_show()` emits three sections in a fixed text format:

```
MEMINFO: <total_kb> <free_kb> <available_kb> <swap_total_kb> <swap_free_kb>
ZONE: <name> <free_pages> <o0> <o1> ... <o10>
PROCESS: <pid> <rss_pages> <oom_score_adj> <name>
```

Kernel APIs used per section:

| Section | API |
|---------|-----|
| MEMINFO | `si_meminfo()` |
| ZONE | `for_each_online_pgdat()` → `zone->free_area[order].nr_free` |
| PROCESS | `for_each_process()` + `rcu_read_lock()` + `get_mm_rss()` |

## Daemon Design: `sentinel_daemon.cpp`

### Data Structures

```cpp
struct ProcSample { uint64_t rss_kb; time_t ts; };

struct ProcState {
    char name[64];
    ProcSample ring[16];   // ring buffer
    int  head;             // next write index
    int  count;            // samples collected
    bool leak_suspect;
};

unordered_map<pid_t, ProcState> g_procs;
```

### Poll Loop (every 5 s)

1. Read `/proc/sentinel` → update `g_procs` ring buffers
2. Read `/proc/meminfo` → update `oom_info_t`
3. Read `/proc/buddyinfo` → update `frag_info_t[]`
4. Read `/proc/slabinfo` → update `slab_entry_t[]` (top 20 by `total_kb`)
5. For each process with `count >= 6`: check last 6 samples for monotonic growth → set `leak_suspect`

### Leak Detection

```
bool is_leak(ProcState& s):
    for i in [0..4]:
        idx_a = (s.head - 6 + i + 16) % 16
        idx_b = (s.head - 6 + i + 1 + 16) % 16
        if s.ring[idx_a].rss_kb >= s.ring[idx_b].rss_kb: return false
    return true
```

### IPC Server

- Unix domain socket at `/var/run/sentinel.sock` (SOCK_STREAM)
- Each connection: read `sentinel_req_t` → write `sentinel_resp_hdr_t` + payload records
- Payload type per command:

| Command | Payload type | Count |
|---------|-------------|-------|
| CMD_STATUS | `oom_info_t` | 1 |
| CMD_LEAKS | `proc_mem_t[]` | leak suspects only |
| CMD_FRAG | `frag_info_t[]` | zones |
| CMD_OOM | `oom_info_t` | 1 |
| CMD_SLAB | `slab_entry_t[]` | up to 20 |
| CMD_WATCH | `proc_mem_t[]` | top 25 by RSS |

## CLI Design: `sentinel_cli.cpp`

1. Parse `argv[1]` → map to `cmd_t`
2. Connect to `/var/run/sentinel.sock`
3. Send `sentinel_req_t`
4. Read `sentinel_resp_hdr_t` → read `count` payload records
5. Print with ANSI colour based on `severity`

## Protocol: `sentinel_proto.h`

All structs are `__attribute__((packed))`. Wire format is little-endian native (same host). See `include/sentinel_proto.h` for full struct definitions.

## OOM Risk Formula

```
used = (MemTotal + SwapTotal) - (MemAvailable + SwapFree)
capacity = MemTotal + SwapTotal
oom_risk_pct = (used * 100) / capacity
```

Severity thresholds: `< 70` → OK, `70–89` → WARN, `≥ 90` → CRITICAL.

## Fragmentation Score Formula

```
small_free = sum(zone->free_area[0..3].nr_free)
total_free = sum(zone->free_area[0..10].nr_free)
frag_score = (small_free * 100) / total_free   // if total_free > 0
```
