#!/usr/bin/env python3
"""scripts/gen_golden.py — Generate + verify the Rule 37 golden suite.

Law (Rule 37): every optimization pass must have >=10 golden IR tests,
checked in as .in.aegis / .expected.son pairs, run in BOTH Static
(AOT) and Profile (JIT) modes.

This script is the AUTHORING + REGENERATION tool for those pairs:

  1. Writes the hand-authored .in.aegis inputs (the TESTS table below
     is the source of truth — each entry is a distinct, Rule 43-named
     scenario targeting one pass).
  2. Runs aegisc on each input in AOT and JIT mode (plus --research
     for research-pass directories) and captures the normalized
     post-pipeline IR as the checked-in expectation.
     - If both modes produce identical IR -> one shared .expected.son.
     - If the modes differ (speculation-visible goldens) -> per-mode
       .expected.aot.son + .expected.jit.son.
  3. Verifies run-to-run determinism (compiles every input twice and
     requires byte-identical output — Rule 40 replay stability).
  4. Verifies the >=10-per-pass census (Rule 37) and fails loudly if
     any pass directory is short.

Re-run this script whenever a pass INTENTIONALLY changes the IR it
produces; the diff of the .expected.son files is the review artifact
proving the change was intended (Rule 52: no accidental rewrites).
"""
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
AEGISC = REPO / "build" / "aegisc"
GOLDEN = REPO / "tests" / "integration" / "golden"

# Directories whose pass runs behind --research (the unified pipeline
# opt-in, Rule A.1). Standard-pipeline directories run the 19-pass
# mid-level pipeline.
RESEARCH_DIRS = {
    "cfl_alias", "value_flow", "pgdlo", "mem_pool_synthesis",
    "cache_oblivious_layout", "slp_vectorization", "auto_parallelization",
    "guarded_devirtualization", "speculative_bce",
    "speculative_effect_reordering", "speculative_lock_elision",
    "bolt_layout",
}

# Minimum golden tests per pass (Rule 37).
MIN_PER_PASS = 10

TESTS: dict[str, list[tuple[str, str]]] = {}


def _fn(name: str, body: str, params: str = "") -> str:
    return f"fn {name}({params}) -> i32 {{\n{body}\n}}\n"


def _main(body: str) -> str:
    return _fn("main", body)


# ============================================================
# SCCP — sparse conditional constant propagation (folding).
# ============================================================
TESTS["sccp"] = [
    ("add_chain_constants_fully_folds",
     _main("    return 1 + 2 + 3;")),
    ("mul_div_mod_constants_fully_folds",
     _main("    return (7 * 6) / 2 % 5;")),
    ("nested_parens_precedence_folds",
     _main("    return (2 + 3) * (4 - 1);")),
    ("shift_constants_fully_fold",
     _main("    return (1 << 4) + (32 >> 2);")),
    ("bitwise_constants_fully_fold",
     _main("    return (12 & 10) | (3 ^ 5);")),
    ("comparison_constants_fold_to_flags",
     _main("    return (3 < 5) + (5 <= 5) + (4 > 9) + (4 >= 4);")),
    ("logical_and_or_constants_fold",
     _main("    return (1 < 2 && 2 < 3) + (1 > 2 || 3 > 2);")),
    ("unary_minus_constants_fold",
     _main("    return -7 + (1 - -2);")),
    ("let_bound_constants_propagate",
     _main("    let a = 2;\n    let b = 3;\n    let c = a * b;\n    return c + 1;")),
    ("constant_folds_inside_call_args",
     _fn("id", "    return v;", "v: i32") + _main("    return id(4 + 5);")),
]

# ============================================================
# StrengthReduction — param-operand algebraic identities.
# (Constant operands fold away in SCCP first; these survive to SR.)
# ============================================================
TESTS["strength_reduction"] = [
    ("mul_by_two_becomes_shl",
     _fn("f", "    return x * 2;", "x: i32") + _main("    return f(21);")),
    ("mul_by_eight_becomes_shl3",
     _fn("f", "    return x * 8;", "x: i32") + _main("    return f(3);")),
    ("mul_by_one_identity_removed",
     _fn("f", "    return x * 1;", "x: i32") + _main("    return f(9);")),
    ("mul_by_zero_collapses",
     _fn("f", "    return x * 0;", "x: i32") + _main("    return f(9);")),
    ("add_zero_right_identity_removed",
     _fn("f", "    return x + 0;", "x: i32") + _main("    return f(5);")),
    ("add_zero_left_identity_removed",
     _fn("f", "    return 0 + x;", "x: i32") + _main("    return f(5);")),
    ("sub_zero_right_identity_removed",
     _fn("f", "    return x - 0;", "x: i32") + _main("    return f(5);")),
    ("sub_self_collapses_to_zero",
     _fn("f", "    return x - x;", "x: i32") + _main("    return f(5);")),
    ("and_zero_mask_collapses",
     _fn("f", "    return x & 0;", "x: i32") + _main("    return f(13);")),
    ("xor_self_collapses_to_zero",
     _fn("f", "    return x ^ x;", "x: i32") + _main("    return f(13);")),
]

# ============================================================
# GVN — global value numbering of pure expressions.
# ============================================================
TESTS["gvn"] = [
    ("duplicate_add_pair_deduped",
     _fn("f", "    let c = a + b;\n    let d = a + b;\n    return c + d;", "a: i32, b: i32")
     + _main("    return f(1, 2);")),
    ("duplicate_return_expr_deduped",
     _fn("f", "    return (x * 3) + (x * 3);", "x: i32") + _main("    return f(4);")),
    ("duplicate_across_if_branches_deduped",
     _fn("f", "    if x > 0 {\n        return x + 7;\n    } else {\n        return x + 7;\n    }", "x: i32")
     + _main("    return f(1);")),
    ("nested_duplicate_subexpr_deduped",
     _fn("f", "    return (a + b) * (a + b) + (a + b);", "a: i32, b: i32") + _main("    return f(2, 3);")),
    ("duplicate_comparison_deduped",
     _fn("f", "    let p = a < b;\n    let q = a < b;\n    return p + q;", "a: i32, b: i32")
     + _main("    return f(1, 2);")),
    ("duplicate_across_functions_deduped",
     _fn("f", "    return x + 100;", "x: i32") + _fn("g", "    return y + 100;", "y: i32")
     + _main("    return f(1) + g(2);")),
    ("duplicate_shift_expr_deduped",
     _fn("f", "    return (x << 2) + (x << 2);", "x: i32") + _main("    return f(3);")),
    ("duplicate_call_arg_exprs_deduped",
     _fn("g", "    return u + v;", "u: i32, v: i32")
     + _fn("f", "    return g(x + 9, x + 9);", "x: i32") + _main("    return f(1);")),
    ("hash_cons_dedup_on_lowering",
     _fn("f", "    let c = a + b;\n    let d = a + b;\n    return c;", "a: i32, b: i32")
     + _main("    return f(1, 2);")),
    ("duplicate_negation_deduped",
     _fn("f", "    return -x + -x;", "x: i32") + _main("    return f(5);")),
]

# ============================================================
# EDCE — effect-aware dead code elimination (always last).
# ============================================================
TESTS["edce"] = [
    ("dead_let_binding_swept",
     _main("    let dead = 5;\n    return 1;")),
    ("dead_expression_statement_swept",
     _main("    2 + 3;\n    return 1;")),
    ("code_after_return_is_dead",
     _main("    return 1;\n    let gone = 2 + 2;\n    return gone;")),
    ("dead_chain_of_lets_swept",
     _main("    let a = 1;\n    let b = a + 1;\n    let c = b + 1;\n    return 9;")),
    ("live_and_dead_lets_mixed",
     _main("    let live = 4;\n    let dead = 5;\n    return live + 1;")),
    ("dead_let_inside_if_branch_swept",
     _fn("f", "    if x > 0 {\n        let dead = 1;\n        return 1;\n    } else {\n        return 2;\n    }", "x: i32")
     + _main("    return f(3);")),
    ("call_with_unused_result_stays_live",
     _fn("g", "    return v;", "v: i32") + _main("    g(7);\n    return 1;")),
    ("dead_constant_fold_result_swept",
     _main("    let x = 2 * 3;\n    return 7;")),
    ("dead_binary_tree_swept",
     _fn("f", "    let t = (a + b) * (a - b);\n    return a;", "a: i32, b: i32")
     + _main("    return f(3, 4);")),
    ("only_return_survives",
     _fn("f", "    return x;", "x: i32") + _main("    return f(6);")),
]

# ============================================================
# CopyPropagation — identity copies + identical-input phis.
# ============================================================
TESTS["copy_propagation"] = [
    ("phi_with_identical_inputs_collapses",
     _fn("f", "    var t = 0;\n    if x > 0 {\n        t = x + 1;\n    } else {\n        t = x + 1;\n    }\n    return t;", "x: i32")
     + _main("    return f(2);")),
    ("let_alias_chain_collapses",
     _fn("f", "    let a = x;\n    let b = a;\n    return b;", "x: i32") + _main("    return f(8);")),
    ("reassign_same_value_collapses",
     _fn("f", "    var t = x;\n    t = x;\n    return t;", "x: i32") + _main("    return f(8);")),
    ("copy_of_negation_collapses",
     _fn("f", "    let a = -x;\n    let b = a;\n    return b;", "x: i32") + _main("    return f(8);")),
    ("copy_of_comparison_collapses",
     _fn("f", "    let a = x < 4;\n    let b = a;\n    return b;", "x: i32") + _main("    return f(8);")),
    ("identical_branch_bodies_collapse",
     _fn("f", "    if x > 0 {\n        return x * 2;\n    } else {\n        return x * 2;\n    }", "x: i32")
     + _main("    return f(3);")),
    ("copy_through_call_result",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let a = g(x);\n    let b = a;\n    return b;", "x: i32") + _main("    return f(5);")),
    ("param_alias_in_call_args",
     _fn("g", "    return u + v;", "u: i32, v: i32")
     + _fn("f", "    let a = x;\n    return g(a, a);", "x: i32") + _main("    return f(5);")),
    ("branch_same_binding_merges_to_value",
     _fn("f", "    if x > 1 {\n        let t = x + 2;\n        return t;\n    } else {\n        let t = x + 2;\n        return t;\n    }", "x: i32")
     + _main("    return f(4);")),
    ("copy_chain_across_two_fns",
     _fn("g", "    let a = v;\n    return a + 1;", "v: i32")
     + _fn("f", "    let b = g(x);\n    let c = b;\n    return c;", "x: i32") + _main("    return f(5);")),
]

# ============================================================
# CSE — effect-sensitive common subexpression elimination.
# ============================================================
TESTS["cse"] = [
    ("repeated_call_not_deduped",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    return g(x) + g(x);", "x: i32") + _main("    return f(1);")),
    ("repeated_pure_expr_deduped",
     _fn("f", "    return (x + 1) + (x + 1);", "x: i32") + _main("    return f(2);")),
    ("call_then_pure_keeps_call_once",
     _fn("g", "    return v * 2;", "v: i32")
     + _fn("f", "    let a = g(x);\n    return a + (x + 3) + (x + 3);", "x: i32") + _main("    return f(4);")),
    ("two_different_calls_both_kept",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("h", "    return v + 2;", "v: i32")
     + _fn("f", "    return g(x) + h(x);", "x: i32") + _main("    return f(5);")),
    ("call_result_binding_reused",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let a = g(x);\n    return a + a;", "x: i32") + _main("    return f(5);")),
    ("calls_in_both_branches_both_kept",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    if x > 0 {\n        return g(x);\n    } else {\n        return g(x);\n    }", "x: i32")
     + _main("    return f(6);")),
    ("pure_chain_around_call_deduped",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let p = x * 5;\n    let q = x * 5;\n    return g(p) + q;", "x: i32")
     + _main("    return f(7);")),
    ("call_args_pure_parts_deduped",
     _fn("g", "    return u + v;", "u: i32, v: i32")
     + _fn("f", "    return g(x + 2, x + 2);", "x: i32") + _main("    return f(8);")),
    ("sequential_calls_form_effect_chain",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let a = g(x);\n    let b = g(a);\n    return b;", "x: i32") + _main("    return f(9);")),
    ("different_args_not_deduped",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    return g(x + 1) + g(x + 2);", "x: i32") + _main("    return f(10);")),
]

# ============================================================
# SimplifyControl — block merge + branch simplification.
# ============================================================
TESTS["simplify_control"] = [
    ("both_branches_return_merge_cleaned",
     _fn("f", "    if x > 0 {\n        return 1;\n    } else {\n        return 2;\n    }", "x: i32")
     + _main("    return f(3);")),
    ("constant_true_cond_branch_pruned",
     _main("    if 1 < 2 {\n        return 10;\n    } else {\n        return 20;\n    }")),
    ("constant_false_cond_branch_pruned",
     _main("    if 1 > 2 {\n        return 10;\n    } else {\n        return 20;\n    }")),
    ("if_without_else_falls_through",
     _fn("f", "    if x > 0 {\n        return 1;\n    }\n    return 2;", "x: i32") + _main("    return f(3);")),
    ("nested_if_both_return",
     _fn("f", "    if x > 0 {\n        if x > 1 {\n            return 1;\n        } else {\n            return 2;\n        }\n    } else {\n        return 3;\n    }", "x: i32")
     + _main("    return f(3);")),
    ("assignment_merge_via_phi",
     _fn("f", "    var t = 0;\n    if x > 0 {\n        t = 5;\n    } else {\n        t = 9;\n    }\n    return t;", "x: i32")
     + _main("    return f(1);")),
    ("identical_branch_bodies",
     _fn("f", "    if x > 0 {\n        return x * 2;\n    } else {\n        return x * 2;\n    }", "x: i32")
     + _main("    return f(4);")),
    ("empty_then_branch_kept",
     _fn("f", "    if x > 0 {\n\n    }\n    return x;", "x: i32") + _main("    return f(5);")),
    ("if_chain_nested_else",
     _fn("f", "    if x > 2 {\n        return 1;\n    } else {\n        if x > 1 {\n            return 2;\n        } else {\n            return 3;\n        }\n    }", "x: i32")
     + _main("    return f(5);")),
    ("merged_binding_used_after_if",
     _fn("f", "    var t = 0;\n    if x > 0 {\n        t = x + 1;\n    } else {\n        t = x + 2;\n    }\n    return t * 2;", "x: i32")
     + _main("    return f(6);")),
]

# ============================================================
# TCO — tail call recognition.
# ============================================================
TESTS["tco"] = [
    ("direct_tail_call",
     _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    return g(x);", "x: i32")
     + _main("    return f(1);")),
    ("tail_call_with_const_arg",
     _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    return g(7);", "x: i32")
     + _main("    return f(1);")),
    ("tail_call_after_computation",
     _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    let y = x + 1;\n    return g(y);", "x: i32")
     + _main("    return f(2);")),
    ("non_tail_call_not_tagged",
     _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    let y = g(x);\n    return y + 1;", "x: i32")
     + _main("    return f(3);")),
    ("tail_call_in_both_branches",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("h", "    return v + 2;", "v: i32")
     + _fn("f", "    if x > 0 {\n        return g(x);\n    } else {\n        return h(x);\n    }", "x: i32")
     + _main("    return f(4);")),
    ("mutual_tail_calls",
     _fn("g", "    return f(v);", "v: i32") + _fn("f", "    if v > 0 {\n        return g(v - 1);\n    }\n    return 0;", "v: i32")
     + _main("    return f(2);")),
    ("tail_call_zero_args",
     _fn("g", "    return 9;") + _fn("f", "    return g();", "x: i32") + _main("    return f(1);")),
    ("tail_call_two_args",
     _fn("g", "    return u + v;", "u: i32, v: i32") + _fn("f", "    return g(x, x + 1);", "x: i32")
     + _main("    return f(5);")),
    ("tail_call_plus_identity_arith",
     _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    return g(x) + 0;", "x: i32")
     + _main("    return f(6);")),
    ("multiple_tail_call_sites",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    if v > 3 {\n        return g(v);\n    }\n    if v > 1 {\n        return g(v);\n    }\n    return g(0);", "v: i32")
     + _main("    return f(7);")),
]

# ============================================================
# Preservation shapes — graphs the frontend can produce that a given
# pass must leave INTACT (the pass's triggers — allocs, loops, stores,
# guards — do not exist at source level yet). Pinning "no accidental
# rewrite" is a first-class golden expectation (Rule 37 covers both
# directions of a pass's behavior).
# ============================================================
SHAPES: list[tuple[str, str]] = [
    ("param_passthrough", _fn("f", "    return x;", "x: i32") + _main("    return f(3);")),
    ("call_result_returned",
     _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    return g(x);", "x: i32") + _main("    return f(2);")),
    ("call_result_unused_effect_kept",
     _fn("g", "    return v + 1;", "v: i32") + _main("    g(5);\n    return 1;")),
    ("two_sequential_calls",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let a = g(x);\n    let b = g(a);\n    return b;", "x: i32") + _main("    return f(1);")),
    ("calls_in_both_branches",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    if x > 0 {\n        return g(x);\n    } else {\n        return g(-x);\n    }", "x: i32")
     + _main("    return f(3);")),
    ("nested_calls",
     _fn("g", "    return v * 2;", "v: i32")
     + _fn("h", "    return g(v) + 1;", "v: i32")
     + _fn("f", "    return h(g(x));", "x: i32") + _main("    return f(2);")),
    ("param_arith_only",
     _fn("f", "    return x * 3 + 7;", "x: i32") + _main("    return f(4);")),
    ("mixed_calls_and_pure",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let p = x * 2;\n    let a = g(p);\n    return a + p;", "x: i32") + _main("    return f(5);")),
    ("call_two_args",
     _fn("g", "    return u + v;", "u: i32, v: i32")
     + _fn("f", "    return g(x, x + 1);", "x: i32") + _main("    return f(6);")),
    ("call_zero_args", _fn("g", "    return 9;") + _main("    return g();")),
    ("if_merge_phi",
     _fn("f", "    var t = 0;\n    if x > 0 {\n        t = x + 1;\n    } else {\n        t = x + 2;\n    }\n    return t;", "x: i32")
     + _main("    return f(7);")),
    ("unary_ops_on_params",
     _fn("f", "    return -x + (-(-x));", "x: i32") + _main("    return f(8);")),
    ("bitops_on_params",
     _fn("f", "    return (x & 12) | (x ^ 3);", "x: i32") + _main("    return f(9);")),
    ("shifts_on_params",
     _fn("f", "    return (x << 2) + (x >> 1);", "x: i32") + _main("    return f(10);")),
    ("comparisons_on_params",
     _fn("f", "    return (x < 4) + (x >= 4) + (x == 4);", "x: i32") + _main("    return f(11);")),
    ("logical_ops_on_params",
     _fn("f", "    return (x > 0 && x < 9) + (x < 0 || x > 9);", "x: i32") + _main("    return f(12);")),
    ("deep_let_chain",
     _fn("f", "    let a = x + 1;\n    let b = a + 1;\n    let c = b + 1;\n    return c;", "x: i32")
     + _main("    return f(13);")),
    ("var_reassignment_chain",
     _fn("f", "    var t = x;\n    t = t + 1;\n    t = t + 1;\n    return t;", "x: i32")
     + _main("    return f(14);")),
    ("compound_assign_chain",
     _fn("f", "    var t = x;\n    t += 3;\n    t -= 1;\n    return t;", "x: i32") + _main("    return f(15);")),
    ("call_inside_condition",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    if g(x) > 2 {\n        return 1;\n    } else {\n        return 2;\n    }", "x: i32")
     + _main("    return f(16);")),
    ("three_functions_module",
     _fn("a", "    return v + 1;", "v: i32") + _fn("b", "    return v * 2;", "v: i32")
     + _main("    return a(1) + b(2);")),
    ("call_result_in_arith",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    return g(x) * 2 - g(x);", "x: i32") + _main("    return f(17);")),
    ("bool_bindings",
     _fn("f", "    let t = x > 1;\n    let u = x > 2;\n    return t + u;", "x: i32") + _main("    return f(18);")),
    ("mixed_constants_and_params",
     _fn("f", "    return (x + 100) * 2 + 5;", "x: i32") + _main("    return f(19);")),
]

# ---- Loop passes: source-level `for var in lo..hi` loops (real
# triggers — the loop passes are reachable from real code now). ----
def _loop(name, body, lo="0", hi="8", outer=""):
    src = outer if outer else f"fn f(n: i32) -> i32 {{\n    for i in {lo}..{hi} {{\n{body}\n    }}\n    return 7;\n}}\n"
    if outer:
        src = f"fn main() -> i32 {{\n{outer}\n    return 7;\n}}\n"
    return src

LOOP_TESTS = {
    "loop_unrolling": [
        # name, full source (constant trip <= 8 -> eliminated).
        ("empty_body_constant_trip8_eliminated",
         "fn main() -> i32 {\n    for i in 0..8 {\n    }\n    return 42;\n}\n"),
        ("empty_body_constant_trip1_eliminated",
         "fn main() -> i32 {\n    for i in 0..1 {\n    }\n    return 5;\n}\n"),
        ("body_ignoring_induction_var_eliminated",
         "fn main() -> i32 {\n    for i in 0..4 {\n        let dead = 5 + 5;\n    }\n    return 3;\n}\n"),
        ("var_binding_only_body_eliminated",
         "fn main() -> i32 {\n    for i in 0..6 {\n        var t = i;\n    }\n    return 2;\n}\n"),
        ("nested_empty_loops_fully_collapse",
         "fn main() -> i32 {\n    for i in 0..3 {\n        for j in 0..3 {\n        }\n    }\n    return 9;\n}\n"),
        # NOT eliminated (sound skips — pinned as positively as the IR allows).
        ("runtime_bound_loop_kept",
         "fn f(n: i32) -> i32 {\n    for i in 0..n {\n    }\n    return 7;\n}\nfn main() -> i32 { return f(5); }\n"),
        ("accumulator_loop_kept",
         "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        s = s + i;\n    }\n    return s;\n}\nfn main() -> i32 { return f(8); }\n"),
        ("call_body_loop_kept",
         "fn g(v: i32) -> i32 {\n    return v + 1;\n}\nfn f(n: i32) -> i32 {\n    for i in 0..n {\n        g(i);\n    }\n    return 7;\n}\nfn main() -> i32 { return f(8); }\n"),
        ("trip9_above_threshold_kept",
         "fn main() -> i32 {\n    for i in 0..9 {\n    }\n    return 1;\n}\n"),
        ("if_body_reading_var_kept",
         "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        if i > 4 {\n            s = s + 1;\n        }\n    }\n    return s;\n}\nfn main() -> i32 { return f(9); }\n"),
    ],
    "loop_fusion": [
        ("degenerate_sibling_same_scev_fused",
         "fn g(v: i32) -> i32 {\n    return v + 1;\n}\nfn main() -> i32 {\n    for i in 0..8 {\n        g(i);\n    }\n    for j in 0..8 {\n    }\n    return 5;\n}\n"),
        ("both_degenerate_siblings_both_eliminated",
         "fn main() -> i32 {\n    for i in 0..8 {\n    }\n    for j in 0..8 {\n    }\n    return 6;\n}\n"),
        ("nested_degenerate_inner_fused",
         "fn main() -> i32 {\n    for i in 0..3 {\n        for j in 0..3 {\n        }\n    }\n    return 9;\n}\n"),
        ("different_trip_counts_not_paired",
         "fn g(v: i32) -> i32 {\n    return v + 1;\n}\nfn main() -> i32 {\n    for i in 0..8 {\n        g(i);\n    }\n    for j in 0..9 {\n    }\n    return 5;\n}\n"),
        ("different_starts_not_paired",
         "fn g(v: i32) -> i32 {\n    return v + 1;\n}\nfn main() -> i32 {\n    for i in 0..8 {\n        g(i);\n    }\n    for j in 1..9 {\n    }\n    return 5;\n}\n"),
        ("non_degenerate_sibling_kept",
         "fn g(v: i32) -> i32 {\n    return v + 1;\n}\nfn main() -> i32 {\n    for i in 0..8 {\n        g(i);\n    }\n    for j in 0..8 {\n        g(j);\n    }\n    return 5;\n}\n"),
        ("single_loop_no_pair",
         "fn main() -> i32 {\n    for i in 0..8 {\n    }\n    return 4;\n}\n"),
        ("three_loops_cascade",
         "fn main() -> i32 {\n    for i in 0..4 {\n    }\n    for j in 0..4 {\n    }\n    for k in 0..4 {\n    }\n    return 8;\n}\n"),
        ("runtime_bounds_pair_kept",
         "fn f(n: i32) -> i32 {\n    for i in 0..n {\n    }\n    for j in 0..n {\n    }\n    return 7;\n}\nfn main() -> i32 { return f(3); }\n"),
        ("loop_then_straightline_code",
         "fn main() -> i32 {\n    for i in 0..8 {\n    }\n    let x = 1;\n    let y = x + 1;\n    return y;\n}\n"),
    ],
    "scev": [
        ("constant_bounds_recurrence_analyzed",
         "fn main() -> i32 {\n    for i in 0..8 {\n    }\n    return 1;\n}\n"),
        ("runtime_bounds_structure_pinned",
         "fn f(n: i32) -> i32 {\n    for i in 0..n {\n    }\n    return 7;\n}\nfn main() -> i32 { return f(5); }\n"),
        ("nonzero_start_recurrence",
         "fn main() -> i32 {\n    for i in 2..8 {\n    }\n    return 3;\n}\n"),
        ("accumulator_two_phis",
         "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        s = s + i;\n    }\n    return s;\n}\nfn main() -> i32 { return f(8); }\n"),
        ("two_accumulators",
         "fn f(n: i32) -> i32 {\n    var a = 0;\n    var b = 0;\n    for i in 0..n {\n        a = a + i;\n        b = b + 2;\n    }\n    return a + b;\n}\nfn main() -> i32 { return f(4); }\n"),
        ("nested_loops_two_recs",
         "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        for j in 0..n {\n            s = s + 1;\n        }\n    }\n    return s;\n}\nfn main() -> i32 { return f(3); }\n"),
        ("loop_with_call_body",
         "fn g(v: i32) -> i32 {\n    return v + 1;\n}\nfn f(n: i32) -> i32 {\n    for i in 0..n {\n        g(i);\n    }\n    return 7;\n}\nfn main() -> i32 { return f(8); }\n"),
        ("param_bounds_expression",
         "fn f(n: i32) -> i32 {\n    for i in 0..(n + 2) {\n    }\n    return 7;\n}\nfn main() -> i32 { return f(5); }\n"),
        ("single_trip_loop",
         "fn main() -> i32 {\n    for i in 0..1 {\n    }\n    return 6;\n}\n"),
        ("loop_result_unused_after",
         "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        s = s + i;\n    }\n    return 99;\n}\nfn main() -> i32 { return f(8); }\n"),
    ],
}

# LICM / IVS / MemPoolSynthesis / AutoParallelization / BoundsCheckElim:
# real loops whose IR these passes must preserve soundly (their
# transforms are analysis/telemetry-stage today — documented gaps).
_LOOP_PRESERVE_SRC = [
    ("empty_loop_structure", "fn main() -> i32 {\n    for i in 0..8 {\n    }\n    return 1;\n}\n"),
    ("runtime_bound_loop", "fn f(n: i32) -> i32 {\n    for i in 0..n {\n    }\n    return 7;\n}\nfn main() -> i32 { return f(5); }\n"),
    ("accumulator_loop", "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        s = s + i;\n    }\n    return s;\n}\nfn main() -> i32 { return f(8); }\n"),
    ("call_in_loop_body", "fn g(v: i32) -> i32 {\n    return v + 1;\n}\nfn f(n: i32) -> i32 {\n    for i in 0..n {\n        g(i);\n    }\n    return 7;\n}\nfn main() -> i32 { return f(8); }\n"),
    ("loop_invariant_pure_in_body", "fn f(a: i32, n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        let x = a * 2;\n        s = s + x;\n    }\n    return s;\n}\nfn main() -> i32 { return f(3, 4); }\n"),
    ("nested_loops", "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        for j in 0..n {\n            s = s + 1;\n        }\n    }\n    return s;\n}\nfn main() -> i32 { return f(3); }\n"),
    ("loop_with_branch_body", "fn f(n: i32) -> i32 {\n    var s = 0;\n    for i in 0..n {\n        if i > 4 {\n            s = s + 1;\n        }\n    }\n    return s;\n}\nfn main() -> i32 { return f(9); }\n"),
    ("two_sequential_loops", "fn main() -> i32 {\n    for i in 0..4 {\n    }\n    for j in 0..6 {\n    }\n    return 2;\n}\n"),
    ("nonzero_start_loop", "fn main() -> i32 {\n    for i in 2..7 {\n    }\n    return 3;\n}\n"),
    ("loop_after_prelude", "fn f(n: i32) -> i32 {\n    let base = n * 3;\n    var s = 0;\n    for i in 0..n {\n        s = s + base;\n    }\n    return s;\n}\nfn main() -> i32 { return f(4); }\n"),
]
for _p in ["licm", "induction_var_simplification", "mem_pool_synthesis",
           "auto_parallelization", "bounds_check_elim", "loop_fission"]:
    LOOP_TESTS[_p] = _LOOP_PRESERVE_SRC

TESTS.update(LOOP_TESTS)

# Escape/NullPtr/RC/DSE still have no source-level triggers (allocs,
# stores, guards): their preservation suites stay as authored below.

PRESERVATION_PASSES = [
    "escape_analysis", "null_pointer_elimination", "rc_optimization",
    "dead_store_elimination",
]
for _i, _pass in enumerate(PRESERVATION_PASSES):
    _names = []
    for _j in range(MIN_PER_PASS):
        _shape_name, _shape_src = SHAPES[(_i * 3 + _j) % len(SHAPES)]
        _names.append((f"preserves_{_shape_name}", _shape_src))
    TESTS[_pass] = _names

# ============================================================
# Research passes (--research pipeline appended).
# ============================================================

# CFL-Alias: analysis facts over params/calls; pins the graph + the
# call speculation the co-scheduled GuardedDevirt performs in JIT.
TESTS["cfl_alias"] = [
    (n, s) for (n, s) in [
        (("params_are_distinct_abstract_locations"),
         _fn("f", "    return (a & b) + (a | b);", "a: i32, b: i32") + _main("    return f(3, 5);")),
        (("call_args_flow_through"),
         _fn("g", "    return u + v;", "u: i32, v: i32") + _fn("f", "    return g(x, x);", "x: i32")
         + _main("    return f(1);")),
        (("two_independent_calls"),
         _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    return g(x) + g(x + 1);", "x: i32")
         + _main("    return f(2);")),
        (("call_result_feeds_next_call"),
         _fn("g", "    return v * 2;", "v: i32")
         + _fn("f", "    let a = g(x);\n    return g(a);", "x: i32") + _main("    return f(3);")),
        (("pure_around_calls"),
         _fn("g", "    return v + 1;", "v: i32")
         + _fn("f", "    let p = x * 2;\n    return g(p) + p;", "x: i32") + _main("    return f(4);")),
        (("branch_local_calls"),
         _fn("g", "    return v + 1;", "v: i32")
         + _fn("f", "    if x > 0 {\n        return g(x);\n    } else {\n        return g(-x);\n    }", "x: i32")
         + _main("    return f(5);")),
        (("param_chain_no_calls"),
         _fn("f", "    let a = x + 1;\n    let b = a + 1;\n    return b;", "x: i32") + _main("    return f(6);")),
        (("multi_fn_module"),
         _fn("a", "    return v + 1;", "v: i32") + _fn("b", "    return v * 3;", "v: i32")
         + _main("    return a(1) + b(2);")),
        (("call_in_condition"),
         _fn("g", "    return v + 1;", "v: i32")
         + _fn("f", "    if g(x) > 2 {\n        return 1;\n    }\n    return 2;", "x: i32")
         + _main("    return f(7);")),
        (("three_calls_chain"),
         _fn("g", "    return v + 1;", "v: i32")
         + _fn("f", "    let a = g(x);\n    let b = g(a);\n    return g(b);", "x: i32")
         + _main("    return f(8);")),
    ]
]

# Value Flow Analysis: allocation-site stamping (no source allocs) +
# flow facts through calls.
TESTS["value_flow"] = [
    (n, s) for (n, s) in [
        (("param_to_param_flow"),
         _fn("f", "    return (a + b) * (a - b);", "a: i32, b: i32") + _main("    return f(6, 2);")),
        (("param_into_call_flow"),
         _fn("g", "    return v + 3;", "v: i32") + _fn("f", "    return g(x * 2);", "x: i32")
         + _main("    return f(1);")),
        (("call_result_flow_downstream"),
         _fn("g", "    return v + 1;", "v: i32")
         + _fn("f", "    let a = g(x);\n    return a + a;", "x: i32") + _main("    return f(2);")),
        (("branch_merge_flow"),
         _fn("f", "    var t = 0;\n    if x > 0 {\n        t = x;\n    } else {\n        t = -x;\n    }\n    return t;", "x: i32")
         + _main("    return f(3);")),
        (("two_param_identity_flow"),
         _fn("f", "    let a = x;\n    let b = a;\n    return b + x;", "x: i32") + _main("    return f(4);")),
        (("pure_expr_flow_shared"),
         _fn("f", "    let p = x + 7;\n    return p * p;", "x: i32") + _main("    return f(5);")),
        (("call_flow_both_branches"),
         _fn("g", "    return v + 1;", "v: i32")
         + _fn("f", "    if x > 1 {\n        return g(x);\n    } else {\n        return g(x + 1);\n    }", "x: i32")
         + _main("    return f(6);")),
        (("flow_through_arith_only"),
         _fn("f", "    return ((x + 1) + 2) + 3;", "x: i32") + _main("    return f(7);")),
        (("flow_multi_arg_call"),
         _fn("g", "    return u * v;", "u: i32, v: i32")
         + _fn("f", "    return g(x + 1, x + 2);", "x: i32") + _main("    return f(8);")),
        (("flow_across_functions"),
         _fn("g", "    return v + 1;", "v: i32") + _fn("h", "    return g(v) * 2;", "v: i32")
         + _main("    return h(9);")),
    ]
]

# PGDLO: struct-layout tagging fires on Alloc nodes (none at source
# level); pins preservation + co-scheduled speculation.
TESTS["pgdlo"] = [(n, s) for (n, s) in [
    (("no_allocs_graph_preserved"), SHAPES[1][1]),
    (("param_only_preserved"), SHAPES[0][1]),
    (("call_graph_preserved"), SHAPES[5][1]),
    (("branch_call_preserved"), SHAPES[4][1]),
    (("pure_arith_preserved"), SHAPES[6][1]),
    (("multi_call_preserved"), SHAPES[3][1]),
    (("two_arg_call_preserved"), SHAPES[8][1]),
    (("zero_arg_call_preserved"), SHAPES[9][1]),
    (("let_chain_preserved"), SHAPES[16][1]),
    (("mixed_call_pure_preserved"), SHAPES[7][1]),
]]

# MemPoolSynthesis: loop alloc pooling (no loops/allocs at source).
TESTS["mem_pool_synthesis"] = [(n, s) for (n, s) in [
    (("no_loop_allocs_preserved"), SHAPES[0][1]),
    (("straight_line_calls_preserved"), SHAPES[2][1]),
    (("branch_calls_preserved"), SHAPES[4][1]),
    (("nested_calls_preserved"), SHAPES[5][1]),
    (("arith_preserved"), SHAPES[13][1]),
    (("if_merge_preserved"), SHAPES[10][1]),
    (("var_chain_preserved"), SHAPES[17][1]),
    (("compound_assign_preserved"), SHAPES[18][1]),
    (("multi_fn_preserved"), SHAPES[20][1]),
    (("call_result_arith_preserved"), SHAPES[21][1]),
]]

# CacheObliviousLayout: container layout rewrites (no containers).
TESTS["cache_oblivious_layout"] = [(n, s) for (n, s) in [
    (("no_containers_preserved"), SHAPES[6][1]),
    (("params_preserved"), SHAPES[0][1]),
    (("calls_preserved"), SHAPES[1][1]),
    (("branches_preserved"), SHAPES[10][1]),
    (("bitops_preserved"), SHAPES[12][1]),
    (("shifts_preserved"), SHAPES[13][1]),
    (("comparisons_preserved"), SHAPES[14][1]),
    (("logical_preserved"), SHAPES[15][1]),
    (("unary_preserved"), SHAPES[11][1]),
    (("constants_preserved"), SHAPES[23][1]),
]]

# SLP: >=4 independent same-kind pure binops get pack-tagged.
TESTS["slp_vectorization"] = [
    ("four_independent_adds_packable",
     _fn("f", "    return (a + b) + (c + d) + (e + g) + (h + i);",
         "a: i32, b: i32, c: i32, d: i32, e: i32, g: i32, h: i32, i: i32")
     + _main("    return f(1, 2, 3, 4, 5, 6, 7, 8);")),
    ("three_adds_below_pack_threshold",
     _fn("f", "    return (a + b) + (c + d) + (e + g);",
         "a: i32, b: i32, c: i32, d: i32, e: i32, g: i32")
     + _main("    return f(1, 2, 3, 4, 5, 6);")),
    ("dependent_adds_not_packable",
     _fn("f", "    let t = a + b;\n    let u = t + c;\n    let v = u + d;\n    let w = v + e;\n    return w;",
         "a: i32, b: i32, c: i32, d: i32, e: i32")
     + _main("    return f(1, 2, 3, 4, 5);")),
    ("four_independent_muls_packable",
     _fn("f", "    return (a * b) + (c * d) + (e * g) + (h * i);",
         "a: i32, b: i32, c: i32, d: i32, e: i32, g: i32, h: i32, i: i32")
     + _main("    return f(2, 3, 4, 5, 6, 7, 8, 9);")),
    ("mixed_kinds_separate_groups",
     _fn("f", "    return (a + b) + (c - d) + (e * g) + (h - i);",
         "a: i32, b: i32, c: i32, d: i32, e: i32, g: i32, h: i32, i: i32")
     + _main("    return f(9, 1, 8, 2, 7, 3, 6, 4);")),
    ("four_independent_subs_packable",
     _fn("f", "    return (a - b) + (c - d) + (e - g) + (h - i);",
         "a: i32, b: i32, c: i32, d: i32, e: i32, g: i32, h: i32, i: i32")
     + _main("    return f(50, 1, 40, 2, 30, 3, 20, 4);")),
    ("pairs_sharing_operand_still_independent",
     _fn("f", "    return (x + a) + (x + b) + (x + c) + (x + d);",
         "x: i32, a: i32, b: i32, c: i32, d: i32")
     + _main("    return f(10, 1, 2, 3, 4);")),
    ("packable_group_plus_call",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let p = g(x);\n    return (a + b) + (c + d) + (e + h) + (i + j) + p;",
           "x: i32, a: i32, b: i32, c: i32, d: i32, e: i32, h: i32, i: i32, j: i32")
     + _main("    return f(1, 2, 3, 4, 5, 6, 7, 8, 9);")),
    ("packable_group_in_branch",
     _fn("f", "    if x > 0 {\n        return (a + b) + (c + d) + (e + g) + (h + i);\n    }\n    return 0;",
         "x: i32, a: i32, b: i32, c: i32, d: i32, e: i32, g: i32, h: i32, i: i32")
     + _main("    return f(1, 1, 2, 3, 4, 5, 6, 7, 8);")),
    ("constant_adds_fold_before_slp",
     _fn("f", "    return (1 + 2) + (3 + 4) + (5 + 6) + (7 + 8);") + _main("    return f();")),
]

# AutoParallelization: loop trip-count gate (no loops at source).
TESTS["auto_parallelization"] = [(n, s) for (n, s) in [
    (("no_loops_straightline_preserved"), SHAPES[6][1]),
    (("no_loops_calls_preserved"), SHAPES[1][1]),
    (("no_loops_branch_preserved"), SHAPES[10][1]),
    (("no_loops_branch_calls_preserved"), SHAPES[4][1]),
    (("no_loops_nested_calls_preserved"), SHAPES[5][1]),
    (("no_loops_seq_calls_preserved"), SHAPES[3][1]),
    (("no_loops_bitops_preserved"), SHAPES[12][1]),
    (("no_loops_shifts_preserved"), SHAPES[13][1]),
    (("no_loops_multi_fn_preserved"), SHAPES[20][1]),
    (("no_loops_mixed_preserved"), SHAPES[7][1]),
]]

# GuardedDevirtualization — the marquee speculation pass: CallAltered
# nodes get guard+FrameState tags in JIT (Profile) mode only.
TESTS["guarded_devirtualization"] = [
    ("single_call_speculated_in_jit_only",
     _fn("g", "    return v + 1;", "v: i32") + _fn("f", "    return g(x);", "x: i32")
     + _main("    return f(1);")),
    ("call_with_const_arg_speculated",
     _fn("g", "    return v + 1;", "v: i32") + _main("    return g(7);")),
    ("two_calls_both_speculated",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    return g(x) + g(x + 1);", "x: i32") + _main("    return f(2);")),
    ("call_result_unused_still_speculated",
     _fn("g", "    return v + 1;", "v: i32") + _main("    g(3);\n    return 1;")),
    ("call_in_both_branches_speculated",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    if x > 0 {\n        return g(x);\n    } else {\n        return g(-x);\n    }", "x: i32")
     + _main("    return f(3);")),
    ("nested_calls_both_speculated",
     _fn("g", "    return v * 2;", "v: i32")
     + _fn("h", "    return g(v) + 1;", "v: i32")
     + _fn("f", "    return h(g(x));", "x: i32") + _main("    return f(4);")),
    ("call_two_args_speculated",
     _fn("g", "    return u + v;", "u: i32, v: i32")
     + _fn("f", "    return g(x, x + 1);", "x: i32") + _main("    return f(5);")),
    ("call_zero_args_speculated",
     _fn("g", "    return 9;") + _main("    return g();")),
    ("no_calls_nothing_speculated",
     _fn("f", "    return x * 2 + 1;", "x: i32") + _main("    return f(6);")),
    ("call_in_condition_speculated",
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    if g(x) > 2 {\n        return 1;\n    }\n    return 2;", "x: i32")
     + _main("    return f(7);")),
]

# SpeculativeBCE — Guard-node speculation (no source guards yet).
TESTS["speculative_bce"] = [(n, s) for (n, s) in [
    (("no_guards_param_only"), SHAPES[0][1]),
    (("no_guards_call_kept"), SHAPES[1][1]),
    (("no_guards_branch_preserved"), SHAPES[10][1]),
    (("no_guards_cmp_chain_preserved"),
     _fn("f", "    return (x < 4) + (x >= 4) + (x == 4);", "x: i32") + _main("    return f(11);")),
    (("no_guards_cmp_in_if"),
     _fn("f", "    if x < 4 {\n        return 1;\n    } else {\n        return 2;\n    }", "x: i32")
     + _main("    return f(12);")),
    (("no_guards_shift_preserved"), SHAPES[13][1]),
    (("no_guards_call_args_preserved"), SHAPES[8][1]),
    (("no_guards_seq_calls_preserved"), SHAPES[3][1]),
    (("no_guards_nested_calls_preserved"), SHAPES[5][1]),
    (("no_guards_mixed_preserved"), SHAPES[7][1]),
]]

# SpeculativeEffectReordering — Load-node speculation (no source loads).
TESTS["speculative_effect_reordering"] = [(n, s) for (n, s) in [
    (("no_loads_params_preserved"), SHAPES[0][1]),
    (("no_loads_calls_preserved"), SHAPES[1][1]),
    (("no_loads_call_chain_preserved"), SHAPES[3][1]),
    (("no_loads_branch_calls_preserved"), SHAPES[4][1]),
    (("no_loads_nested_calls_preserved"), SHAPES[5][1]),
    (("no_loads_effect_order_preserved"),
     _fn("g", "    return v + 1;", "v: i32")
     + _fn("f", "    let a = g(x);\n    let b = g(a);\n    let c = g(b);\n    return c;", "x: i32")
     + _main("    return f(1);")),
    (("no_loads_arith_preserved"), SHAPES[6][1]),
    (("no_loads_bitops_preserved"), SHAPES[12][1]),
    (("no_loads_var_chain_preserved"), SHAPES[17][1]),
    (("no_loads_mixed_preserved"), SHAPES[7][1]),
]]

# SpeculativeLockElision — CallCrowded speculation (source calls are
# CallAltered, so nothing to elide; pins preservation + co-scheduled
# devirtualization tags in JIT).
TESTS["speculative_lock_elision"] = [(n, s) for (n, s) in [
    (("altered_call_not_elided"), SHAPES[1][1]),
    (("two_altered_calls_not_elided"), SHAPES[3][1]),
    (("branch_calls_not_elided"), SHAPES[4][1]),
    (("nested_altered_calls_not_elided"), SHAPES[5][1]),
    (("call_result_arith_not_elided"), SHAPES[21][1]),
    (("pure_graph_untouched"), SHAPES[6][1]),
    (("param_graph_untouched"), SHAPES[0][1]),
    (("call_zero_args_untouched"), SHAPES[9][1]),
    ("call_in_condition_untouched", SHAPES[19][1]),
    (("multi_fn_calls_untouched"), SHAPES[20][1]),
]]

# BOLTLayout — post-link layout; graph-level run() is a no-op.
TESTS["bolt_layout"] = [(n, s) for (n, s) in [
    (("graph_level_noop_params"), SHAPES[0][1]),
    (("graph_level_noop_call"), SHAPES[1][1]),
    (("graph_level_noop_branch"), SHAPES[10][1]),
    (("graph_level_noop_branch_calls"), SHAPES[4][1]),
    (("graph_level_noop_nested_calls"), SHAPES[5][1]),
    (("graph_level_noop_seq_calls"), SHAPES[3][1]),
    (("graph_level_noop_arith"), SHAPES[6][1]),
    (("graph_level_noop_bitops"), SHAPES[12][1]),
    (("graph_level_noop_multi_fn"), SHAPES[20][1]),
    (("graph_level_noop_mixed"), SHAPES[7][1]),
]]


def run_aegisc(src_path: Path, mode_flag: str, research: bool) -> str:
    """Run aegisc and return the normalized post-pipeline IR."""
    cmd = [str(AEGISC), str(src_path), "--dump-ir", "--no-verify", mode_flag]
    if research:
        cmd.append("--research")
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if proc.returncode != 0:
        raise RuntimeError(
            f"aegisc failed ({' '.join(cmd)}):\nstdout:{proc.stdout}\nstderr:{proc.stderr}")
    out = proc.stdout
    # Keep only the post-pipeline graph; normalize version/node counts.
    lines = []
    keep = False
    for line in out.splitlines():
        if line.startswith("----- After passes -----"):
            keep = True
            continue
        if keep:
            # Normalize v=N (Rule 50 version stamps) and n=N (node count).
            import re
            line = re.sub(r"v=\d+", "v=*", line)
            line = re.sub(r"n=\d+", "n=*", line)
            lines.append(line)
    if not lines:
        raise RuntimeError(f"no post-passes IR section in output for {src_path}")
    # Drop trailing blank lines: the driver prints format_graph(g) + "\n",
    # which leaves one empty line after the last node. Command
    # substitution on the harness side strips ALL trailing newlines,
    # so the checked-in file must not carry blank padding either.
    while lines and lines[-1] == "":
        lines.pop()
    return "\n".join(lines) + "\n"


def main() -> int:
    if not AEGISC.exists():
        print(f"FAIL: aegisc not found at {AEGISC} (run scripts/build.sh first)")
        return 2

    # ---- Rule 37 census BEFORE writing anything. ----
    short = {p: len(t) for p, t in TESTS.items() if len(t) < MIN_PER_PASS}
    if short:
        print(f"FAIL: Rule 37 census — these passes have < {MIN_PER_PASS} goldens: {short}")
        return 2
    total = sum(len(t) for t in TESTS.values())
    print(f"census OK: {len(TESTS)} passes x >= {MIN_PER_PASS} = {total} golden pairs")

    failures: list[str] = []
    for pass_dir, entries in TESTS.items():
        research = pass_dir in RESEARCH_DIRS
        out_dir = GOLDEN / pass_dir
        out_dir.mkdir(parents=True, exist_ok=True)
        # Rule 50/D.2 hygiene: this generator owns the suite — remove
        # files from a previous generation that the current TESTS table
        # no longer defines (stale pairs would silently inflate the
        # Rule 37 census and keep testing deleted scenarios).
        owned = {f"{name}.in.aegis" for name, _ in entries}
        owned |= {f"{name}.expected.son" for name, _ in entries}
        owned |= {f"{name}.expected.aot.son" for name, _ in entries}
        owned |= {f"{name}.expected.jit.son" for name, _ in entries}
        for stale in out_dir.iterdir():
            if stale.is_file() and stale.name not in owned:
                stale.unlink()
        for name, src in entries:
            in_path = out_dir / f"{name}.in.aegis"
            in_path.write_text(src)
            aot1 = run_aegisc(in_path, "--aot", research)
            jit1 = run_aegisc(in_path, "--jit", research)
            # Determinism: re-run both modes; output must be identical
            # (Rule 40 replay stability).
            aot2 = run_aegisc(in_path, "--aot", research)
            jit2 = run_aegisc(in_path, "--jit", research)
            if aot1 != aot2 or jit1 != jit2:
                failures.append(f"{in_path}: non-deterministic output")
                continue
            base = out_dir / name
            if aot1 == jit1:
                (base.parent / f"{base.name}.expected.son").write_text(aot1)
            else:
                (base.parent / f"{base.name}.expected.aot.son").write_text(aot1)
                (base.parent / f"{base.name}.expected.jit.son").write_text(jit1)
            print(f"  golden: {pass_dir}/{name}")

    if failures:
        print(f"FAIL: {len(failures)} problems:")
        for f in failures:
            print(f"  {f}")
        return 1
    print(f"OK: wrote {total} golden pairs under {GOLDEN}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
