#!/bin/bash
# scripts/cleanup.sh - Stop daemon, unload kernel module, clean build artifacts

set -e

echo "[*] Stopping daemon (if running)..."
./sentinel stop 2>/dev/null || true
sleep 1

echo "[*] Unloading kernel module..."
sudo rmmod sentinel_mod 2>/dev/null && echo "[+] Module unloaded" || echo "[!] Module not loaded"

echo "[*] Cleaning build artifacts..."
make clean

echo "[+] Cleanup complete."
