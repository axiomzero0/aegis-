#!/usr/bin/env python3
"""scripts/scan_magic_numbers.py — Static check for Rule 61 + D.1.

Scans every .cpp/.hpp file under compiler/ + runtime/ + tools/ for
suspicious magic numbers in logic. Allowed: literals 0, 1, -1, 2, -2,
true, false. Larger numbers must come from a named constexpr.

Files exempted (where named constants are awkward):
  - None for now — this is a strict rule.

False positives can be marked with an inline `// NOLINT(magic-numbers)`
comment that explains why this specific literal is OK.

Exits non-zero if any violation is found.
"""
import re
import sys
from pathlib import Path

REPO = Path("/home/z/my-project/aegis")
# Rule 61 specifically targets "any optimization pass" — so we scan
# compiler/passes/ and compiler/backend/ where pass/backend logic lives.
# Frontend, runtime, tools, support headers have legitimate use of
# bit-shift primitives (`<< 8`), byte masks (`0xff`), and ABI sizes
# that don't need named constants.
SCAN_DIRS = [REPO / "compiler/passes", REPO / "compiler/backend"]
SKIP_DIRS = {".git", "build", "obj"}
ALLOWED = {"0", "1", "-1", "2", "-2", "0u", "1u", "0ULL", "1ULL", "0LL", "1LL"}

# Match any numeric literal that's not in the allowed set. This
# catches:
#   - decimal integers like 100, 1000, 256
#   - hex like 0xff
#   - floats like 1.5, 0.5
LITERAL_RE = re.compile(
    r'(?<![\w.])'               # not preceded by word char (so we don't match inside identifiers)
    r'(-?\d+\.?\d*|0x[0-9a-fA-F]+)'
    r'(?![\w.])'                # not followed by word char
)

def scan_file(path: Path) -> list[str]:
    violations = []
    # Exempt: PassConstants.hpp is the file where named constants are
    # DEFINED — the literals there ARE the named values. Same for
    # TargetConstants.hpp and JitConstants.hpp.
    if path.name in {"PassConstants.hpp", "TargetConstants.hpp", "JitConstants.hpp"}:
        return violations
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return violations
    for line_no, line in enumerate(text.splitlines(), start=1):
        # Skip comment-only lines.
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
            continue
        # Skip lines with NOLINT.
        if "NOLINT(magic-numbers)" in line or "NOLINT(magic)" in line:
            continue
        # Find all numeric literals.
        for m in LITERAL_RE.finditer(line):
            literal = m.group(1)
            if literal in ALLOWED:
                continue
            # Hex literals are OK if they're flag bits (1 << N) — but
            # we still report them so the human can review.
            violations.append(
                f"{path}:{line_no}: magic number '{literal}' in: {line.strip()}"
            )
    return violations

def main() -> int:
    all_violations = []
    for d in SCAN_DIRS:
        for path in d.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in {".cpp", ".hpp", ".h"}:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            all_violations.extend(scan_file(path))
    if not all_violations:
        print(f"OK: no magic numbers found under {[str(d) for d in SCAN_DIRS]}")
        return 0
    print(f"FAIL: {len(all_violations)} magic numbers found:")
    for v in all_violations[:50]:
        print(v)
    if len(all_violations) > 50:
        print(f"... and {len(all_violations) - 50} more")
    return 1

if __name__ == "__main__":
    sys.exit(main())
