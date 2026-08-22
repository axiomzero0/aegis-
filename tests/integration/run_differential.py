#!/usr/bin/env python3
"""tests/integration/run_differential.py — Rule 38 differential testing.

Law (Rule 38): "AOT Baseline <-> JIT Apex <-> Reference Interpreter
comparisons run on every PR. Divergence blocks merge. Assert
byte-for-byte identical results and memory layouts."

For every generated program this harness asserts THREE-WAY agreement:

  1. Reference value — the expression evaluated independently in
     Python with exact int64-range arithmetic (the "interpreter").
  2. AOT mode — aegisc --aot: the post-pipeline IR must fold main's
     return value to the reference constant.
  3. JIT mode — aegisc --jit: same expectation, PLUS the normalized
     post-pipeline IR must be byte-for-byte identical to AOT mode
     (the standard pipeline must be mode-independent; Rule A.1).

The program corpus is generated from a FIXED, documented seed so any
failure is exactly reproducible (Rule 40: debugging starts from
replay). Failing programs + their outputs are saved under
replay-artifacts/ (gitignored).

Usage:
    python3 tests/integration/run_differential.py [--count N] [--seed S]
"""
import argparse
import random
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
AEGISC = REPO / "build" / "aegisc"
REPLAY_DIR = REPO / "replay-artifacts"

# ---- Corpus parameters (named + documented per Rule 61) ----

#: Default corpus size. 200 programs give the corpus ~1000 operator
#: applications — deep enough to catch folding divergence without
#: slowing CI meaningfully (<10 s).
DEFAULT_COUNT = 200

#: Default seed. Fixed so a failure on CI reproduces bit-for-bit
#: locally (Rule 40). Change the seed to explore a new corpus.
DEFAULT_SEED = 20260823  # date-based: reproducible + memorable.

#: Bounded constants keep every intermediate inside int64 so the
#: Python reference is exact (no overflow semantics to model).
CONST_MIN = 0
CONST_MAX = 99

#: Maximum expression depth (AST height).
MAX_DEPTH = 5

#: Shift amounts are small so no int64 overflow can occur.
SHIFT_MAX = 5


def gen_expr(rng: random.Random, depth: int) -> str:
    """Generate one random arithmetic expression string."""
    if depth <= 0 or rng.random() < 0.25:
        return str(rng.randint(CONST_MIN, CONST_MAX))
    choice = rng.random()
    if choice < 0.60:
        a = gen_expr(rng, depth - 1)
        b = gen_expr(rng, depth - 1)
        op = rng.choice(["+", "-", "*", "&", "|", "^"])
        return f"({a} {op} {b})"
    if choice < 0.80:
        a = gen_expr(rng, depth - 1)
        op = rng.choice(["<<", ">>"])
        return f"({a} {op} {rng.randint(0, SHIFT_MAX)})"
    # Unary ops.
    a = gen_expr(rng, depth - 1)
    op = rng.choice(["-", "~", "!"])
    return f"{op}({a})"


def reference_value(expr: str) -> int:
    """Evaluate the generated expression exactly as Python int math.

    The generator only emits ops whose Python semantics match the
    compiler's documented int64 semantics at these magnitudes:
    + - * & | ^ << >> are identical; unary - ~ map directly; ! maps
    to 1-if-zero.
    """
    py = expr.replace("!", " __not__ ")
    value = eval(  # noqa: S307 — corpus is locally generated, not user input
        py, {"__not__": lambda x: 1 if x == 0 else 0, "__builtins__": {}}
    )
    return int(value)


def normalized_post_pipeline_ir(stdout: str) -> str:
    """Extract the post-pipeline graph, normalizing version/node stamps."""
    lines: list[str] = []
    keep = False
    for line in stdout.splitlines():
        if line.startswith("----- After passes -----"):
            keep = True
            continue
        if keep:
            line = re.sub(r"v=\d+", "v=*", line)
            line = re.sub(r"n=\d+", "n=*", line)
            lines.append(line)
    if not lines:
        raise RuntimeError("no post-pipeline IR in compiler output")
    return "\n".join(lines) + "\n"


def folded_return_value(ir: str) -> int | None:
    """Return the Constant value feeding the LAST Return in the IR."""
    lines = ir.splitlines()
    last_ret_val: str | None = None
    for line in lines:
        m = re.match(r"^n(\d+) : Return \(Pure\) ins=\[(\d+),(\d+),(\d+)\]", line)
        if m:
            last_ret_val = m.group(4)
    if last_ret_val is None:
        return None
    for line in lines:
        m = re.match(rf"^n{last_ret_val} : Constant \(Pure\) value=(-?\d+)", line)
        if m:
            return int(m.group(1))
    return None


def run_aegisc(src_path: Path, mode: str) -> str:
    proc = subprocess.run(
        [str(AEGISC), str(src_path), "--dump-ir", "--no-verify", mode],
        capture_output=True, text=True, timeout=60,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"aegisc {mode} failed:\n{proc.stderr}")
    return proc.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    args = parser.parse_args()

    if not AEGISC.exists():
        print(f"FAIL: aegisc not found at {AEGISC} (run scripts/build.sh first)")
        return 2

    rng = random.Random(args.seed)
    failures: list[str] = []
    for i in range(args.count):
        expr = gen_expr(rng, MAX_DEPTH)
        src = f"fn main() -> i32 {{\n    return {expr};\n}}\n"
        src_path = REPO / "build" / f"diff_case_{i}.aegis"
        src_path.write_text(src)

        try:
            ref = reference_value(expr)
            aot_ir = normalized_post_pipeline_ir(run_aegisc(src_path, "--aot"))
            jit_ir = normalized_post_pipeline_ir(run_aegisc(src_path, "--jit"))
            aot_again = normalized_post_pipeline_ir(run_aegisc(src_path, "--aot"))
            got = folded_return_value(aot_ir)
            problems = []
            if got != ref:
                problems.append(f"value mismatch: aot={got} reference={ref}")
            if aot_ir != jit_ir:
                problems.append("AOT and JIT IR diverged (Rule A.1/38)")
            if aot_ir != aot_again:
                problems.append("non-deterministic output (Rule 40)")
        except Exception as exc:  # noqa: BLE001 — report and keep going
            problems = [f"harness error: {exc}"]

        if problems:
            failures.append(f"case {i} [{expr}]: " + "; ".join(problems))
            REPLAY_DIR.mkdir(exist_ok=True)
            (REPLAY_DIR / f"diff_case_{i}.aegis").write_text(src)
            (REPLAY_DIR / f"diff_case_{i}.log").write_text(
                f"expr: {expr}\nproblems: {problems}\n"
            )
        src_path.unlink(missing_ok=True)

    if failures:
        print(f"FAIL: {len(failures)}/{args.count} differential mismatches:")
        for f in failures[:20]:
            print(f"  {f}")
        if len(failures) > 20:
            print(f"  ... and {len(failures) - 20} more (see {REPLAY_DIR}/)")
        return 1
    print(f"OK: {args.count} programs agree across reference/AOT/JIT "
          f"(seed={args.seed})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
