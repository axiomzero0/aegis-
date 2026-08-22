// ============================================================
// backend/MachineInstr.h — A simple target-agnostic MachineInstr.
// ============================================================
// Until a real backend exists, we use a single-string opcode and up to
// 3 integer operands. This is sufficient for the Linear Scan allocator
// to run and produce register assignments.
// ============================================================
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aegis/support/Primitives.hpp"

namespace aegis {

enum class RegClass : uint8_t {
    General = 0, // GPR
    Float   = 1, // FP/SIMD
    Vector  = 2, // SIMD-only
};

// A virtual register id (post-IR-lowering).
using VRegId = uint32_t;
inline constexpr VRegId kInvalidVReg = 0xFFFFFFFFu;

/// Maximum def slots on a single MachineInstr. x86-64 instructions
/// define at most one register, but the array is sized uniformly so
/// instruction selection never branches on operand count (Rule 59:
/// fixed-size SoA-friendly records).
inline constexpr size_t kMaxDefsPerInstr{4};

/// Maximum use slots on a single MachineInstr (e.g. a 3-operand
/// RMW plus a base+index pair). Same uniform-record rationale as
/// kMaxDefsPerInstr.
inline constexpr size_t kMaxUsesPerInstr{4};

// A physical register id (machine-specific; we treat them as opaque ints
// for the prototype). The Linear Scan allocator will assign VReg -> PReg.
using PRegId = uint16_t;

struct MachineInstr {
    std::string op;
    std::array<VRegId, kMaxDefsPerInstr> defs{kInvalidVReg, kInvalidVReg,
                                               kInvalidVReg, kInvalidVReg};
    std::array<VRegId, kMaxUsesPerInstr> uses{kInvalidVReg, kInvalidVReg,
                                               kInvalidVReg, kInvalidVReg};
    RegClass rc{RegClass::General};
    // True when `imm` carries this instruction's immediate operand.
    // Immediates live in a DEDICATED field — they must never be
    // smuggled through a VRegId slot: the register allocator sizes its
    // interval tables by the largest vreg id it sees, so a large
    // immediate masquerading as a vreg id allocated gigabytes and
    // crashed with std::bad_alloc (Rule 62: no "small" data corruption;
    // Rule 73: no fragile encodings).
    bool    has_imm{false};
    int64_t imm{0};

    MachineInstr() = default;
    // NOTE: unset def/use slots are kInvalidVReg, NOT 0 — vreg 0 is a
    // perfectly valid register. Zero-initialized slots made every
    // instruction "use vreg 0", corrupting liveness intervals
    // (Rule 73: sentinel values must not be confusable with valid data).
};

struct MachineFunction {
    std::string            name;
    std::vector<MachineInstr> instrs;
};

} // namespace aegis
