// backend/LinearScan.h — Phase 1 register allocator (per the spec).
// ============================================================
// Law: spec §"Backend" —
//   "Phase 1 (Get it working): Write a Linear Scan Register Allocator
//    in C++26. It's simple, fast, and doesn't require external
//    dependencies. This will get your custom backend up and running."
//
// Standard linear-scan algorithm (Massa et al., 2005):
//   1. Compute live intervals (one per VReg).
//   2. Sort intervals by start position.
//   3. Walk intervals in order; assign a free PReg from the right
//      RegClass. If none is free, spill the one with the furthest
//      next-use.
// ============================================================
#pragma once
#include <vector>
#include "aegis/backend/MachineIR.hpp"
#include "aegis/ir/Graph.hpp"

namespace aegis {

struct LiveInterval {
    VRegId  vreg{kInvalidVReg};
    RegClass rc{RegClass::General};
    uint32_t start{0};  // first instruction index where vreg is live
    uint32_t end{0};    // last instruction index + 1
};

class LinearScanAllocator {
public:
    // The allocator only READS the MachineFunction (interval
    // computation); const-ref so emitters can allocate over a
    // function they received by const& (Rule 73: interfaces that
    // cannot misuse).
    LinearScanAllocator(const MachineFunction& mf, uint16_t num_gpr, uint16_t num_fpr)
        : mf_(mf), num_gpr_(num_gpr), num_fpr_(num_fpr) {}

    // Run the allocator. Returns the number of spills emitted.
    uint32_t run();

    [[nodiscard]] PRegId assignment(VRegId v) const noexcept {
        if (v >= assignment_.size()) return 0;
        return assignment_[v];
    }
    [[nodiscard]] bool is_spilled(VRegId v) const noexcept {
        if (v >= spilled_.size()) return false;
        return spilled_[v];
    }

private:
    const MachineFunction& mf_;
    uint16_t num_gpr_;
    uint16_t num_fpr_;
    std::vector<PRegId> assignment_{};
    std::vector<uint8_t> spilled_{};

    // Compute live intervals using a backwards scan.
    void compute_intervals(std::vector<LiveInterval>& out);
};

} // namespace aegis
