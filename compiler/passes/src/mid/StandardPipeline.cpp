// passes/mid/StandardPipeline.cpp — Construct the AOT/JIT pipeline.
//
// Pipeline order (Rules A.1 — One Pipeline, Two Inputs):
//   1. EscapeAnalysis   — find non-escaping allocs, stack-promote.
//   2. SCCP             — constant fold + propagate (must run early).
//   3. StrengthReduction — replace Mul-by-pow2 with Shl, etc.
//   4. CopyPropagation  — eliminate identity Casts, identical-branch
//                         Select, identical-input Phis.
//   5. GVN              — global value numbering for Pure nodes.
//   6. CSE              — effect-sensitive CSE (load-after-load, etc).
//   7. LICM             — hoist loop-invariant Pure nodes.
//   8. BoundsCheckElim  — remove statically-provable bounds checks.
//   9. DSE              — eliminate dead stores (overwritten before read).
//  10. TCO              — tag tail calls for the backend.
//  11. SimplifyControl  — block merge + jump thread.
//  12. EDCE             — sweep dead nodes (always last).
//
// All passes are idempotent (Rule B.5) and monotone-decreasing in IR
// size (Rule B.6). PassManager runs them to fixpoint.
#include "aegis/passes/mid/StandardPipeline.hpp"

#include "aegis/passes/mid/BoundsCheckElim.hpp"
#include "aegis/passes/mid/CSE.hpp"
#include "aegis/passes/mid/CopyPropagation.hpp"
#include "aegis/passes/mid/DSE.hpp"
#include "aegis/passes/mid/EDCE.hpp"
#include "aegis/passes/mid/EscapeAnalysis.hpp"
#include "aegis/passes/mid/GVN.hpp"
#include "aegis/passes/mid/LICM.hpp"
#include "aegis/passes/mid/SCCP.hpp"
#include "aegis/passes/mid/SimplifyControl.hpp"
#include "aegis/passes/mid/StrengthReduction.hpp"
#include "aegis/passes/mid/TCO.hpp"

namespace aegis::passes::mid {

std::vector<std::unique_ptr<Pass>> build_standard_pipeline() {
    std::vector<std::unique_ptr<Pass>> v;
    v.push_back(std::make_unique<EscapeAnalysisPass>());
    v.push_back(std::make_unique<SCCPPass>());
    v.push_back(std::make_unique<StrengthReductionPass>());
    v.push_back(std::make_unique<CopyPropagationPass>());
    v.push_back(std::make_unique<GVNPass>());
    v.push_back(std::make_unique<CSEPass>());
    v.push_back(std::make_unique<LICMPass>());
    v.push_back(std::make_unique<BoundsCheckElimPass>());
    v.push_back(std::make_unique<DeadStoreElimPass>());
    v.push_back(std::make_unique<TailCallOptPass>());
    v.push_back(std::make_unique<SimplifyControlPass>());
    v.push_back(std::make_unique<EDCEPass>());
    return v;
}

} // namespace aegis::passes::mid
