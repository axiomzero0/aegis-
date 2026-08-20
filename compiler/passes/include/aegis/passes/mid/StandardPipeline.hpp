// passes/mid/StandardPipeline.hpp — Build the AOT/JIT standard pipeline.
#pragma once
#include <memory>
#include <vector>
#include "aegis/passes/Pass.hpp"
namespace aegis::passes::mid {
// Build the standard AOT pipeline (Rules A.1, B.5, B.6). Returns the
// vector of passes in the order they should run.
std::vector<std::unique_ptr<Pass>> build_standard_pipeline();
} // namespace aegis::passes::mid
