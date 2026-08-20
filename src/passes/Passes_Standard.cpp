// passes/Passes_Standard.cpp — Construct the AOT/JIT pipeline.
#include "passes/Passes_Standard.h"

namespace aegis {

std::vector<std::unique_ptr<Pass>> build_standard_pipeline() {
    std::vector<std::unique_ptr<Pass>> v;
    v.push_back(std::make_unique<SCCPPass>());        // SCCP: constants first
    v.push_back(std::make_unique<GVNPass>());         // GVN: dedup Pure
    v.push_back(std::make_unique<SimplifyControlPass>()); // block-merge + DSE + TCO
    v.push_back(std::make_unique<EDCEPass>());        // E-DCE: sweep last
    return v;
}

} // namespace aegis
