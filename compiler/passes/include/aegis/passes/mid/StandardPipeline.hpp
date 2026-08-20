// passes/Passes_Standard.h — Declares all standard passes.
#pragma once
#include "aegis/passes/Pass.hpp"
#include "aegis/passes/mid/GVN.hpp"
#include "aegis/passes/mid/EDCE.hpp"
#include "aegis/passes/mid/SCCP.hpp"
#include "aegis/passes/mid/SimplifyControl.hpp"

namespace aegis {
// Build the standard AOT pipeline (Rules A.1, B.5, B.6). Returns the
// vector of passes in the order they should run.
std::vector<std::unique_ptr<Pass>> build_standard_pipeline();
} // namespace aegis
