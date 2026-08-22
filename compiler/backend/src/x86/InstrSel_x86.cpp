// backend/InstrSelection.cpp — Pattern-based instruction selection.
#include "aegis/backend/InstrSel.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/ir/NodeShape.hpp"
#include "aegis/passes/PassConstants.hpp"

#include <string>

namespace aegis {

namespace {
// Mnemonic for a binary op. Returns nullptr when the kind has no
// machine mapping — callers must SKIP the instruction rather than
// emit a wrong opcode (Rule D.3: no silent fallback to "mov").
const char* op_for(NodeKind k) {
    switch (k) {
        case NodeKind::Add:   return "add";
        case NodeKind::Sub:   return "sub";
        case NodeKind::Mul:   return "mul";
        case NodeKind::Div:   return "div";
        case NodeKind::UDiv:  return "udiv";
        case NodeKind::Mod:   return "mod";
        case NodeKind::UMod:  return "umod";
        case NodeKind::And:   return "and";
        case NodeKind::Or:    return "or";
        case NodeKind::Xor:   return "xor";
        case NodeKind::Shl:   return "shl";
        case NodeKind::Shr:   return "sar";
        case NodeKind::LShr:  return "shr";
        case NodeKind::CmpEq: return "cmp_eq";
        case NodeKind::CmpNe: return "cmp_ne";
        case NodeKind::CmpLt: return "cmp_lt";
        case NodeKind::CmpLe: return "cmp_le";
        case NodeKind::CmpGt: return "cmp_gt";
        case NodeKind::CmpGe: return "cmp_ge";
        default: return nullptr;
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
                // The immediate lives in the DEDICATED imm field (full
                // 64 bits). It must NOT be packed into uses[0]: the
                // register allocator sizes its tables by the largest
                // vreg id, so a large/negative immediate in a vreg
                // slot made it allocate gigabytes (std::bad_alloc).
                mi.has_imm = true;
                mi.imm = n.payload.i64;
                mf.instrs.push_back(mi);
                break;
            }
            case NodeKind::Neg:
            case NodeKind::Not:
            case NodeKind::BitNot: {
                // Unary ops get real instructions — silently skipping
                // them would leave their vreg defined nowhere while
                // uses still reference it (Rule D.3).
                static constexpr const char* kUnaryOps[] = {"neg", "lnot", "bnot"};
                int idx = (n.kind == NodeKind::Neg) ? 0
                        : (n.kind == NodeKind::Not) ? 1 : 2;
                MachineInstr mi;
                mi.op = kUnaryOps[idx];
                mi.defs[0] = get_or_assign(id);
                auto data = n.data_ins();
                if (!data.empty()) mi.uses[0] = get_or_assign(data[0]);
                mf.instrs.push_back(mi);
                break;
            }
            case NodeKind::Add: case NodeKind::Sub: case NodeKind::Mul:
            case NodeKind::Div: case NodeKind::UDiv: case NodeKind::Mod:
            case NodeKind::UMod:
            case NodeKind::And: case NodeKind::Or:
            case NodeKind::Xor: case NodeKind::Shl: case NodeKind::Shr:
            case NodeKind::LShr:
            case NodeKind::CmpEq: case NodeKind::CmpNe:
            case NodeKind::CmpLt: case NodeKind::CmpLe:
            case NodeKind::CmpGt: case NodeKind::CmpGe: {
                const char* op = op_for(n.kind);
                if (op == nullptr) break; // no machine mapping: skip
                MachineInstr mi;
                mi.op = op;
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
                // Return is Pure, so data_ins() spans ALL inputs
                // [ctrl, eff, value] — the returned VALUE is the LAST
                // input, not data[0] (which is the control proj).
                // (Pre-fix `ret` read the control proj: silent wrong
                // operand, Rule D.3.)
                if (n.inputs.size() >= ir::shape::kReturnInputs) {
                    mi.uses[0] = get_or_assign(
                        n.inputs[ir::shape::kReturnValIndex]);
                }
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
