// ============================================================
// aegis/ir/NodeShape.hpp — Named constants for IR node input shapes.
// ============================================================
// Laws:
//   Rule 61 (No Hard-Coded Constants in Optimization Logic).
//   Rule D.1 (No Magic Numbers or Unnamed Constants).
//
// Every "magic 3" that appears in pass logic for `n.inputs.size() == 3`
// is actually a reference to a specific IR convention:
//   - Return has 3 inputs: {ctrl, eff, val}.
//   - Select has 3 data inputs: {cond, a, b}.
//   - Phi has 3 inputs: {region, val_then, val_else}.
//   - Store has 2 data inputs: {ptr, val} (4 with ctrl+eff prefix).
//
// Passes should reference these by name, not by raw integer.
// ============================================================
#pragma once

#include <cstdint>

namespace aegis::ir::shape {

/// Return node input count: {ctrl, eff, val}.
constexpr uint32_t kReturnInputs{3};

/// Return node's value-input index (the value being returned).
constexpr uint32_t kReturnValIndex{2};

/// Select node input count: {cond, then_val, else_val}.
constexpr uint32_t kSelectInputs{3};

/// If node input count: {ctrl, cond}.
constexpr uint32_t kIfInputs{2};

/// Phi node input count (for the simple 2-branch case): {region, val_then, val_else}.
constexpr uint32_t kPhiInputs2Branches{3};

/// Store node input count: {ctrl, eff, ptr, val}.
constexpr uint32_t kStoreInputs{4};

/// Load node input count: {ctrl, eff, ptr}.
constexpr uint32_t kLoadInputs{3};

/// Alloc node input count: {ctrl, eff}.
constexpr uint32_t kAllocInputs{2};

/// Index of the data-input slice for Altered/Crowded nodes.
/// inputs[0] = ctrl, inputs[1] = eff, inputs[2..] = data.
constexpr uint32_t kCtrlEffPrefix{2};

/// Index of the condition input in an If node.
constexpr uint32_t kIfCondIndex{1};

} // namespace aegis::ir::shape
