// backend/LinearScan.cpp — Phase 1 register allocator implementation.
#include "aegis/backend/RegAlloc/LinearScan.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace aegis {

namespace {
struct ActiveEntry {
    VRegId  vreg{kInvalidVReg};
    uint32_t end{0};
    PRegId  preg{0};
};
}

void LinearScanAllocator::compute_intervals(std::vector<LiveInterval>& out) {
    // For each VReg, find its first def and its last use across the
    // function's instruction stream.
    VRegId max_v = 0;
    for (const auto& i : mf_.instrs) {
        for (VRegId v : i.defs) if (v != kInvalidVReg) max_v = std::max(max_v, v);
        for (VRegId v : i.uses) if (v != kInvalidVReg) max_v = std::max(max_v, v);
    }
    std::vector<uint32_t> first(max_v + 1, 0xFFFFFFFFu);
    std::vector<uint32_t> last(max_v + 1, 0);
    std::vector<RegClass> rc(max_v + 1, RegClass::General);
    for (uint32_t i = 0; i < mf_.instrs.size(); ++i) {
        const MachineInstr& mi = mf_.instrs[i];
        for (VRegId v : mi.defs) {
            if (v == kInvalidVReg) continue;
            if (first[v] == 0xFFFFFFFFu) first[v] = i;
            last[v] = i;
            rc[v] = mi.rc;
        }
        for (VRegId v : mi.uses) {
            if (v == kInvalidVReg) continue;
            if (first[v] == 0xFFFFFFFFu) first[v] = i;
            last[v] = i;
            rc[v] = mi.rc;
        }
    }
    for (VRegId v = 0; v <= max_v; ++v) {
        if (first[v] == 0xFFFFFFFFu) continue;
        out.push_back(LiveInterval{v, rc[v], first[v], last[v] + 1});
    }
}

uint32_t LinearScanAllocator::run() {
    std::vector<LiveInterval> intervals;
    compute_intervals(intervals);
    std::sort(intervals.begin(), intervals.end(),
              [](const LiveInterval& a, const LiveInterval& b) {
                  if (a.start != b.start) return a.start < b.start;
                  return a.end < b.end;
              });

    VRegId max_v = 0;
    for (const auto& iv : intervals) max_v = std::max(max_v, iv.vreg);
    assignment_.assign(max_v + 1, 0);
    spilled_.assign(max_v + 1, 0);

    std::vector<ActiveEntry> active;
    uint32_t spills = 0;

    for (const auto& iv : intervals) {
        // Expire intervals that ended before iv.start.
        active.erase(
            std::remove_if(active.begin(), active.end(),
                           [&](const ActiveEntry& e) { return e.end <= iv.start; }),
            active.end());

        uint16_t num_preg = (iv.rc == RegClass::Float) ? num_fpr_ : num_gpr_;

        // Find a free PReg by scanning active entries.
        PRegId free_preg = static_cast<PRegId>(-1);
        bool used[256] = {};
        for (const ActiveEntry& e : active) {
            if (e.preg < 256) used[e.preg] = true;
        }
        for (PRegId p = 0; p < num_preg; ++p) {
            if (!used[p]) { free_preg = p; break; }
        }

        if (free_preg != static_cast<PRegId>(-1)) {
            assignment_[iv.vreg] = free_preg;
            active.push_back(ActiveEntry{iv.vreg, iv.end, free_preg});
        } else {
            // Spill: pick the active entry with the furthest endpoint.
            auto max_it = std::max_element(active.begin(), active.end(),
                [](const ActiveEntry& a, const ActiveEntry& b) { return a.end < b.end; });
            if (max_it != active.end() && max_it->end > iv.end) {
                spilled_[max_it->vreg] = 1;
                assignment_[iv.vreg] = max_it->preg;
                PRegId preg = max_it->preg;
                active.erase(max_it);
                active.push_back(ActiveEntry{iv.vreg, iv.end, preg});
            } else {
                spilled_[iv.vreg] = 1;
                assignment_[iv.vreg] = static_cast<PRegId>(-1); // no preg
            }
            ++spills;
        }
    }
    return spills;
}

} // namespace aegis
