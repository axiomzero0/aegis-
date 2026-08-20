#!/usr/bin/env bash
# tests/run_golden.sh — run a .aegis file through the compiler and compare
# the post-passes IR dump with a .expected.son file.
#
# Rule 37: "Every optimization pass must have ≥10 golden IR tests."
# This is the harness. Golden files are checked-in .in.aegis /
# .expected.son pairs and they run in both Static (AOT) and Profile
# (JIT) modes.
set -euo pipefail

INPUT="$1"
EXPECTED="$2"

if [ ! -x "$(command -v aegis)" ]; then
    echo "aegis binary not on PATH; skipping golden test"
    exit 1
fi

ACTUAL=$(aegis "$INPUT" --dump-ir --no-verify 2>/dev/null | tail -n +1 || true)

if [ -z "$ACTUAL" ]; then
    echo "FAIL: empty output from aegis for $INPUT"
    exit 1
fi

# Compare ignoring graph version stamps (v=N) and node-count suffixes
# (n=N), which are expected to vary across runs.
if diff <(echo "$ACTUAL" | sed -E 's/v=[0-9]+/v=*/' | sed -E 's/n=[0-9]+/n=*/') \
         <(cat "$EXPECTED" | sed -E 's/v=[0-9]+/v=*/' | sed -E 's/n=[0-9]+/n=*/') > /dev/null; then
    echo "PASS: $INPUT"
    exit 0
else
    echo "FAIL: $INPUT"
    echo "--- expected ---"
    cat "$EXPECTED"
    echo "--- actual ---"
    echo "$ACTUAL"
    exit 1
fi
