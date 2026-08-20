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

// A physical register id (machine-specific; we treat them as opaque ints
// for the prototype). The Linear Scan allocator will assign VReg -> PReg.
using PRegId = uint16_t;

struct MachineInstr {
    std::string op;
    std::array<VRegId, 4> defs{};
    std::array<VRegId, 4> uses{};
    RegClass rc{RegClass::General};
};

struct MachineFunction {
    std::string            name;
    std::vector<MachineInstr> instrs;
};

} // namespace aegis
