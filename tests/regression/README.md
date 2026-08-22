# tests/regression/ — Rule 36 regression suite

Every bug fix must ship with **≥5 regression tests** (Rule 36 of
`docs/laws.md`): minimal reproducer, variant trigger, boundary /
negative, integration, and deopt / state reconstruction.

## Suites

| Binary | Bug | Categories |
|--------|-----|------------|
| `test_regression` (built from `test_regression.cpp`) | **R1** speculative passes were non-idempotent under the PassManager fixpoint loop — every re-run minted a duplicate `FrameState` per speculated call until the budget cap (Rule B.5) | 5 (r1_*) |
| `test_regression` | **R2** `GuardedDevirtualization` / `SpeculativeLockElision` stored the FrameState id in `payload.u64`, the same union slot as the callee `SymbolId` — the callee was silently redirected to a bogus symbol (Rule 62 data corruption) | 5 (r2_*) |
| `test_regression` | **R3** assignment statements were silently dropped: the Pratt parser never classified assignment tokens as operators, and the Lowerer's `ExprStmt` path discarded the store — programs computed wrong answers with no diagnostic (Rule D.3) | 5 (r3_*) |
| `run_golden.sh` (harness-level, exercised by every CI golden test) | **R4** the golden harness invoked a nonexistent `aegis` binary, so all Rule 37 golden tests failed before running the compiler; the harness also silently "skipped" instead of failing loudly | harness regression — `run_golden.sh` now resolves `aegisc` (env var → `build/aegisc` → PATH) and exits non-zero when it cannot |

## Running

```sh
bash scripts/build_tests.sh   # builds tests/regression/test_regression
./build/test_regression
```

## Adding a new regression suite

1. Name every test for the behavior it pins (Rule 43):
   `r5_<minimal|variant|boundary|integration|deopt>_<what_it_checks>`.
2. Provide all five Rule 36 categories — CI counts them.
3. Register the new binary in `tests/CMakeLists.txt` **and**
   `scripts/build_tests.sh` (both build systems must stay in sync —
   the dual-build contract is documented in the README).
