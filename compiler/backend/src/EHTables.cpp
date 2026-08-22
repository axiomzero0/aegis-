// backend/EHTables.cpp — Generate .eh_frame unwind tables.
//
// Law (Section §II Backend & Low-Level):
//   "Exception Handling Table Generation: Unwind tables and landing pads."
//
// Algorithm (DWARF-based, Linux):
//   1. For each function, emit a CIE (Common Information Entry) —
//      describes the calling convention + return address register.
//   2. For each function, emit an FDE (Frame Description Entry) —
//      describes the function's prologue/epilogue + how to restore
//      registers.
//   3. Append the FDE record to .eh_frame.
//
// Rule B.1: no exceptions on the hot path — the EH table is metadata
// only, the C++ runtime never throws into it.
#include "aegis/backend/EHTables.hpp"
#include "aegis/backend/TargetConstants.hpp"

namespace aegis::backend {

int EHTableGenerator::run(std::vector<UnwindEntry>& out_entries) noexcept {
    // Emit one FDE per function. The function's offset within .text
    // is known from the MachineFunction's start; we record that
    // + the unwind-info offset (which we'd compute from the CIE +
    // FDE layout, but for the prototype we just emit the entry).
    UnwindEntry e;
    e.function_offset = 0;       // (real impl: track function offset)
    e.unwind_info_offset = static_cast<uint32_t>(out_entries.size())
                          * constants::kEhFrameEntryStrideBytes;
    out_entries.push_back(e);
    (void)mf_; (void)target_;
    return 1;
}

} // namespace aegis::backend
