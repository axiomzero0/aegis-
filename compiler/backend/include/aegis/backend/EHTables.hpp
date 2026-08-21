// backend/EHTables.hpp — Unwind tables + landing pads generation.
// ============================================================
// Law (Section §II Backend & Low-Level):
//   "Exception Handling Table Generation: Unwind tables and landing pads."
// ============================================================
#pragma once
#include <cstdint>
#include <vector>
#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/Target.hpp"
namespace aegis::backend {

struct UnwindEntry {
    uint32_t function_offset;       // offset within the .text section
    uint32_t unwind_info_offset;    // offset within the .eh_frame section
};

class EHTableGenerator {
public:
    EHTableGenerator(MachineFunction& mf, const Target& target)
        : mf_(mf), target_(target) {}
    // Returns the number of unwind entries emitted.
    int run(std::vector<UnwindEntry>& out_entries) noexcept;
private:
    MachineFunction& mf_;
    const Target&    target_;
};

} // namespace aegis::backend
