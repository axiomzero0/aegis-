#!/usr/bin/env bash
# tests/integration/run_all_golden.sh — Run the entire Rule 37 golden suite.
#
# Discovers every .in.aegis under tests/integration/golden/<pass>/ and
# runs it through run_golden.sh in BOTH modes (AOT + JIT). Research
# pass directories run with --research (the unified pipeline opt-in).
#
# Also enforces the Rule 37 census: every pass directory must hold
# >= 10 golden pairs, or this script fails loudly.
#
# Usage: run_all_golden.sh [aegisc-path]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GOLDEN="$HERE/golden"
RUNNER="$HERE/run_golden.sh"

# Passes whose goldens run the research pipeline (Rule A.1 opt-in).
RESEARCH_PASSES="cfl_alias value_flow pgdlo mem_pool_synthesis cache_oblivious_layout slp_vectorization auto_parallelization guarded_devirtualization speculative_bce speculative_effect_reordering speculative_lock_elision bolt_layout"

# Rule 37 census (named constant, not a magic number).
MIN_PER_PASS=10

if [[ -n "${1:-}" ]]; then
    export AEGISC="$1"
fi

total=0
failed=0
for dir in "$GOLDEN"/*/; do
    pass="$(basename "$dir")"
    count=$(ls -1 "$dir"/*.in.aegis 2>/dev/null | wc -l)
    if (( count < MIN_PER_PASS )); then
        echo "FAIL (Rule 37 census): pass '$pass' has $count goldens (< $MIN_PER_PASS)" >&2
        failed=$((failed + 1))
        continue
    fi
    extra=""
    for rp in $RESEARCH_PASSES; do
        if [[ "$pass" == "$rp" ]]; then extra="--research"; fi
    done
    for input in "$dir"/*.in.aegis; do
        base="${input%.in.aegis}"
        if bash "$RUNNER" "$input" "$base" $extra > /dev/null 2>&1; then
            total=$((total + 1))
        else
            echo "FAIL: $input"
            bash "$RUNNER" "$input" "$base" $extra > /dev/null || true
            failed=$((failed + 1))
        fi
    done
done

if (( failed > 0 )); then
    echo "FAIL: $failed golden test(s) failed ($total passed)" >&2
    exit 1
fi
echo "OK: all $total golden pairs passed (AOT + JIT)"
