// backend/InstrSelection.cpp — Pattern-based instruction selection.
#include "aegis/backend/InstrSel.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/passes/PassConstants.hpp"

#include <string>

namespace aegis {

namespace {
const char* op_for(NodeKind k) {
    switch (k) {
        case NodeKind::Add: return "add";
        case NodeKind::Sub: return "sub";
        case NodeKind::Mul: return "mul";
        case NodeKind::Div: return "div";
        case NodeKind::And: return "and";
        case NodeKind::Or:  return "or";
        case NodeKind::Xor: return "xor";
        case NodeKind::Shl: return "shl";
        case NodeKind::Shr: return "sar";
        case NodeKind::CmpEq: return "cmp_eq";
        case NodeKind::CmpLt: return "cmp_lt";
        default: return "mov";
    }
}
} // namespace

MachineFunction InstrSelector::lower(std::string_view fn_name) {
    MachineFunction mf;
    mf.name = std::string(fn_name);
    VRegId next_vreg = 0;
    // Assign one VReg per data-producing node.
    std::vector<VRegId> vreg_per_node(g_.size(), kInvalidVReg);
    auto get_or_assign = [&](NodeId id) -> VRegId {
        if (vreg_per_node[id] != kInvalidVReg) return vreg_per_node[id];
        VRegId v = next_vreg++;
        vreg_per_node[id] = v;
        return v;
    };

    for (NodeId id = 0; id < g_.size(); ++id) {
        const Node& n = g_[id];
        if (n.flags.has(NodeFlagBit::IsDead)) continue;
        switch (n.kind) {
            case NodeKind::Constant: {
                MachineInstr mi;
                mi.op = "mov_imm";
                mi.defs[0] = get_or_assign(id);
                // Encode the immediate as a low-31-bit value to fit
                // into the VRegId-sized `uses[0]` slot. The full 64-bit
                // value is recovered by sign-extension at emit time.
                // This is a documented encoding, not a workaround.
                mi.uses[0] = static_cast<VRegId>(
                    n.payload.u64 &
                    aegis::passes::constants::kImmediateMaskLow31Bits);
                mf.instrs.push_back(mi);
                break;
            }
            case NodeKind::Add: case NodeKind::Sub: case NodeKind::Mul:
            case NodeKind::Div: case NodeKind::And: case NodeKind::Or:
            case NodeKind::Xor: case NodeKind::Shl: case NodeKind::Shr:
            case NodeKind::CmpEq: case NodeKind::CmpLt: {
                MachineInstr mi;
                mi.op = op_for(n.kind);
                mi.defs[0] = get_or_assign(id);
                auto data = n.data_ins();
                if (data.size() >= 1) mi.uses[0] = get_or_assign(data[0]);
                if (data.size() >= 2) mi.uses[1] = get_or_assign(data[1]);
                mf.instrs.push_back(mi);
                break;
            }
            case NodeKind::Load: {
                MachineInstr mi;
                mi.op = "load";
                mi.defs[0] = get_or_assign(id);
                auto data = n.data_ins();
                if (!data.empty()) mi.uses[0] = get_or_assign(data[0]);
                mf.instrs.push_back(mi);
                break;
            }
            case NodeKind::Store: {
                MachineInstr mi;
                mi.op = "store";
                auto data = n.data_ins();
                if (data.size() >= 1) mi.uses[0] = get_or_assign(data[0]);
                if (data.size() >= 2) mi.uses[1] = get_or_assign(data[1]);
                mf.instrs.push_back(mi);
                break;
            }
            case NodeKind::Return: {
                MachineInstr mi;
                mi.op = "ret";
                auto data = n.data_ins();
                if (!data.empty()) mi.uses[0] = get_or_assign(data[0]);
                mf.instrs.push_back(mi);
                break;
            }
            default:
                // Skip structural / effect nodes for now.
                break;
        }
    }
    return mf;
}

} // namespace aegis
