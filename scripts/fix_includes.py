#!/usr/bin/env python3
"""scripts/fix_includes.py — Update include paths after directory restructure.

Rewrites include paths from the old layout to the new layout:

  "common/Primitives.h"     -> "aegis/support/Primitives.hpp"
  "common/Flags.h"          -> "aegis/support/Flags.hpp"
  "common/Diagnostics.h"    -> "aegis/support/Diagnostics.hpp"
  "common/Expected.h"       -> "aegis/support/Expected.hpp"
  "core/SymbolTable.h"      -> "aegis/support/StringIntern.hpp"
  "core/SmallVector.h"      -> "aegis/support/SmallVector.hpp"
  "core/SparseSet.h"        -> "aegis/support/SparseSet.hpp"
  "core/BitVector.h"        -> "aegis/support/BitVector.hpp"
  "core/SwissTable.h"       -> "aegis/support/SwissTable.hpp"
  "ir/NodeKind.h"           -> "aegis/ir/NodeKind.hpp"
  "ir/Node.h"               -> "aegis/ir/Node.hpp"
  "ir/Graph.h"              -> "aegis/ir/Graph.hpp"
  "ir/HashConsing.h"        -> "aegis/ir/HashConsing.hpp"
  "ir/Verifier.h"           -> "aegis/ir/Verifier.hpp"
  "ir/Printer.h"            -> "aegis/ir/Printer.hpp"
  "frontend/AST.h"          -> "aegis/frontend/AST.hpp"
  "frontend/ASTPrinter.h"  -> "aegis/frontend/ASTPrinter.hpp"
  "frontend/Lexer.h"       -> "aegis/frontend/Lexer.hpp"
  "frontend/Parser.h"      -> "aegis/frontend/Parser.hpp"
  "frontend/Lowerer.h"     -> "aegis/frontend/Lowering.hpp"
  "frontend/TypeChecker.h" -> "aegis/frontend/TypeChecker.hpp"
  "frontend/EffectInference.h" -> "aegis/frontend/EffectInference.hpp"
  "passes/Pass.h"          -> "aegis/passes/Pass.hpp"
  "passes/PassManager.h"   -> "aegis/passes/PassManager.hpp"
  "passes/GVN.h"           -> "aegis/passes/mid/GVN.hpp"
  "passes/EDCE.h"          -> "aegis/passes/mid/EDCE.hpp"
  "passes/SCCP.h"          -> "aegis/passes/mid/SCCP.hpp"
  "passes/SimplifyControl.h" -> "aegis/passes/mid/SimplifyControl.hpp"
  "passes/Passes_Standard.h" -> "aegis/passes/mid/StandardPipeline.hpp"
  "backend/MachineInstr.h"   -> "aegis/backend/MachineIR.hpp"
  "backend/LinearScan.h"     -> "aegis/backend/RegAlloc/LinearScan.hpp"
  "backend/InstrSelection.h" -> "aegis/backend/InstrSel.hpp"
"""

import os
import re
import sys

ROOT = "/home/z/my-project/aegis"

# Map of old include path -> new include path
MAPPINGS = [
    # support/
    ("common/Primitives.h",   "aegis/support/Primitives.hpp"),
    ("common/Flags.h",         "aegis/support/Flags.hpp"),
    ("common/Diagnostics.h",   "aegis/support/Diagnostics.hpp"),
    ("common/Expected.h",     "aegis/support/Expected.hpp"),
    ("core/SymbolTable.h",    "aegis/support/StringIntern.hpp"),
    ("core/SmallVector.h",    "aegis/support/SmallVector.hpp"),
    ("core/SparseSet.h",      "aegis/support/SparseSet.hpp"),
    ("core/BitVector.h",      "aegis/support/BitVector.hpp"),
    ("core/SwissTable.h",     "aegis/support/SwissTable.hpp"),
    # ir/
    ("ir/NodeKind.h",         "aegis/ir/NodeKind.hpp"),
    ("ir/Node.h",             "aegis/ir/Node.hpp"),
    ("ir/Graph.h",            "aegis/ir/Graph.hpp"),
    ("ir/HashConsing.h",     "aegis/ir/HashConsing.hpp"),
    ("ir/Verifier.h",        "aegis/ir/Verifier.hpp"),
    ("ir/Printer.h",         "aegis/ir/Printer.hpp"),
    # frontend/
    ("frontend/AST.h",            "aegis/frontend/AST.hpp"),
    ("frontend/ASTPrinter.h",     "aegis/frontend/ASTPrinter.hpp"),
    ("frontend/Lexer.h",          "aegis/frontend/Lexer.hpp"),
    ("frontend/Parser.h",         "aegis/frontend/Parser.hpp"),
    ("frontend/Lowerer.h",        "aegis/frontend/Lowering.hpp"),
    ("frontend/TypeChecker.h",    "aegis/frontend/TypeChecker.hpp"),
    ("frontend/EffectInference.h","aegis/frontend/EffectInference.hpp"),
    # passes/
    ("passes/Pass.h",            "aegis/passes/Pass.hpp"),
    ("passes/PassManager.h",     "aegis/passes/PassManager.hpp"),
    ("passes/GVN.h",             "aegis/passes/mid/GVN.hpp"),
    ("passes/EDCE.h",            "aegis/passes/mid/EDCE.hpp"),
    ("passes/SCCP.h",            "aegis/passes/mid/SCCP.hpp"),
    ("passes/SimplifyControl.h", "aegis/passes/mid/SimplifyControl.hpp"),
    ("passes/Passes_Standard.h", "aegis/passes/mid/StandardPipeline.hpp"),
    # backend/
    ("backend/MachineInstr.h",    "aegis/backend/MachineIR.hpp"),
    ("backend/LinearScan.h",      "aegis/backend/RegAlloc/LinearScan.hpp"),
    ("backend/InstrSelection.h",  "aegis/backend/InstrSel.hpp"),
]

def rewrite_file(path: str) -> int:
    with open(path, 'r') as f:
        content = f.read()
    original = content
    for old, new in MAPPINGS:
        # Match #include "...old" — exact path (no prefix). The quotes
        # could be either <> or "" — we handle both.
        content = re.sub(
            r'#(\s*include\s*[<"])' + re.escape(old) + r'([>"])',
            r'#\1' + new + r'\2',
            content
        )
    if content != original:
        with open(path, 'w') as f:
            f.write(content)
        return 1
    return 0

def main() -> int:
    total = 0
    for dirpath, _, filenames in os.walk(ROOT):
        if '/.git' in dirpath or '/build' in dirpath:
            continue
        for name in filenames:
            if name.endswith('.hpp') or name.endswith('.cpp'):
                path = os.path.join(dirpath, name)
                if rewrite_file(path):
                    total += 1
                    print(f"updated {path}")
    print(f"\n{total} files updated")
    return 0

if __name__ == "__main__":
    sys.exit(main())
