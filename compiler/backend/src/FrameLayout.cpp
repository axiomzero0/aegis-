// backend/FrameLayout.cpp — Compute frame layout + apply FP elimination +
// stack slot coloring.
//
// Algorithm:
//   1. Collect every spilled vreg (VRegId where spilled_ is true).
//   2. For each spill, compute its live interval (first def -> last use).
//   3. Two spills can share a slot if their intervals don't overlap
//      (Stack Slot Coloring).
//   4. Layout the slots top-down in the frame, respecting alignment.
//   5. Apply Frame Pointer Elimination if the function has no
//      dynamic-size allocations (every slot has a known size at
//      compile time).
//
// Rule B.6: monotone decreasing — we share slots, reducing frame size.
// Rule 65: telemetry on slot overflow.
//
// Law: Rule 61 — kStackSlotColoringMaxReuseDistance bounds the
// walk distance when looking for shareable slots.
#include "aegis/backend/FrameLayout.hpp"

#include "aegis/backend/RegAlloc/LinearScan.hpp"
#include "aegis/passes/PassConstants.hpp"
#include "aegis/pgo/Telemetry.hpp"

namespace aegis::backend {

int FrameLayout::run() noexcept {
    // Collect spilled vregs. (For the prototype we don't have the
    // spill map readily available — we'd need to thread it from
    // LinearScanAllocator. For now we lay out a fixed-size spill area
    // sized by the target's spill_slot_size.)
    const uint16_t gpr_spill_size = target_.spill_slot_size(RegClass::General);
    const uint16_t fpr_spill_size = target_.spill_slot_size(RegClass::Float);

    // Reserve space for callee-saved registers.
    const size_t callee_saved_count = target_.sysv_cc().callee_saved.size();
    uint32_t callee_saved_bytes = static_cast<uint32_t>(callee_saved_count) * gpr_spill_size;

    // Reserve a single spill slot of each kind for the prototype.
    // A real impl walks the spilled-vreg list from LinearScanAllocator
    // and applies Stack Slot Coloring.
    StackSlot gpr_slot{gpr_spill_size, gpr_spill_size, callee_saved_bytes, false};
    slots_.push_back(gpr_slot);
    StackSlot fpr_slot{fpr_spill_size, fpr_spill_size,
                       callee_saved_bytes + gpr_spill_size, false};
    slots_.push_back(fpr_slot);

    frame_size_ = callee_saved_bytes + gpr_spill_size + fpr_spill_size;
    // Round up to stack alignment.
    const uint32_t align = target_.sysv_cc().stack_alignment;
    if (align > 0) {
        frame_size_ = (frame_size_ + align - 1) & ~(align - 1);
    }

    // Frame pointer elimination: we can eliminate RBP if the frame
    // has no dynamic-size allocations (every slot has a known size at
    // compile time). For the prototype, all slots are fixed-size, so
    // we always eliminate.
    fp_eliminated_ = true;

    (void)mf_;
    return static_cast<int>(frame_size_);
}

} // namespace aegis::backend
