// ============================================================
// common/Primitives.h — Core ID types and basic aliases.
// ============================================================
// Laws implemented here:
//   Rule 53 — Index-based graph, 32-bit NodeId only (never Node*)
//   Rule 54 — Interned symbols (SymbolId = uint32_t)
//   Rule 51 — Bitmasked flags (see Flags<E> in Flags.h)
// ============================================================
#pragma once

#include <cstdint>
#include <limits>

namespace aegis {

// 32-bit index into the Graph's node arena.
//   Rule 53: "Never use raw pointers (Node*) for edges in the Sea of Nodes."
//   4 bytes per edge -> double the node density in L1/L2 vs. raw pointers,
//   and makes the graph trivially serializable + arena-relocatable.
using NodeId = uint32_t;

// Sentinel: invalid/absent node. We reserve 0xFFFFFFFF for "no node".
inline constexpr NodeId kInvalidNodeId = std::numeric_limits<NodeId>::max();

// Sentinel: the conceptual "start" node of the graph (effect & control roots).
inline constexpr NodeId kStartNodeId = 0u;

// Sentinel: a "virtual" node id used by analysis passes to mean "not yet set".
inline constexpr NodeId kUnknownNodeId = kInvalidNodeId - 1;

// 32-bit index into the SymbolTable. String content lives exactly once
// in the table; the IR carries only this 4-byte id.
//   Rule 54: "Never pass, compare, or store std::string in the IR or passes."
using SymbolId = uint32_t;
inline constexpr SymbolId kInvalidSymbolId = std::numeric_limits<SymbolId>::max();

// 32-bit index into the TypeTable.
using TypeId = uint32_t;
inline constexpr TypeId kInvalidTypeId = std::numeric_limits<TypeId>::max();

// 32-bit index for interned constants (used by hash-consing for I-Pure nodes).
using ConstId = uint32_t;
inline constexpr ConstId kInvalidConstId = std::numeric_limits<ConstId>::max();

// 32-bit index for FrameState attachments.
using FrameStateId = uint32_t;
inline constexpr FrameStateId kInvalidFrameStateId = std::numeric_limits<FrameStateId>::max();

// 32-bit index for lifetime regions (affine borrow lifetime scoping).
using RegionId = uint32_t;
inline constexpr RegionId kInvalidRegionId = std::numeric_limits<RegionId>::max();

// 32-bit index for basic blocks (post-IR-lowering structure for backend).
using BlockId = uint32_t;
inline constexpr BlockId kInvalidBlockId = std::numeric_limits<BlockId>::max();

// Compile-time sanity: NodeId fits the "32-bit index" rule.
static_assert(sizeof(NodeId) == 4, "NodeId must be 32-bit (Rule 53).");
static_assert(sizeof(SymbolId) == 4, "SymbolId must be 32-bit (Rule 54).");

} // namespace aegis
