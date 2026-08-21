// backend/DebugInfo.hpp — Maps machine code back to source for debugging.
// ============================================================
// Law (Section §II Backend & Low-Level):
//   "Debug Information Generation: Maps machine code back to source
//    for debugging."
// ============================================================
#pragma once
#include <cstdint>
#include <vector>
#include "aegis/backend/MachineIR.hpp"
namespace aegis::backend {

struct DebugLineEntry {
    uint32_t machine_offset;     // byte offset in .text
    uint32_t source_line;        // 1-based source line
    uint32_t source_col;         // 1-based source column
};

class DebugInfoGenerator {
public:
    explicit DebugInfoGenerator(MachineFunction& mf) : mf_(mf) {}
    // Returns the number of debug line entries emitted.
    int run(std::vector<DebugLineEntry>& out_entries) noexcept;
private:
    MachineFunction& mf_;
};

} // namespace aegis::backend
