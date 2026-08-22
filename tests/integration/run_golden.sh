#!/usr/bin/env bash
# tests/integration/run_golden.sh — Rule 37 golden IR test harness.
#
# Law (Rule 37): "Every optimization pass must have >=10 golden IR
# tests. Checked-in .in.aegis / .expected.son file pairs. Tests must
# run in both Static Mode (AOT) and Profile Mode (JIT)."
#
# This harness runs one golden pair in BOTH modes and diffs each mode's
# post-pipeline IR against the checked-in expectation. It also serves
# Rule 38 (differential testing): AOT and JIT must agree with each
# other on the shared expectation wherever a single .expected.son file
# is used — a divergence is a failing test, not a warning.
#
# Usage:
#   run_golden.sh <input.aegis> <expected-base> [--research]
#
#   <input.aegis>    the Aegis source file.
#   <expected-base>  path WITHOUT extension. The harness looks for:
#                       <base>.expected.son          (shared by both modes)
#                       <base>.expected.aot.son      (AOT-specific)
#                       <base>.expected.jit.son      (JIT-specific)
#                    Per-mode files override the shared one, which is
#                    how speculation-visible goldens (research passes)
#                    pin their mode-dependent IR.
#   --research       append the research passes (40-51) to the pipeline.
#
# Failures are loud (Rule D.3): missing compiler binary, empty output,
# or any diff exits non-zero with a full diff printout.
set -euo pipefail

INPUT="${1:?usage: run_golden.sh <input.aegis> <expected-base> [--research]}"
EXPECTED_BASE="${2:?usage: run_golden.sh <input.aegis> <expected-base> [--research]}"
shift 2 || true
RESEARCH_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --research) RESEARCH_ARGS+=(--research) ;;
        *) echo "run_golden.sh: unknown argument: $arg" >&2; exit 2 ;;
    esac
done

# ---- Locate the compiler binary (fail loudly — Rule D.3). ----
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ -n "${AEGISC:-}" ]]; then
    AEGISC_BIN="$AEGISC"
elif [[ -x "$ROOT/build/aegisc" ]]; then
    AEGISC_BIN="$ROOT/build/aegisc"
elif command -v aegisc > /dev/null 2>&1; then
    AEGISC_BIN="$(command -v aegisc)"
else
    echo "FAIL: aegisc binary not found (looked at \$AEGISC, $ROOT/build/aegisc, PATH)" >&2
    exit 2
fi

if [[ ! -f "$INPUT" ]]; then
    echo "FAIL: input file does not exist: $INPUT" >&2
    exit 2
fi

# Normalize a raw IR dump into comparable form:
#   - keep only the post-pipeline graph (the section AFTER the
#     '----- After passes -----' marker; the marker itself is dropped)
#   - blank graph version stamps (v=N) and node counts (n=N), which vary
#     run-to-run by design (version bumps are Rule 50 telemetry, not IR shape)
normalize() {
    sed -n '/^----- After passes -----$/,$p' | tail -n +2 \
        | sed -E 's/v=[0-9]+/v=*/' \
        | sed -E 's/n=[0-9]+/n=*/'
}

# Stamp-only normalization for EXPECTED files (they are already
# section-free; gen_golden.py wrote them that way). Hand-edited files
# with stray v=/n= stamps still compare cleanly.
normalize_stamps_only() {
    sed -E 's/v=[0-9]+/v=*/' | sed -E 's/n=[0-9]+/n=*/'
}

# Run one mode and compare against an expected file.
# usage: check_mode <mode-flag> <expected-file> ; returns 0 on match
check_mode() {
    local mode_flag="$1"
    local expected_file="$2"
    local actual
    actual="$("$AEGISC_BIN" "$INPUT" --dump-ir --no-verify $mode_flag "${RESEARCH_ARGS[@]+"${RESEARCH_ARGS[@]}"}" 2>/dev/null | normalize)" || true
    if [[ -z "$actual" ]]; then
        echo "FAIL: empty compiler output for $INPUT (${mode_flag:-default})" >&2
        return 1
    fi
    if diff <(echo "$actual") <(cat "$expected_file" | normalize_stamps_only) > /tmp/golden_diff_$$ 2>&1; then
        return 0
    fi
    echo "FAIL: $INPUT (${mode_flag:-default}) vs $expected_file" >&2
    cat /tmp/golden_diff_$$ >&2
    rm -f /tmp/golden_diff_$$
    return 1
}

# ---- Resolve expected files. ----
AOT_EXPECTED="${EXPECTED_BASE}.expected.aot.son"
JIT_EXPECTED="${EXPECTED_BASE}.expected.jit.son"
SHARED_EXPECTED="${EXPECTED_BASE}.expected.son"

if [[ -f "$AOT_EXPECTED" && -f "$JIT_EXPECTED" ]]; then
    # Mode-specific expectations (speculation-visible goldens).
    check_mode "--aot" "$AOT_EXPECTED" || exit 1
    check_mode "--jit" "$JIT_EXPECTED" || exit 1
elif [[ -f "$SHARED_EXPECTED" ]]; then
    # Shared expectation: both modes must match it, which additionally
    # enforces AOT == JIT (Rule 38 differential) for this input.
    check_mode "--aot" "$SHARED_EXPECTED" || exit 1
    check_mode "--jit" "$SHARED_EXPECTED" || exit 1
else
    echo "FAIL: no expected file found for base '$EXPECTED_BASE'" >&2
    echo "      (looked for .expected.son, .expected.aot.son/.expected.jit.son)" >&2
    exit 2
fi

echo "PASS: $INPUT (aot+jit)"
exit 0
