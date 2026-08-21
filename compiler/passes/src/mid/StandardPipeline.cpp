// passes/mid/StandardPipeline.cpp — Construct the AOT/JIT pipeline.
//
// Pipeline order (Rules A.1 — One Pipeline, Two Inputs):
//
//   1. EscapeAnalysis   - find non-escaping allocs, stack-promote.
//   2. NullPointerElim   - strip null-check guards on Alloc results.
//   3. RCOptimization    - merge redundant RC inc/dec pairs.
//   4. SCCP              - constant fold + propagate (must run early).
//   5. StrengthReduction - replace Mul-by-pow2 with Shl, etc.
//   6. CopyPropagation   - eliminate identity Casts, identical-branch
//                          Select, identical-input Phis.
//   7. GVN               - global value numbering for Pure nodes.
//   8. CSE               - effect-sensitive (load-after-load, etc).
//   9. SCEV              - analyze loop induction variables (analysis pass).
//  10. LICM              - hoist loop-invariant Pure nodes.
//  11. InductionVarSimp  - rewrite induction vars to linear form.
//  12. LoopUnrolling     - duplicate loop bodies (budget-guarded).
//  13. LoopFusion        - merge adjacent loops with same range.
//  14. LoopFission       - split large loops for I-cache density.
//  15. BoundsCheckElim   - remove statically-provable bounds checks.
//  16. DSE               - eliminate dead stores (overwritten before read).
//  17. TCO               - tag tail calls for the backend.
//  18. SimplifyControl   - block merge + jump thread.
//  19. EDCE              - sweep dead nodes (always last).
//
// All passes are idempotent (Rule B.5) and monotone-decreasing in IR
// size (Rule B.6). Passes that may grow the IR (Loop Unrolling,
// Loop Fission, Induction Var Simplification) run inside a guarded
// fixpoint with a strict budget (kLoopUnrollMaxNodeGrowth, etc.).
#include "aegis/passes/mid/StandardPipeline.hpp"

#include "aegis/passes/mid/BoundsCheckElim.hpp"
#include "aegis/passes/mid/CSE.hpp"
#include "aegis/passes/mid/CopyPropagation.hpp"
#include "aegis/passes/mid/DSE.hpp"
#include "aegis/passes/mid/EDCE.hpp"
#include "aegis/passes/mid/EscapeAnalysis.hpp"
#include "aegis/passes/mid/GVN.hpp"
#include "aegis/passes/mid/InductionVarSimplification.hpp"
#include "aegis/passes/mid/LICM.hpp"
#include "aegis/passes/mid/LoopFission.hpp"
#include "aegis/passes/mid/LoopFusion.hpp"
#include "aegis/passes/mid/LoopUnrolling.hpp"
#include "aegis/passes/mid/NullPointerElimination.hpp"
#include "aegis/passes/mid/RCOptimization.hpp"
#include "aegis/passes/mid/SCCP.hpp"
#include "aegis/passes/mid/SCEV.hpp"
#include "aegis/passes/mid/SimplifyControl.hpp"
#include "aegis/passes/mid/StrengthReduction.hpp"
#include "aegis/passes/mid/TCO.hpp"

namespace aegis::passes::mid {

std::vector<std::unique_ptr<Pass>> build_standard_pipeline() {
    std::vector<std::unique_ptr<Pass>> v;
    v.push_back(std::make_unique<EscapeAnalysisPass>());
    v.push_back(std::make_unique<NullPointerEliminationPass>());
    v.push_back(std::make_unique<RCOptimizationPass>());
    v.push_back(std::make_unique<SCCPPass>());
    v.push_back(std::make_unique<StrengthReductionPass>());
    v.push_back(std::make_unique<CopyPropagationPass>());
    v.push_back(std::make_unique<GVNPass>());
    v.push_back(std::make_unique<CSEPass>());
    v.push_back(std::make_unique<SCEVPass>());               // analysis
    v.push_back(std::make_unique<LICMPass>());
    v.push_back(std::make_unique<InductionVarSimplificationPass>());
    v.push_back(std::make_unique<LoopUnrollingPass>());
    v.push_back(std::make_unique<LoopFusionPass>());
    v.push_back(std::make_unique<LoopFissionPass>());
    v.push_back(std::make_unique<BoundsCheckElimPass>());
    v.push_back(std::make_unique<DeadStoreElimPass>());
    v.push_back(std::make_unique<TailCallOptPass>());
    v.push_back(std::make_unique<SimplifyControlPass>());
    v.push_back(std::make_unique<EDCEPass>());
    return v;
}

} // namespace aegis::passes::mid
