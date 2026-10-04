# MemorySentinel — IPC Protocol API

The daemon and CLI communicate over a Unix domain socket (`/var/run/sentinel.sock`) using a binary protocol defined in `include/sentinel_proto.h`.

## Transport

- Socket type: `SOCK_STREAM`
- Path: `/var/run/sentinel.sock`
- Permissions: `0666` (daemon creates; CLI connects as any user)

## Request

The CLI sends exactly one `sentinel_req_t` per connection:

```c
typedef struct {
    uint32_t cmd;    // cmd_t opcode
    int32_t  pid;    // -1 = all processes (used by CMD_WATCH)
    uint32_t flags;  // reserved, set to 0
} __attribute__((packed)) sentinel_req_t;
```

### Command Opcodes

| Value | Name | Description |
|-------|------|-------------|
| 1 | `CMD_STATUS` | Overall memory health |
| 2 | `CMD_LEAKS` | Leak suspect processes |
| 3 | `CMD_FRAG` | Zone fragmentation |
| 4 | `CMD_OOM` | OOM risk info |
| 5 | `CMD_SLAB` | Slab cache stats |
| 6 | `CMD_WATCH` | Top-25 processes by RSS |
| 7 | `CMD_STOP` | Stop the daemon |

## Response

The daemon always responds with a `sentinel_resp_hdr_t` followed by `count` payload records:

```c
typedef struct {
    uint32_t   status;       // 0 = success, non-zero = error
    uint32_t   count;        // number of payload records
    severity_t severity;     // SEV_OK=0, SEV_WARN=1, SEV_CRITICAL=2
    char       message[128]; // human-readable summary
} __attribute__((packed)) sentinel_resp_hdr_t;
```

## Payload Types by Command

### CMD_STATUS / CMD_OOM → `oom_info_t` (count = 1)

```c
typedef struct {
    uint64_t   total_kb;
    uint64_t   free_kb;
    uint64_t   available_kb;
    uint64_t   swap_total_kb;
    uint64_t   swap_free_kb;
    uint32_t   oom_risk_pct;    // 0–100
    severity_t severity;
    int32_t    top_oom_pid;
    char       top_oom_name[64];
    int32_t    top_oom_score;
} __attribute__((packed)) oom_info_t;
```

### CMD_LEAKS / CMD_WATCH → `proc_mem_t[]` (count = N processes)

```c
typedef struct {
    int32_t  pid;
    char     name[64];
    uint64_t rss_kb;
    uint64_t pss_kb;
    uint64_t heap_kb;
    uint64_t growth_kb;      // RSS delta since last sample
    uint8_t  leak_suspect;   // 1 = leaking
} __attribute__((packed)) proc_mem_t;
```

### CMD_FRAG → `frag_info_t[]` (count = number of zones)

```c
typedef struct {
    char     zone[32];
    uint32_t free_pages;
    uint32_t frag_score;          // 0–100
    uint32_t order_counts[11];    // free blocks per order 0..10
} __attribute__((packed)) frag_info_t;
```

### CMD_SLAB → `slab_entry_t[]` (count ≤ 20)

```c
typedef struct {
    char     name[32];
    uint64_t active_objs;
    uint64_t total_objs;
    uint32_t obj_size;
    uint64_t total_kb;
} __attribute__((packed)) slab_entry_t;
```

### CMD_STOP → no payload (count = 0)

## Error Handling

If `sentinel_resp_hdr_t.status != 0`, the `message` field contains the error description and no payload records follow.
