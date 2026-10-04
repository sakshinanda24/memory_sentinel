/* sentinel_proto.h - Shared protocol between daemon and CLI */
#ifndef SENTINEL_PROTO_H
#define SENTINEL_PROTO_H

#include <stdint.h>

#define SENTINEL_SOCK_PATH  "/var/run/sentinel.sock"
#define SENTINEL_PROC_PATH  "/proc/sentinel"
#define SENTINEL_VERSION    "1.0.0"

/* Command opcodes from CLI -> Daemon */
typedef enum {
    CMD_STATUS      = 1,   /* Overall memory health summary */
    CMD_LEAKS       = 2,   /* Per-process leak suspects */
    CMD_FRAG        = 3,   /* Buddy allocator fragmentation */
    CMD_OOM         = 4,   /* OOM risk assessment */
    CMD_SLAB        = 5,   /* Kernel slab cache stats */
    CMD_WATCH       = 6,   /* Start continuous watch (pid) */
    CMD_STOP        = 7,   /* Stop daemon */
} cmd_t;

/* Severity levels */
typedef enum {
    SEV_OK       = 0,
    SEV_WARN     = 1,
    SEV_CRITICAL = 2,
} severity_t;

/* Request packet */
typedef struct {
    uint32_t cmd;
    int32_t  pid;       /* used by CMD_WATCH; -1 = all */
    uint32_t flags;
} __attribute__((packed)) sentinel_req_t;

/* Per-process memory snapshot */
typedef struct {
    int32_t  pid;
    char     name[64];
    uint64_t rss_kb;
    uint64_t pss_kb;
    uint64_t heap_kb;
    uint64_t growth_kb;   /* RSS delta since last sample */
    uint8_t  leak_suspect; /* 1 if monotonically growing */
} __attribute__((packed)) proc_mem_t;

/* Buddy allocator fragmentation info (per zone) */
typedef struct {
    char     zone[32];
    uint32_t free_pages;
    uint32_t frag_score;  /* 0-100, higher = more fragmented */
    uint32_t order_counts[11]; /* free blocks per order 0..10 */
} __attribute__((packed)) frag_info_t;

/* Slab cache entry */
typedef struct {
    char     name[32];
    uint64_t active_objs;
    uint64_t total_objs;
    uint32_t obj_size;
    uint64_t total_kb;
} __attribute__((packed)) slab_entry_t;

/* OOM risk snapshot */
typedef struct {
    uint64_t total_kb;
    uint64_t free_kb;
    uint64_t available_kb;
    uint64_t swap_total_kb;
    uint64_t swap_free_kb;
    uint32_t oom_risk_pct;   /* 0-100 */
    severity_t severity;
    int32_t  top_oom_pid;
    char     top_oom_name[64];
    int32_t  top_oom_score;
} __attribute__((packed)) oom_info_t;

/* Generic response header */
typedef struct {
    uint32_t   status;      /* 0 = ok, non-zero = error */
    uint32_t   count;       /* number of records following */
    severity_t severity;
    char       message[128];
} __attribute__((packed)) sentinel_resp_hdr_t;

#endif /* SENTINEL_PROTO_H */
