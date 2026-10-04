# MemorySentinel — Troubleshooting

## `Cannot connect to sentinel daemon`

The daemon is not running or the socket was not created.

```bash
ls /var/run/sentinel.sock          # check if socket exists
sudo ./sentinel-daemon &           # start the daemon
```

---

## `insmod: ERROR: could not insert module sentinel_mod.ko`

Kernel headers do not match the running kernel, or the module was built for a different kernel.

```bash
uname -r                                        # running kernel version
ls /lib/modules/$(uname -r)/build              # headers must exist here
make clean && make all                          # rebuild against current headers
sudo insmod kernel/sentinel_mod.ko
```

---

## `/proc/sentinel` not found

The kernel module is not loaded.

```bash
sudo insmod kernel/sentinel_mod.ko
dmesg | tail -5                    # should show: sentinel: module loaded
cat /proc/sentinel
```

---

## `Permission denied on /var/run/sentinel.sock`

The daemon was not started with `sudo`. The socket requires root to create.

```bash
sudo ./sentinel-daemon &
# Socket is created with 0666; CLI can then run as a regular user
```

---

## `make userspace` fails with C++17 errors

GCC version is too old.

```bash
g++ --version                      # must be 9+
sudo apt install g++-9             # Ubuntu/Debian
sudo dnf install gcc-c++           # Fedora/RHEL
```

---

## `make all` fails: `No rule to make target modules`

Kernel headers are missing.

```bash
sudo apt install linux-headers-$(uname -r)     # Ubuntu/Debian
sudo dnf install kernel-devel                  # Fedora/RHEL
```

---

## Daemon starts but `./sentinel leaks` shows no suspects

The daemon needs at least 6 poll cycles (~30 seconds) to detect leaks. Wait and retry.

```bash
sleep 35 && ./sentinel leaks
```

---

## `dmesg` shows kernel warnings after `rmmod`

A reference count was not released. Ensure the daemon is stopped before unloading:

```bash
./sentinel stop
sleep 2
sudo rmmod sentinel_mod
dmesg | tail -10
```

---

## Secure Boot: `Required key not available`

The kernel module must be signed. Refer to your distribution's module signing documentation, or disable Secure Boot in BIOS/UEFI for development use.

---

## High CPU usage from `sentinel-daemon`

Verify the poll interval is 5 seconds (default). If the daemon is looping unexpectedly, check `dmesg` for errors reading `/proc/sentinel` and restart:

```bash
./sentinel stop
sudo ./sentinel-daemon &
```
