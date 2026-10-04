#!/bin/bash
# scripts/run.sh - Build, load kernel module, and start daemon

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

echo "[*] Building MemorySentinel..."
make all

echo "[*] Loading kernel module..."
sudo insmod kernel/sentinel_mod.ko 2>/dev/null || true
lsmod | grep sentinel_mod && echo "[+] Kernel module loaded" || echo "[!] Module load failed (non-fatal)"

echo "[*] Starting daemon..."
sudo ./sentinel-daemon &
DAEMON_PID=$!
echo "[+] Daemon PID: $DAEMON_PID"
sleep 1

echo "[*] Quick status check..."
./sentinel status

echo ""
echo "Usage:"
echo "  ./sentinel status"
echo "  ./sentinel leaks"
echo "  ./sentinel frag"
echo "  ./sentinel oom"
echo "  ./sentinel slab"
echo "  ./sentinel stop"
