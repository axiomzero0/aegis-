#!/usr/bin/env python3
"""scripts/check_perf.py — Rule 41 performance-regression gate.

Law (Rule 41): "If a benchmark regresses >5%, the PR must include root
cause analysis, justification, a tracking issue, and approval. No
silent performance degradation."

This script turns that law into an enforceable gate:

  1. Runs tests/perf benchmarks (or parses output handed to it).
  2. Compares each case's median against the checked-in baseline
     (scripts/perf_baseline.json).
  3. FAILS when any case regresses more than kThresholdPercent.
     Improvements are reported (and flagged for baseline refresh) but
     never fail the gate.

Baseline updates are a deliberate act with a paper trail:

    python3 scripts/check_perf.py --update-baseline --reason "<why>"

The reason string is stored in the baseline file and shows up in the
diff — that diff IS the Rule 41 waiver artifact reviewers sign off on.

Usage:
    python3 scripts/check_perf.py                     # gate vs baseline
    python3 scripts/check_perf.py --update-baseline --reason "..."
"""
import argparse
import datetime
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BENCH = REPO / "build" / "bench_pipeline"
BASELINE = REPO / "scripts" / "perf_baseline.json"

#: Rule 41's regression threshold (percent). A regression strictly
#: greater than this fails the gate; the waiver process (RCA + tracking
#: issue + approval) is the only way past it.
THRESHOLD_PERCENT = 5.0

#: Fractional noise floor: regressions smaller than this (in percent)
#: are treated as noise and ignored, so CI runners with scheduling
#: jitter don't flake. Deliberately well under the Rule 41 threshold.
NOISE_FLOOR_PERCENT = 0.25


def run_benchmarks() -> dict[str, float]:
    if not BENCH.exists():
        print(f"FAIL: benchmark binary not found at {BENCH} "
              f"(run scripts/build_tests.sh first)", file=sys.stderr)
        sys.exit(2)
    proc = subprocess.run([str(BENCH)], capture_output=True, text=True, timeout=600)
    if proc.returncode != 0:
        print("FAIL: benchmark binary exited non-zero", file=sys.stderr)
        sys.stderr.write(proc.stderr)
        sys.exit(2)
    medians: dict[str, float] = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        # Format: "bench <name> <iters> <nodes> <median_us>"
        # (skip the header row, whose last field is the literal
        # 'median_us' column label)
        if len(parts) == 5 and parts[0] == "bench" and parts[4] != "median_us":
            medians[parts[1]] = float(parts[4])
    if not medians:
        print("FAIL: could not parse benchmark output", file=sys.stderr)
        sys.exit(2)
    return medians


def load_baseline() -> dict:
    if not BASELINE.exists():
        print(f"FAIL: no baseline at {BASELINE} — create one with "
              f"--update-baseline (Rule 41 gating needs a reference)",
              file=sys.stderr)
        sys.exit(2)
    return json.loads(BASELINE.read_text())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--reason", default="")
    args = parser.parse_args()

    medians = run_benchmarks()

    if args.update_baseline:
        if not args.reason or len(args.reason) < 20:
            print("FAIL: --update-baseline requires --reason with a real "
                  "justification (>= 20 chars) — the reason is the Rule 41 "
                  "waiver record stored in the baseline diff.",
                  file=sys.stderr)
            return 2
        doc = {
            "captured": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "reason": args.reason,
            "threshold_percent": THRESHOLD_PERCENT,
            "cases": {k: v for k, v in sorted(medians.items())},
        }
        BASELINE.write_text(json.dumps(doc, indent=2) + "\n")
        print(f"OK: baseline updated ({len(medians)} cases) — review the "
              f"perf_baseline.json diff as the Rule 41 record")
        return 0

    baseline = load_baseline()
    base_cases: dict[str, float] = baseline.get("cases", {})

    regressions: list[str] = []
    improvements: list[str] = []
    print(f"{'case':<14} {'base_us':>10} {'now_us':>10} {'delta':>8}  verdict")
    for name, now in sorted(medians.items()):
        base = base_cases.get(name)
        if base is None:
            # A NEW benchmark case: informational only this run; it
            # enters the baseline at the next deliberate refresh.
            print(f"{name:<14} {'-':>10} {now:>10.1f} {'-':>8}  NEW (baseline on refresh)")
            continue
        delta_pct = (now - base) / base * 100.0
        if delta_pct > THRESHOLD_PERCENT:
            verdict = "REGRESSION (Rule 41: waiver required)"
            regressions.append(f"{name}: {delta_pct:+.1f}% ({base:.1f} -> {now:.1f} us)")
        elif delta_pct > NOISE_FLOOR_PERCENT:
            verdict = "slower (within threshold)"
        elif delta_pct < -THRESHOLD_PERCENT:
            verdict = "improved (refresh baseline recommended)"
            improvements.append(f"{name}: {delta_pct:+.1f}%")
        else:
            verdict = "ok"
        print(f"{name:<14} {base:>10.1f} {now:>10.1f} {delta_pct:>+7.1f}%  {verdict}")

    for name in base_cases:
        if name not in medians:
            print(f"NOTE: baseline case '{name}' no longer exists "
                  f"(benchmark renamed/removed?)")

    if regressions:
        print("\nFAIL: Rule 41 violations — regressions exceed "
              f"{THRESHOLD_PERCENT:.0f}%:")
        for r in regressions:
            print(f"  {r}")
        print("\nTo proceed you must (Rule 41): include root cause "
              "analysis, justification, a tracking issue, and approval "
              "in the PR; then refresh the baseline with\n"
              "  python3 scripts/check_perf.py --update-baseline "
              "--reason \"<RCA + issue #>\"")
        return 1
    if improvements:
        print("\nOK: no regressions. Improvements worth baselining:")
        for i in improvements:
            print(f"  {i}")
    else:
        print("\nOK: no regressions beyond the Rule 41 threshold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
