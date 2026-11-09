#!/bin/sh
# Host-Test der JSON-Ausgabe (Standardkonfiguration) + Validierung mit Python-json.
set -e
cd "$(dirname "$0")"
if command -v g++ >/dev/null 2>&1; then CXX="g++"; else CXX="${ZIGPY:-python3} -m ziglang c++"; fi
rm -rf /tmp/cyd_json
$CXX -std=c++14 -O1 -Wall -Wextra -fsanitize=undefined -o /tmp/cyd_json_test host_json_test.cpp
/tmp/cyd_json_test /tmp/cyd_json
python3 ../tools/validate_json.py /tmp/cyd_json
