// backend/LinearScan.cpp — Phase 1 register allocator implementation.
//
// Uses constants from passes/PassConstants.hpp (Rule 61 / D.1):
//   - kLinearScanMaxPRegs bounds the `used[]` array.
//   - kLinearScanDefaultSpillCost is the weight applied to the spill
//     heuristic when no PGO profile is available.
#include "aegis/backend/RegAlloc/LinearScan.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "aegis/passes/PassConstants.hpp"

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
    // ---- LOOP WIDENING (correctness with back edges). ----
    //
    // Straight-line first/last indices ignore that a loop RE-EXECUTES
    // its body: a vreg defined before the loop and used inside it is
    // read again on every iteration, so its interval must extend to
    // the back edge (the matching jmp). Without this, the allocator
    // hands its register to a body temporary and the next iteration
    // reads a clobbered value — observed as a loop returning its
    // entry accumulator (param n's register reused for s_next).
    {
        // Find loop spans: `label L` ... `jmp L` (the structured loop
        // emitter always emits this exact pair).
        std::vector<std::pair<uint32_t, uint32_t>> spans;
        for (uint32_t i = 0; i < mf_.instrs.size(); ++i) {
            const MachineInstr& mi = mf_.instrs[i];
            if (!mi.has_imm) continue;
            if (mi.op == "label") {
                for (uint32_t j = i + 1; j < mf_.instrs.size(); ++j) {
                    const MachineInstr& mj = mf_.instrs[j];
                    if (mj.op == "jmp" && mj.has_imm && mj.imm == mi.imm) {
                        spans.emplace_back(i, j);
                        break;
                    }
                }
            }
        }
        for (const auto& [head, tail] : spans) {
            for (VRegId v = 0; v <= max_v; ++v) {
                if (first[v] == 0xFFFFFFFFu) continue;
                // Live across the loop head and still active at/after
                // it -> must survive to the back edge.
                if (first[v] < head && last[v] >= head &&
                    last[v] < tail) {
                    last[v] = tail;
                }
            }
        }
    }
    // ---- CALL SPANNING: caller-saved exclusion. ----
    //
    // An interval (AFTER loop widening — a value carried across a
    // back edge that contains a call spans it too) that still covers
    // a `call` instruction must live in a callee-saved register: the
    // call's ABI sequence overwrites the argument registers and the
    // callee owns all caller-saved registers per SysV.
    std::vector<uint8_t> spans_call(max_v + 1, 0);
    for (uint32_t i = 0; i < mf_.instrs.size(); ++i) {
        if (mf_.instrs[i].op != "call") continue;
        for (VRegId v = 0; v <= max_v; ++v) {
            if (first[v] == 0xFFFFFFFFu) continue;
            // Strictly ACROSS: an operand whose LAST read is this call
            // (last == i) is consumed by the argument moves and does
            // not need to survive it; marking it call-spanning forced
            // every call argument into the 5 callee-saved registers
            // and spilled real cross-call values.
            if (first[v] <= i && i < last[v]) spans_call[v] = 1;
        }
    }
    for (VRegId v = 0; v <= max_v; ++v) {
        if (first[v] == 0xFFFFFFFFu) continue;
        out.push_back(LiveInterval{v, rc[v], first[v], last[v] + 1,
                                   spans_call[v] != 0});
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
        // Call-spanning values may only take callee-saved pregs.
        const uint16_t preg_floor = iv.spans_call ? callee_saved_from_ : 0;

        // Find a free PReg by scanning active entries.
        // Law: Rule 61 — kLinearScanMaxPRegs is a named constant, not
        // a magic 256. Sized via PassConstants.hpp.
        PRegId free_preg = static_cast<PRegId>(-1);
        constexpr uint32_t kUsedArraySize =
            aegis::passes::constants::kLinearScanMaxPRegs;
        std::array<bool, kUsedArraySize> used{};
        for (const ActiveEntry& e : active) {
            if (e.preg < kUsedArraySize) {
                used[e.preg] = true;
            }
        }
        for (PRegId p = preg_floor; p < num_preg; ++p) {
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
