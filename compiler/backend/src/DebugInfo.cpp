// backend/DebugInfo.cpp — Emit DWARF line-program entries.
//
// Law (Section §II Backend & Low-Level):
//   "Debug Information Generation: Maps machine code back to source
//    for debugging."
//
// Algorithm (DWARF line program, simplified):
//   1. For each MachineInstr in the function, emit a row mapping
//      machine_offset -> (source_line, source_col).
//   2. Pack the rows into a .debug_line section.
//
// For the prototype we emit one row per MachineInstr with line = i+1.
#include "aegis/backend/DebugInfo.hpp"

namespace aegis::backend {

int DebugInfoGenerator::run(std::vector<DebugLineEntry>& out_entries) noexcept {
    for (uint32_t i = 0; i < mf_.instrs.size(); ++i) {
        DebugLineEntry e;
        e.machine_offset = i;   // (real impl: track byte offsets post-emit)
        e.source_line    = i + 1;
        e.source_col     = 1;
        out_entries.push_back(e);
    }
    return static_cast<int>(mf_.instrs.size());
}

} // namespace aegis::backend
