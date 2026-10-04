# MemorySentinel — Testing

## Test Environment Requirements

- Linux VM or bare-metal (kernel 5.x / 6.x)
- Kernel headers installed
- Root access
- `valgrind` (optional, for leak simulation)

## Build Verification

```bash
make clean && make all
```

Expected: no compiler errors or warnings; all three outputs present.

## Kernel Module Tests

### Load / Unload

```bash
sudo insmod kernel/sentinel_mod.ko
lsmod | grep sentinel_mod          # must appear
cat /proc/sentinel                 # must print MEMINFO/ZONE/PROCESS lines
sudo rmmod sentinel_mod
lsmod | grep sentinel_mod          # must be absent
dmesg | tail -10                   # check for clean init/exit messages
```

### /proc/sentinel Format

Verify output contains expected section headers:
```bash
cat /proc/sentinel | grep -E '^(MEMINFO|ZONE|PROCESS):'
```

## Daemon Tests

### Startup and Socket Creation

```bash
sudo ./sentinel-daemon &
ls -la /var/run/sentinel.sock      # must exist with 0666 permissions
```

### IPC Connectivity

```bash
./sentinel status                  # must return a valid response
```

### Graceful Shutdown

```bash
./sentinel stop
ls /var/run/sentinel.sock          # must be removed
```

## CLI Command Tests

Run each command and verify non-empty, correctly formatted output:

```bash
./sentinel status
./sentinel leaks
./sentinel frag
./sentinel oom
./sentinel slab
./sentinel watch
```

## Leak Detection Test

Simulate a leaking process using a simple C program:

```c
// leak_sim.c
#include <stdlib.h>
#include <unistd.h>
int main() {
    while (1) { malloc(1024 * 1024); sleep(5); }
}
```

```bash
gcc -o leak_sim leak_sim.c
./leak_sim &
# Wait ~35 seconds (7 poll cycles)
./sentinel leaks   # leak_sim should appear as a suspect
kill %1
```

## Fragmentation Score Test

```bash
./sentinel frag
# Verify: score is 0–100, zone names match /proc/buddyinfo
cat /proc/buddyinfo
```

## OOM Risk Test

```bash
./sentinel oom
# Verify: oom_risk_pct is 0–100, top_oom_pid is a valid PID
```

## Stress / Regression

After all tests:
```bash
sudo rmmod sentinel_mod
make clean
```

Verify `dmesg` shows no kernel warnings or oops related to `sentinel`.

## Known Limitations

- Leak detection requires ≥ 30 seconds of daemon uptime (6 × 5 s poll interval)
- `CMD_WATCH` is a single snapshot; continuous refresh is handled by the CLI loop
- Kernel module tests require a real Linux kernel; they cannot run in containers without `--privileged`
