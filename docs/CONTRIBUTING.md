# MemorySentinel — Contributing

## Getting Started

1. Fork the repository and clone your fork
2. Create a feature branch: `git checkout -b feature/your-feature`
3. Make changes, commit, and push
4. Open a Pull Request against `main`

## Code Style

- Kernel module (`sentinel_mod.c`): follow Linux kernel coding style (`checkpatch.pl`)
- Userspace (`*.cpp`): C++17, 4-space indent, no trailing whitespace
- Keep functions small and single-purpose
- No commented-out code in commits

## Commit Messages

```
component: short imperative summary (≤72 chars)

Optional body explaining why, not what.
```

Examples:
- `kernel: add vmalloc stats to /proc/sentinel`
- `daemon: fix ring buffer wraparound for short-lived processes`
- `cli: add colour support for watch command`

## Adding a New CLI Command

1. Add a new `cmd_t` opcode in `include/sentinel_proto.h`
2. Add a payload struct if needed (use `__attribute__((packed))`)
3. Handle the opcode in `sentinel_daemon.cpp` (collect data + serialize)
4. Add the display function in `sentinel_cli.cpp`
5. Update `docs/API.md` and `docs/USER_GUIDE.md`

## Kernel Module Changes

- Test load/unload at least 10 times to check for reference leaks
- Run `sudo dmesg -w` while testing to catch kernel warnings
- Verify with `make -C /lib/modules/$(uname -r)/build M=$(pwd) checkpatch`

## Pull Request Checklist

- [ ] `make clean && make all` succeeds without warnings
- [ ] Module loads and unloads cleanly (`dmesg` shows no errors)
- [ ] All CLI commands return valid output
- [ ] `docs/` updated if behaviour or API changed
- [ ] No hardcoded paths other than `/var/run/sentinel.sock` and `/proc/sentinel`

## Reporting Issues

Open a GitHub Issue with:
- Kernel version (`uname -r`)
- Distribution and GCC version
- Full error message and relevant `dmesg` output
