#!/bin/sh
# Host-Test bauen und ausfuehren (6 Konfigurationen). Nimmt g++ falls vorhanden, sonst zig c++ (pip install ziglang).
set -e
cd "$(dirname "$0")"
if command -v g++ >/dev/null 2>&1; then CXX="g++"; else CXX="${ZIGPY:-python3} -m ziglang c++"; fi
for CFG in "" "-DENABLE_LD2420=1" "-DSHOW_RANGE_ONLY_RINGS=1" "-DENABLE_LD2420=1 -DSHOW_RANGE_ONLY_RINGS=1" "-DPOS_SMOOTH_ALPHA=0.6f" "-DSPAWN_CONFIRM_FRAMES=1"; do
  echo "=== Konfiguration: ${CFG:-Standard (LD2450 + LD2410C)} ==="
  $CXX -std=c++14 -O1 -Wall -Wextra -fsanitize=undefined $CFG -o /tmp/tracker_host_test host_test.cpp
  /tmp/tracker_host_test
done
