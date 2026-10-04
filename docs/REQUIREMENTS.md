# MemorySentinel — Requirements

## Functional Requirements

| ID | Requirement |
|----|-------------|
| FR-01 | The kernel module SHALL expose `/proc/sentinel` with buddy allocator, vmalloc, and OOM data |
| FR-02 | The daemon SHALL poll `/proc` sources every 5 seconds |
| FR-03 | The daemon SHALL maintain a 16-sample RSS ring buffer per process |
| FR-04 | The system SHALL flag a process as a leak suspect after 6 consecutive monotonic RSS growth samples |
| FR-05 | The system SHALL compute a fragmentation score (0–100) per buddy allocator zone |
| FR-06 | The system SHALL compute OOM risk as a percentage of used memory capacity |
| FR-07 | The system SHALL identify the top OOM candidate via `/proc/<pid>/oom_score` |
| FR-08 | The system SHALL report the top 20 kernel slab cache consumers by memory |
| FR-09 | The CLI SHALL support commands: `status`, `leaks`, `frag`, `oom`, `slab`, `watch`, `stop` |
| FR-10 | The CLI SHALL display severity-coded output (green/yellow/red) |
| FR-11 | The daemon SHALL communicate with the CLI over a Unix domain socket |
| FR-12 | The `watch` command SHALL display the top 25 processes by RSS with leak flags |

## Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| NFR-01 | The kernel module SHALL be compatible with Linux kernel 5.x and 6.x |
| NFR-02 | Userspace components SHALL be written in C++17 |
| NFR-03 | The daemon SHALL NOT consume more than 2% CPU during idle polling |
| NFR-04 | The Unix socket SHALL be created with `0666` permissions to allow non-root CLI access |
| NFR-05 | The system SHALL require root privileges only for module load and daemon startup |
| NFR-06 | All kernel data structures SHALL be accessed with appropriate locking (`rcu_read_lock`) |
| NFR-07 | The binary protocol structs SHALL use `__attribute__((packed))` for portability |
| NFR-08 | Build SHALL succeed with GCC/G++ 9+ using `make all` |
