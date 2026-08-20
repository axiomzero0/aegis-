// passes/Passes_Standard.h — Declares all standard passes.
#pragma once
#include "passes/Pass.h"
#include "passes/GVN.h"
#include "passes/EDCE.h"
#include "passes/SCCP.h"
#include "passes/SimplifyControl.h"

namespace aegis {
// Build the standard AOT pipeline (Rules A.1, B.5, B.6). Returns the
// vector of passes in the order they should run.
std::vector<std::unique_ptr<Pass>> build_standard_pipeline();
} // namespace aegis
