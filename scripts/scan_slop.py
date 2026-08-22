#!/usr/bin/env python3
"""scripts/scan_slop.py — Static check for Rule D.7 (untracked workarounds).

Law (Rule D.7): comments containing the forbidden workaround markers
(the vocabulary is defined in MARKERS below) are forbidden WITHOUT an
approved tracking issue and a removal deadline <= 2 sprints.

This scanner enforces the mechanical half of that law: any such marker
must be followed by a machine-readable tracking reference on the same
line (or the line immediately below):

    // TODO(aegis-#123, remove-by 2026-09-15): ...

The reference format is `(<owner>-#<number>` optionally with
`, remove-by YYYY-MM-DD`. Bare markers fail the scan.

Scanned trees: compiler/, runtime/, tools/, tests/, scripts/.
docs/ is exempt (the laws themselves discuss the banned phrases).
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

SCAN_DIRS = ["compiler", "runtime", "tools", "tests", "scripts"]
SKIP_DIRS = {".git", "build", "obj"}
MARKERS = ("HACK", "TODO", "FIXME", "WORKAROUND", "TEMPORARY")  # NOLINT(slop) — the scanned vocabulary itself

# A tracked marker cites an issue id and (best practice) a removal date.
TRACKED_RE = re.compile(
    r"\((?:[A-Za-z0-9_\-./]+-)?#(?P<issue>\d+)"
    r"(?:\s*,\s*remove-by\s+(?P<date>\d{4}-\d{2}-\d{2}))?"
    r"(?:\s*,\s*deadline\s+(?P<deadline>\d{4}-\d{2}-\d{2}))?"
    r"\)"
)


def scan_file(path: Path) -> list[str]:
    violations: list[str] = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return violations
    for line_no, line in enumerate(text.splitlines(), start=1):
        for marker in MARKERS:
            # Word-boundary match so TODOIST-style identifiers don't trip.
            if not re.search(rf"\b{marker}\b", line):
                continue
            # A marker inside a string literal is still a marker in
            # source — but a marker *quoted while explaining the rule*
            # (e.g. this scanner, docs) is exempt via the docs/ skip
            # and the NOLINT escape below.
            if "NOLINT(slop)" in line:
                break
            m = TRACKED_RE.search(line)
            if m is None:
                violations.append(
                    f"{path}:{line_no}: untracked '{marker}' marker: {line.strip()}"
                )
            elif m.group("date") is None and m.group("deadline") is None:
                violations.append(
                    f"{path}:{line_no}: tracked '{marker}' lacks a removal "
                    f"deadline (Rule D.7: <= 2 sprints): {line.strip()}"
                )
            break
    return violations


def main() -> int:
    roots = [REPO / d for d in SCAN_DIRS]
    missing = [d for d in roots if not d.is_dir()]
    if missing:
        print(f"FAIL: scan directories do not exist: {[str(d) for d in missing]}")
        return 2
    all_violations: list[str] = []
    for root in roots:
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in {".cpp", ".hpp", ".h", ".sh", ".py"}:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            all_violations.extend(scan_file(path))
    if not all_violations:
        print(f"OK: no untracked workaround markers under {SCAN_DIRS}")
        return 0
    print(f"FAIL: {len(all_violations)} untracked/unbounded workaround markers:")
    for v in all_violations:
        print(f"  {v}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
