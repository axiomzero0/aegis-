// ============================================================
// ir/NodeKind.h — Enumerated IR node kinds.
// ============================================================
// Law: Rule B.3 — "Use enum class NodeKind for type switching."
//       No RTTI in the IR/backend.
//
// The Aegis IR is a unified Sea-of-Nodes variant ("E-SoN") with three
// effect classes on every value-producing node:
//   Pure    — no observable effects; freely reorderable / CSE-able.
//   Altered — mutates memory; participates in the effect chain.
//   Crowded — synchronizes with the world (I/O, atomics, threads);
//             never reordered past anything.
//
// The IR also has structural nodes:
//   Start, Region, If, Proj, Loop, Branch, Return, Call.
// ============================================================
#pragma once

#include <cstdint>

#include "aegis/support/Assert.hpp"

namespace aegis {

enum class NodeKind : uint16_t {
    // ---------- Structural / control ----------
    Start        = 0,  // single root: produces Control + Effect roots
    Region       = 1,  // merge point for control flow
    Loop         = 2,  // back-edge region (loop header)
    If           = 3,  // branch on a boolean value
    Proj         = 4,  // extract one output of a multi-output node
                       // (If.{true,false}, Start.{ctrl,eff}, Call.{ret,eff}, ...)
    Return       = 5,
    Branch       = 6,  // unconditional jump to a region
    Stop         = 7,  // end of program

    // ---------- Constants / data ----------
    Constant     = 10, // Pure: literal value (i32, i64, f64, bool, ...).
    Parameter    = 11, // Pure: function parameter.
    Phi          = 12, // Pure: phi node at region merge points.
    Add          = 13, // Pure: arithmetic +
    Sub          = 14, // Pure: arithmetic -
    Mul          = 15, // Pure: arithmetic *
    Div          = 16, // Pure: signed /  (trap-free if signed-div-by-zero is checked separately)
    UDiv         = 17, // Pure: unsigned /
    Mod          = 18, // Pure: signed %
    UMod         = 19, // Pure: unsigned %
    And          = 20, // Pure: bitwise &
    Or           = 21, // Pure: bitwise |
    Xor          = 22, // Pure: bitwise ^
    Shl          = 23, // Pure: shift left
    Shr          = 24, // Pure: shift right (signed/arith by operand type)
    LShr         = 25, // Pure: shift right (logical)
    CmpEq        = 26, // Pure: ==
    CmpNe        = 27, // Pure: !=
    CmpLt        = 28, // Pure: signed <
    CmpLe        = 29, // Pure: signed <=
    CmpGt        = 30, // Pure: signed >
    CmpGe        = 31, // Pure: signed >=
    CmpUlt       = 32, // Pure: unsigned <
    CmpUle       = 33, // Pure: unsigned <=
    CmpUgt       = 34, // Pure: unsigned >
    CmpUge       = 35, // Pure: unsigned >=
    Neg          = 36, // Pure: unary negation (-x)
    Not          = 37, // Pure: logical not (!x)
    BitNot       = 49, // Pure: bitwise not (~x). Distinct kind from Not
                       // so constant folding is unambiguous (Rule 69 —
                       // no implicit semantic coercions in the IR).
    Load         = 38, // Altered: read from memory (in the effect chain)
    Store        = 39, // Altered: write to memory (in the effect chain)
    Alloc        = 40, // Altered: heap allocation (escapable via escape analysis)
    StackAlloc   = 41, // Pure: stack allocation (no effect, not escapable)
    GetElementPtr  = 42, // Pure: pointer arithmetic (offset only)
    GetFieldPtr    = 43, // Pure: struct field pointer
    Cast           = 44, // Pure: bitcast / zero-extend / sign-extend
    Select         = 45, // Pure: c ? a : b
    CallPure      = 46, // Pure: call to a Pure callee (no effect)
    CallAltered   = 47, // Altered: call to an Altered callee
    CallCrowded   = 48, // Crowded: call to a Crowded callee (I/O, atomic, ...)

    // ---------- Synchronization / atomic ----------
    AtomicLoad   = 50, // Crowded
    AtomicStore  = 51, // Crowded
    AtomicRMW    = 52, // Crowded: fetch_add, fetch_sub, CAS, ...
    Fence        = 53, // Crowded: memory barrier

    // ---------- Speculation / guards (PGO-driven, JIT) ----------
    Guard        = 60, // Crowded: deoptimize if condition false
    Deopt        = 61, // Crowded: unconditional deoptimization
    FrameState   = 62, // metadata node, never executed — used by deopt
    ProfiledEntry= 63, // PGO data attachment (data node, not effect)

    // ---------- Backend / machine-level (post-IR-lowering) ----------
    MachineOp    = 90, // placeholder until a real machine-level enum exists
};

// Class of effects that a node carries. Pure nodes have no effect class
// and never participate in the effect chain. Altered nodes mutate memory
// and serialize w.r.t. each other. Crowded nodes serialize against
// everything (and deopt paths). Pure nodes may be reordered freely.
enum class EffectClass : uint8_t {
    Pure    = 0,
    Altered = 1,
    Crowded = 2,
};

[[nodiscard]] constexpr const char* node_kind_name(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::Start:        return "Start";
        case NodeKind::Region:       return "Region";
        case NodeKind::Loop:         return "Loop";
        case NodeKind::If:           return "If";
        case NodeKind::Proj:         return "Proj";
        case NodeKind::Return:       return "Return";
        case NodeKind::Branch:       return "Branch";
        case NodeKind::Stop:         return "Stop";
        case NodeKind::Constant:     return "Constant";
        case NodeKind::Parameter:    return "Parameter";
        case NodeKind::Phi:          return "Phi";
        case NodeKind::Add:          return "Add";
        case NodeKind::Sub:          return "Sub";
        case NodeKind::Mul:          return "Mul";
        case NodeKind::Div:          return "Div";
        case NodeKind::UDiv:         return "UDiv";
        case NodeKind::Mod:          return "Mod";
        case NodeKind::UMod:         return "UMod";
        case NodeKind::And:          return "And";
        case NodeKind::Or:           return "Or";
        case NodeKind::Xor:          return "Xor";
        case NodeKind::Shl:          return "Shl";
        case NodeKind::Shr:          return "Shr";
        case NodeKind::LShr:         return "LShr";
        case NodeKind::CmpEq:        return "CmpEq";
        case NodeKind::CmpNe:        return "CmpNe";
        case NodeKind::CmpLt:        return "CmpLt";
        case NodeKind::CmpLe:        return "CmpLe";
        case NodeKind::CmpGt:        return "CmpGt";
        case NodeKind::CmpGe:        return "CmpGe";
        case NodeKind::CmpUlt:       return "CmpUlt";
        case NodeKind::CmpUle:       return "CmpUle";
        case NodeKind::CmpUgt:       return "CmpUgt";
        case NodeKind::CmpUge:       return "CmpUge";
        case NodeKind::Neg:          return "Neg";
        case NodeKind::Not:          return "Not";
        case NodeKind::BitNot:       return "BitNot";
        case NodeKind::Load:         return "Load";
        case NodeKind::Store:        return "Store";
        case NodeKind::Alloc:        return "Alloc";
        case NodeKind::StackAlloc:   return "StackAlloc";
        case NodeKind::GetElementPtr:  return "GetElementPtr";
        case NodeKind::GetFieldPtr:    return "GetFieldPtr";
        case NodeKind::Cast:           return "Cast";
        case NodeKind::Select:         return "Select";
        case NodeKind::CallPure:       return "CallPure";
        case NodeKind::CallAltered:    return "CallAltered";
        case NodeKind::CallCrowded:   return "CallCrowded";
        case NodeKind::AtomicLoad:     return "AtomicLoad";
        case NodeKind::AtomicStore:    return "AtomicStore";
        case NodeKind::AtomicRMW:      return "AtomicRMW";
        case NodeKind::Fence:          return "Fence";
        case NodeKind::Guard:          return "Guard";
        case NodeKind::Deopt:          return "Deopt";
        case NodeKind::FrameState:     return "FrameState";
        case NodeKind::ProfiledEntry:  return "ProfiledEntry";
        case NodeKind::MachineOp:      return "MachineOp";
    }
    AEGIS_UNREACHABLE();
}

// Map a NodeKind to its effect class. Pure nodes are the default;
// Altered nodes are anything that mutates memory or calls an Altered
// function; Crowded nodes are atomics, fences, I/O calls, and guards.
//
// Law: Rule D.3 — switch is exhaustive on the closed enum. New
// NodeKinds MUST be added here or the build will fail.
[[nodiscard]] constexpr EffectClass effect_class_of(NodeKind k) noexcept {
    switch (k) {
        // ---- Altered (memory-mutating) ----
        case NodeKind::Load:
        case NodeKind::Store:
        case NodeKind::Alloc:
        case NodeKind::CallAltered:
            return EffectClass::Altered;
        // ---- Crowded (world-synchronizing) ----
        case NodeKind::CallCrowded:
        case NodeKind::AtomicLoad:
        case NodeKind::AtomicStore:
        case NodeKind::AtomicRMW:
        case NodeKind::Fence:
        case NodeKind::Guard:
        case NodeKind::Deopt:
            return EffectClass::Crowded;
        // ---- Pure (structural + arithmetic + data) ----
        case NodeKind::Start:
        case NodeKind::Region:
        case NodeKind::Loop:
        case NodeKind::If:
        case NodeKind::Proj:
        case NodeKind::Return:
        case NodeKind::Branch:
        case NodeKind::Stop:
        case NodeKind::Constant:
        case NodeKind::Parameter:
        case NodeKind::Phi:
        case NodeKind::Add:
        case NodeKind::Sub:
        case NodeKind::Mul:
        case NodeKind::Div:
        case NodeKind::UDiv:
        case NodeKind::Mod:
        case NodeKind::UMod:
        case NodeKind::And:
        case NodeKind::Or:
        case NodeKind::Xor:
        case NodeKind::Shl:
        case NodeKind::Shr:
        case NodeKind::LShr:
        case NodeKind::CmpEq:
        case NodeKind::CmpNe:
        case NodeKind::CmpLt:
        case NodeKind::CmpLe:
        case NodeKind::CmpGt:
        case NodeKind::CmpGe:
        case NodeKind::CmpUlt:
        case NodeKind::CmpUle:
        case NodeKind::CmpUgt:
        case NodeKind::CmpUge:
        case NodeKind::Neg:
        case NodeKind::Not:
        case NodeKind::BitNot:
        case NodeKind::StackAlloc:
        case NodeKind::GetElementPtr:
        case NodeKind::GetFieldPtr:
        case NodeKind::Cast:
        case NodeKind::Select:
        case NodeKind::CallPure:
        case NodeKind::FrameState:
        case NodeKind::ProfiledEntry:
        case NodeKind::MachineOp:
            return EffectClass::Pure;
    }
    AEGIS_UNREACHABLE();
}

[[nodiscard]] constexpr bool is_pure(NodeKind k) noexcept {
    return effect_class_of(k) == EffectClass::Pure;
}
[[nodiscard]] constexpr bool is_altered(NodeKind k) noexcept {
    return effect_class_of(k) == EffectClass::Altered;
}
[[nodiscard]] constexpr bool is_crowded(NodeKind k) noexcept {
    return effect_class_of(k) == EffectClass::Crowded;
}

} // namespace aegis
