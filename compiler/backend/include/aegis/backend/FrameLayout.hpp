// backend/FrameLayout.hpp — Frame pointer elimination + stack slot coloring
// + prologue/epilogue generation.
// ============================================================
// Laws:
//   "Frame Pointer Elimination: Frees frame pointer register for
//    general use."
//   "Stack Slot Coloring: Shares stack slots for non-overlapping
//    spilled variable lifetimes."
//   "Prologue/Epilogue Generation: Function entry/exit code generation."
// ============================================================
#pragma once
#include <cstdint>
#include <vector>
#include "aegis/backend/MachineIR.hpp"
#include "aegis/backend/Target.hpp"
namespace aegis::backend {

struct StackSlot {
    uint32_t size_bytes;
    uint32_t alignment;
    uint32_t offset;       // assigned by the layout pass
    bool     is_shared;    // shared with another slot (Stack Slot Coloring)
};

class FrameLayout {
public:
    FrameLayout(MachineFunction& mf, const Target& target)
        : mf_(mf), target_(target) {}

    // Returns the total frame size in bytes.
    int run() noexcept;

    [[nodiscard]] const std::vector<StackSlot>& slots() const noexcept { return slots_; }
    [[nodiscard]] uint32_t frame_size() const noexcept { return frame_size_; }
    [[nodiscard]] bool frame_pointer_eliminated() const noexcept { return fp_eliminated_; }

private:
    MachineFunction&            mf_;
    const Target&               target_;
    std::vector<StackSlot>     slots_{};
    uint32_t                    frame_size_{0};
    bool                        fp_eliminated_{false};
};

} // namespace aegis::backend
