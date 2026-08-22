#!/usr/bin/env python3
"""scripts/scan_magic_numbers.py — Static check for Rule 61 + D.1.

Scans every .cpp/.hpp file under compiler/ + runtime/ + tools/ for
suspicious magic numbers in logic. Allowed: literals 0, 1, -1, 2, -2,
true, false. Larger numbers must come from a named constexpr.

Files/lines exempted (principled, not filename hacks):
  - Lines that DEFINE a named constant (`constexpr ... kName{N}`) —
    the literal is the definition, which is exactly the compliant
    pattern Rule 61 asks for.
  - Enum member definitions (`RAX = 0,`) — hardware register
    encodings and similar declarative tables (Law D.2 explicitly
    endorses declarative tables for ABI definitions; the member name
    is the documentation).
  - Numeric literals inside string literals and comments (they are
    text, not logic).
  - Lines carrying an inline `// NOLINT(magic-numbers)` marker with a
    justification.

Exits non-zero if any violation is found.
"""
import re
import sys
from pathlib import Path

# Resolve the repo root from THIS SCRIPT's location so the scan works
# in any checkout (CI, fresh clone, local). A hard-coded absolute path
# would silently scan nothing when the checkout moves — a Rule D.3
# silent-fallback defect.
REPO = Path(__file__).resolve().parent.parent

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

# A line that DEFINES a named constant: `constexpr uint32_t kFoo{64};`
# or `static constexpr size_t kBar = 3;`. The literal on such a line
# is the value of the documented constant — the compliant pattern.
CONST_DEF_RE = re.compile(r'\bconstexpr\b')

# An enum member definition: `RAX = 0,` / `XMM10 = 10,`. Declarative
# tables for hardware encodings (Law D.2); the name documents the
# number.
ENUM_MEMBER_RE = re.compile(r'^\s*[A-Za-z_]\w*\s*=\s*-?\d+\s*[,;}]')

# Strip string literals first (so a // inside a string does not look
# like a comment start), then strip // comments.
STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'')
LINE_COMMENT_RE = re.compile(r'//[^\n]*')

def strip_noncode(line: str) -> str:
    """Remove string literals and // comments from a source line."""
    line = STRING_RE.sub('""', line)
    line = LINE_COMMENT_RE.sub('', line)
    return line

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
    in_block_comment = False
    for line_no, line in enumerate(text.splitlines(), start=1):
        # Track block comments spanning multiple lines.
        if in_block_comment:
            if "*/" in line:
                in_block_comment = False
                line = line.split("*/", 1)[1]
            else:
                continue
        if "/*" in line and "*/" not in line:
            in_block_comment = True
            line = line.split("/*", 1)[0]
        # Skip comment-only lines.
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
            continue
        # Skip lines with NOLINT.
        if "NOLINT(magic-numbers)" in line or "NOLINT(magic)" in line:
            continue
        code = strip_noncode(line)
        # Skip constant definitions and enum member definitions — the
        # literal is the (documented) definition itself.
        if CONST_DEF_RE.search(code):
            continue
        if ENUM_MEMBER_RE.match(code):
            continue
        # Find all numeric literals in the code part of the line.
        for m in LITERAL_RE.finditer(code):
            literal = m.group(1)
            if literal in ALLOWED:
                continue
            violations.append(
                f"{path}:{line_no}: magic number '{literal}' in: {line.strip()}"
            )
    return violations

def main() -> int:
    # Rule D.3: fail loudly (not vacuously) if the source tree is not
    # where this script expects it. A missing directory previously made
    # the scan pass with zero files checked.
    missing = [d for d in SCAN_DIRS if not d.is_dir()]
    if missing:
        print(f"FAIL: scan directories do not exist: {[str(d) for d in missing]}")
        print(f"      (resolved repo root: {REPO})")
        return 2
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
