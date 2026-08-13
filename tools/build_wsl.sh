#!/usr/bin/env bash
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make -f "$ROOT/Makefile" clean
make -f "$ROOT/Makefile" > /tmp/gcc-rt-build.log 2>&1
rc=$?
echo "EXIT:${rc}"
grep -E "error:|undefined reference|Error [0-9]|Assembler messages" /tmp/gcc-rt-build.log | head -n 40 || true
tail -n 15 /tmp/gcc-rt-build.log
ls -la build/User.* 2>/dev/null || true
exit "${rc}"
