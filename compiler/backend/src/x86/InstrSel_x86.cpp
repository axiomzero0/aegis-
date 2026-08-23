// backend/InstrSelection.cpp — Pattern-based instruction selection.
#include "aegis/backend/InstrSel.hpp"

#include "aegis/ir/NodeKind.hpp"
#include "aegis/ir/NodeShape.hpp"
#include "aegis/passes/PassConstants.hpp"

#include <string>

namespace aegis {

namespace {
/// Bits per byte — bounds the x86-64 shift-count immediate to the
/// architectural range [0, 63] (Rule D.1: named, not a literal).
constexpr int64_t g_BitsPerByte{8};
/// Depth bound for the region-chain walk below (nested-merge lookups
/// are shallow; the bound only guards against malformed graphs).
constexpr uint32_t g_MaxRegionWalkDepth{64};

// Collect every If reachable from a control node down the pred chain
// (Proj -> its If; Region -> recurse into its preds). These are all
// the decisions that had to be made for control to reach this point.
void collect_ifs(const Graph& g, NodeId ctrl, std::vector<NodeId>& out,
                 uint32_t depth = 0) {
    if (ctrl == kInvalidNodeId || ctrl >= g.size() || depth > g_MaxRegionWalkDepth)
        return;
    if (g[ctrl].kind == NodeKind::Proj) {
        const NodeId base = g[ctrl].inputs.empty()
            ? kInvalidNodeId : g[ctrl].inputs[0];
        if (base != kInvalidNodeId && base < g.size() &&
            g[base].kind == NodeKind::If) {
            bool have = false;
            for (NodeId f : out) {
                if (f == base) { have = true; break; }
            }
            if (!have) out.push_back(base);
            // CONTINUE UP: the If's own ctrl input carries the
            // ENCLOSING decisions (a nested branch is only reachable
            // through its parent's projection). Without this walk the
            // pred chains of a nested merge share nothing and the
            // divergence point can't be found (Rule 73).
            if (g[base].inputs.size() >= ir::shape::kIfInputs) {
                collect_ifs(g, g[base].inputs[0], out, depth + 1);
            }
        }
        return;
    }
    if (g[ctrl].kind == NodeKind::Region) {
        for (NodeId pred : g[ctrl].inputs) {
            collect_ifs(g, pred, out, depth + 1);
        }
    }
}

// Resolve the If that GOVERNS a merge region: the decision at which
// the region's preds DIVERGED. That is the If reachable from EVERY
// pred chain (for a direct if-merge, each pred chain contains exactly
// the same one If; for nested merges each chain also contains the
// chain's own inner Ifs, which are NOT shared). Return the unique
// common If, or invalid.
NodeId governing_if(const Graph& g, NodeId region) {
    if (region == kInvalidNodeId || region >= g.size()) return kInvalidNodeId;
    std::vector<std::vector<NodeId>> per_pred;
    for (NodeId pred : g[region].inputs) {
        if (pred == kInvalidNodeId || pred >= g.size()) continue;
        std::vector<NodeId> ifs;
        collect_ifs(g, pred, ifs);
        if (!ifs.empty()) per_pred.push_back(std::move(ifs));
    }
    if (per_pred.empty()) return kInvalidNodeId;
    // Intersect: candidates from the first chain present in all others.
    for (NodeId cand : per_pred[0]) {
        bool shared = true;
        for (size_t i = 1; i < per_pred.size(); ++i) {
            bool found = false;
            for (NodeId f : per_pred[i]) {
                if (f == cand) { found = true; break; }
            }
            if (!found) { shared = false; break; }
        }
        if (shared) return cand;
    }
    return kInvalidNodeId;
}
/// DFS states for the topological scheduler.
constexpr uint8_t g_VisitNew{0}, g_VisitOnStack{1}, g_VisitDone{2};
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
    // ABI argument index for Parameter nodes (this function's params,
    // counted in node order = signature order).
    uint32_t param_count_ = 0;
    // Assign one VReg per data-producing node.
    std::vector<VRegId> vreg_per_node(g_.size(), kInvalidVReg);
    auto get_or_assign = [&](NodeId id) -> VRegId {
        if (vreg_per_node[id] != kInvalidVReg) return vreg_per_node[id];
        VRegId v = next_vreg++;
        vreg_per_node[id] = v;
        return v;
    };

    // Collect one MachineInstr per emittable node; scheduling happens
    // AFTER collection (topological order — see below).
    std::vector<NodeInstr> per_node;
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
                per_node.push_back(NodeInstr{id, mi});
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
                per_node.push_back(NodeInstr{id, mi});
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
                // Shifts by a CONSTANT amount fold into the immediate
                // form (x86 C1 /n ib) — no vreg + mov_imm for the
                // count. Runtime-amount shifts keep the vreg operand
                // (the executable encoder rejects those loudly; the
                // corpus/IR constant-folds them).
                const bool is_shift = n.kind == NodeKind::Shl ||
                                      n.kind == NodeKind::Shr ||
                                      n.kind == NodeKind::LShr;
                if (is_shift && data.size() >= 2 &&
                    data[1] != kInvalidNodeId && data[1] < g_.size() &&
                    g_[data[1]].kind == NodeKind::Constant) {
                    const int64_t amount = g_[data[1]].payload.i64;
                    if (amount >= 0 && amount <
                            static_cast<int64_t>(sizeof(int64_t) *
                                                 g_BitsPerByte)) {
                        mi.has_imm = true;
                        mi.imm = amount;
                        per_node.push_back(NodeInstr{id, mi});
                        break;
                    }
                }
                if (data.size() >= 2) mi.uses[1] = get_or_assign(data[1]);
                per_node.push_back(NodeInstr{id, mi});
                break;
            }
            case NodeKind::Load: {
                MachineInstr mi;
                mi.op = "load";
                mi.defs[0] = get_or_assign(id);
                auto data = n.data_ins();
                if (!data.empty()) mi.uses[0] = get_or_assign(data[0]);
                per_node.push_back(NodeInstr{id, mi});
                break;
            }
            case NodeKind::Store: {
                MachineInstr mi;
                mi.op = "store";
                auto data = n.data_ins();
                if (data.size() >= 1) mi.uses[0] = get_or_assign(data[0]);
                if (data.size() >= 2) mi.uses[1] = get_or_assign(data[1]);
                per_node.push_back(NodeInstr{id, mi});
                break;
            }
            case NodeKind::Phi: {
                // MERGE phi (region-pred) -> `select` pseudo-instr:
                // dst = cond ? then_val : else_val. The condition is
                // recovered from the region's If (region inputs are
                // the If's two Projs). Loop-pred phis are cyclic and
                // are NOT collected (the harness guard rejects their
                // undefined vregs loudly — loops are not emitted yet).
                if (n.inputs.size() != ir::shape::kPhiInputs2Branches) break;
                const NodeId region = n.inputs[0];
                if (region == kInvalidNodeId || region >= g_.size()) break;
                if (g_[region].kind != NodeKind::Region) break;
                // Find the If governing the region (direct Proj case
                // or through nested merge Regions).
                const NodeId if_node = governing_if(g_, region);
                if (if_node == kInvalidNodeId) break;
                if (g_[if_node].inputs.size() != ir::shape::kIfInputs) break;
                const NodeId cond = g_[if_node].inputs[ir::shape::kIfCondIndex];
                if (cond == kInvalidNodeId || cond >= g_.size()) break;
                MachineInstr mi;
                mi.op = "select";
                mi.defs[0] = get_or_assign(id);
                mi.uses[0] = get_or_assign(cond);        // 0/1 selector
                mi.uses[1] = get_or_assign(n.inputs[1]); // then value
                mi.uses[2] = get_or_assign(n.inputs[2]); // else value
                per_node.push_back(NodeInstr{id, mi});
                break;
            }
            case NodeKind::Parameter: {
                // ABI entry pseudo-instruction: defs[0] = the vreg that
                // carries this parameter, imm = the argument index.
                // The emitter lowers it to `mov <home>, <abi arg reg>`
                // (SysV: RDI, RSI, RDX, RCX, R8, R9). Parameters are
                // created in signature order, so the running count IS
                // the ABI index (Rule 53: node order = signature order).
                MachineInstr mi;
                mi.op = "param";
                mi.defs[0] = get_or_assign(id);
                mi.has_imm = true;
                mi.imm = static_cast<int64_t>(param_count_++);
                per_node.push_back(NodeInstr{id, mi});
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
                per_node.push_back(NodeInstr{id, mi});
                break;
            }
            default:
                // Skip structural / effect nodes for now.
                break;
        }
    }
    // ---- Path selection: loops need STRUCTURED emission. ----
    //
    // Straight-line code (no Loop nodes) uses the flat topological
    // scheduler below. A function with loops is emitted STRUCTURALLY:
    // preheader phi-initializations, loop-header label, body closure,
    // exit check (jz), back-edge phi updates, jmp, exit label — then
    // the post-loop value computations. Nested loops inside a body
    // are NOT yet emitted; their phis surface as undefined vregs and
    // the harness guard rejects them loudly (Rule D.3).
    {
        std::vector<NodeId> loops;
        for (NodeId id = 0; id < g_.size(); ++id) {
            if (g_[id].flags.has(NodeFlagBit::IsDead)) continue;
            if (g_[id].kind == NodeKind::Loop) loops.push_back(id);
        }
        if (!loops.empty()) {
            emit_structured_loops(loops, per_node, vreg_per_node,
                                  next_vreg, mf);
            return mf;
        }
    }

    // ---- Schedule: emit in GRAPH TOPOLOGICAL ORDER. ----
    //
    // Node-id order is NOT topological after the mid-level passes:
    // SCCP appends folded constants at the END of the id space and
    // rewires earlier nodes to use them, so id order can place a use
    // before its def (observed as a garbage read at runtime — the
    // differential harness caught it). A post-order DFS over each
    // emittable node's INPUT edges emits every operand before its
    // consumer; `ret` sinks last. LinearScan then computes intervals
    // over this clean stream.
    {
        // instr index per node id (only for collected nodes).
        std::vector<int64_t> instr_of(g_.size(), -1);
        for (size_t k = 0; k < per_node.size(); ++k) {
            instr_of[per_node[k].node] = static_cast<int64_t>(k);
        }
        std::vector<uint8_t> state(g_.size(), 0); // 0=new 1=on-stack 2=done
        // Explicit stack: (node, inputs_pushed). Rule 73: no recursion
        // depth limits.
        struct Frame { NodeId node; bool expanded; };
        std::vector<Frame> stack;
        for (auto& [node_id, mi] : per_node) {
            (void)mi;
            if (state[node_id] != 0) continue;
            stack.push_back(Frame{node_id, false});
            while (!stack.empty()) {
                Frame& top = stack.back();
                // Capture the node id by value: every use of `top`
                // AFTER a push_back below would read through a
                // dangling reference when the stack vector reallocates
                // (ASan-caught; Rule 73).
                const NodeId top_node = top.node;
                if (!top.expanded) {
                    if (state[top_node] == g_VisitDone) { stack.pop_back(); continue; }
                    state[top_node] = g_VisitOnStack;
                    // Mark expanded BEFORE any push_back — pushing may
                    // reallocate the vector and dangle `top` (Rule 73).
                    top.expanded = true;
                    // Push DATA inputs that carry instrs (ctrl/eff
                    // nodes have none; their instrs, if any, are
                    // self-contained). Inputs are pushed LAST-to-FIRST
                    // so the stack pops them FIRST-to-LAST: operands
                    // emit in source order (left-to-right), keeping
                    // right-hand constants just-in-time instead of
                    // live across the whole expression (the naive
                    // order made all of a 64-term chain's constants
                    // live at once and spilled).
                    bool pushed_any = false;
                    const auto& ins = g_[top.node].inputs;
                    for (size_t k = ins.size(); k-- > 0;) {
                        NodeId in = ins[k];
                        if (in == kInvalidNodeId || in >= g_.size()) continue;
                        if (instr_of[in] == -1) continue;      // no instr
                        if (state[in] != 0) continue;          // visited
                        stack.push_back(Frame{in, false});
                        pushed_any = true;
                    }
                    // A select's condition lives on the If, not on the
                    // Phi — visit it explicitly so the compare that
                    // produces the 0/1 selector is emitted BEFORE the
                    // select (the Phi's own input list would not order
                    // it; Rule 73). Uses ONLY the captured id (no
                    // `top` here — pushes may have reallocated).
                    if (g_[top_node].kind == NodeKind::Phi &&
                        ins.size() == ir::shape::kPhiInputs2Branches &&
                        ins[0] < g_.size() &&
                        g_[ins[0]].kind == NodeKind::Region) {
                        for (NodeId pred : g_[ins[0]].inputs) {
                            if (pred == kInvalidNodeId || pred >= g_.size())
                                continue;
                            if (g_[pred].kind != NodeKind::Proj) continue;
                            const NodeId base = g_[pred].inputs.empty()
                                ? kInvalidNodeId : g_[pred].inputs[0];
                            if (base == kInvalidNodeId || base >= g_.size())
                                continue;
                            if (g_[base].kind != NodeKind::If) continue;
                            if (g_[base].inputs.size() !=
                                ir::shape::kIfInputs) continue;
                            const NodeId cond =
                                g_[base].inputs[ir::shape::kIfCondIndex];
                            if (cond == kInvalidNodeId || cond >= g_.size())
                                continue;
                            if (instr_of[cond] == -1 || state[cond] != 0)
                                continue;
                            stack.push_back(Frame{cond, false});
                            pushed_any = true;
                        }
                    }
                    if (!pushed_any) {
                        // Leaf: emit now (no pushes happened; still use
                        // the captured id for uniformity).
                        mf.instrs.push_back(per_node[static_cast<size_t>(
                            instr_of[top_node])].mi);
                        state[top_node] = g_VisitDone;
                        stack.pop_back();
                    }
                    continue;
                }
                // Expanded: all children processed when control returns
                // here — emit and finish. (No push happens on this
                // path; the captured id keeps it reallocation-safe.)
                mf.instrs.push_back(per_node[static_cast<size_t>(
                    instr_of[top_node])].mi);
                state[top_node] = g_VisitDone;
                stack.pop_back();
            }
        }
        // ---- Dead-instruction sweep. ----
        //
        // Selection folds constant shift amounts into the immediate
        // form, leaving the shift-count Constant node selected as a
        // `mov_imm` whose vreg has NO users. Emitting it would write a
        // register the allocator never reserved (default preg 0) and
        // silently clobber a live value — observed as a wrong runtime
        // result by the differential harness. Any instr whose def has
        // no remaining users (and is not `ret`) is dead: drop it.
        {
            std::vector<uint8_t> used(next_vreg, 0);
            for (const auto& mi : mf.instrs) {
                for (VRegId u : mi.uses) {
                    if (u != kInvalidVReg && u < used.size()) used[u] = 1;
                }
            }
            std::vector<MachineInstr> live;
            live.reserve(mf.instrs.size());
            for (auto& mi : mf.instrs) {
                if (mi.op != "ret" && mi.defs[0] != kInvalidVReg &&
                    mi.defs[0] < used.size() && used[mi.defs[0]] == 0) {
                    continue; // dead def: never emitted
                }
                live.push_back(std::move(mi));
            }
            mf.instrs = std::move(live);
        }

        // Cycle guard (Rule D.3): every emittable instr must have been
        // scheduled. A use cycle in straight-line SSA is a compiler
        // bug — abort loudly rather than emit wrong code.
        if (mf.instrs.size() > per_node.size()) {
            std::fprintf(stderr,
                         "instrsel: scheduling left %zu of %zu instrs "
                         "unscheduled (use cycle?)\n",
                         per_node.size() - mf.instrs.size(),
                         per_node.size());
            std::abort();
        }
    }
    return mf;
}

// (namespace continues — structured loop emitter below)

// ============================================================
// Structured loop emission.
// ============================================================
//
// The canonical source-level loop shape (from the for-statement
// lowering) is:
//
//     Loop(back=body_ctrl, entry)
//       phi_k = phi(Loop, {entry_k, back_k})
//       cond  = cmp(phi_primary, bound)
//       If(Loop, cond) -> Proj0 (body ctrl) ... body ... back to Loop
//                       -> Proj1 (exit ctrl)
//
// Emitted machine code per loop (all phis hold machine registers for
// the whole loop; updates happen IN PLACE at the back edge):
//
//     <preheader>   mov v(phi_k), v(entry_k)        ; per phi
//     head:         <body closure, topo order>       ; phis are leaves
//                  jz  exit, v(cond)                 ; exit check
//                  mov v(phi_k), v(back_k)           ; back-edge update
//                  jmp head
//     exit:
//
// SOUNDNESS: the phi register holds the CURRENT iteration's value
// during the body (updates come last); at the jz-taken exit it holds
// the value the NEXT iteration would have entered with — exactly the
// post-loop read convention of the lowering. Body computations are
// pure and are fully recomputed each iteration.
namespace {

// Is this node a loop phi (its region input is a Loop header)?
bool is_loop_phi(const Graph& g, NodeId id) {
    if (id >= g.size()) return false;
    const Node& n = g[id];
    return n.kind == NodeKind::Phi && !n.inputs.empty() &&
           n.inputs[0] < g.size() && g[n.inputs[0]].kind == NodeKind::Loop;
}

} // namespace

void InstrSelector::emit_structured_loops(const std::vector<NodeId>& loops,
                                          std::vector<NodeInstr>& per_node,
                                          std::vector<VRegId>& vreg_per_node,
                                          VRegId& next_vreg,
                                          MachineFunction& mf) {
    // instr index per node id (collected nodes only).
    std::vector<int64_t> instr_of(g_.size(), -1);
    for (size_t k = 0; k < per_node.size(); ++k) {
        instr_of[per_node[k].node] = static_cast<int64_t>(k);
    }

    // Vreg assignment is SHARED with the collection phase (the map
    // is passed in): nodes the selector already touched keep their
    // ids — a fresh map here would double-assign the phi registers and
    // silently disconnect the loop body from its phis (caught by the
    // executed differential harness).
    std::vector<VRegId>& vreg_of = vreg_per_node;
    auto vreg_for = [&](NodeId id) -> VRegId {
        if (id >= g_.size()) return kInvalidVReg;
        if (vreg_of[id] != kInvalidVReg) return vreg_of[id];
        VRegId v = next_vreg++;
        vreg_of[id] = v;
        return v;
    };

    std::vector<uint8_t> emitted(g_.size(), 0);

    // ---- Prologue: ALL parameter instructions first. ----
    // A `param` reads its ABI ARGUMENT register; those registers are
    // also vreg homes and may be overwritten inside loops/bodies, so
    // params must execute exactly once at entry — never inside a loop
    // body (the closure walk below would otherwise pull a param in
    // wherever its value is first used, re-reading a clobbered
    // register; caught by inspection of the emitted stream).
    for (const auto& ni : per_node) {
        if (ni.mi.op != "param") continue;
        mf.instrs.push_back(ni.mi);
        emitted[ni.node] = 1;
    }

    // Topo-emit the computation closure of `roots` (explicit-stack
    // post-order). Leaves: nodes with no collected instruction, nodes
    // already emitted, and loop phis (their registers are defined by
    // the loop machinery, and their inputs are NOT followed — that is
    // where the cycle lives).
    auto emit_closure = [&](const std::vector<NodeId>& roots) {
        struct Frame { NodeId node; bool expanded; };
        std::vector<Frame> st;
        std::vector<uint8_t> onpath(g_.size(), 0);
        for (NodeId r : roots) {
            if (r == kInvalidNodeId || r >= g_.size()) continue;
            if (instr_of[r] == -1 || emitted[r] != 0 || is_loop_phi(g_, r))
                continue;
            if (onpath[r] == 0) st.push_back(Frame{r, false});
        }
        while (!st.empty()) {
            const NodeId node = st.back().node;
            if (!st.back().expanded) {
                st.back().expanded = true; // BEFORE pushes (Rule 73)
                onpath[node] = 1;
                const auto& ins = g_[node].inputs;
                for (size_t k = ins.size(); k-- > 0;) {
                    NodeId in = ins[k];
                    if (in == kInvalidNodeId || in >= g_.size()) continue;
                    if (instr_of[in] == -1) continue;
                    if (emitted[in] != 0) continue;
                    if (is_loop_phi(g_, in)) continue; // cycle cut
                    if (onpath[in] != 0) continue;
                    st.push_back(Frame{in, false});
                }
                // A select phi's condition lives on the governing If,
                // not among the phi's inputs — visit it explicitly (the
                // flat scheduler carries the same special case; without
                // it the compare is never emitted and the select reads
                // an undefined register).
                if (g_[node].kind == NodeKind::Phi &&
                    ins.size() == ir::shape::kPhiInputs2Branches &&
                    ins[0] < g_.size() &&
                    g_[ins[0]].kind == NodeKind::Region) {
                    const NodeId gif = governing_if(g_, ins[0]);
                    if (gif != kInvalidNodeId &&
                        g_[gif].inputs.size() == ir::shape::kIfInputs) {
                        const NodeId gcond =
                            g_[gif].inputs[ir::shape::kIfCondIndex];
                        if (gcond != kInvalidNodeId && gcond < g_.size() &&
                            instr_of[gcond] != -1 && emitted[gcond] == 0 &&
                            onpath[gcond] == 0) {
                            st.push_back(Frame{gcond, false});
                        }
                    }
                }
                continue;
            }
            st.pop_back();
            if (emitted[node] != 0) continue;
            emitted[node] = 1;
            mf.instrs.push_back(per_node[static_cast<size_t>(
                instr_of[node])].mi);
        }
    };

    // Label ids: 2*i = head of loops[i], 2*i+1 = exit of loops[i].
    for (size_t li = 0; li < loops.size(); ++li) {
        const NodeId loop = loops[li];
        const int64_t head_label = static_cast<int64_t>(2 * li);
        const int64_t exit_label = static_cast<int64_t>(2 * li + 1);

        // The loop's phis and its exit If.
        std::vector<NodeId> phis;
        NodeId exit_if = kInvalidNodeId;
        for (NodeId u : g_.users_snapshot(loop)) {
            if (u >= g_.size()) continue;
            if (g_[u].flags.has(NodeFlagBit::IsDead)) continue;
            if (g_[u].kind == NodeKind::Phi &&
                !g_[u].inputs.empty() && g_[u].inputs[0] == loop) {
                phis.push_back(u);
            } else if (g_[u].kind == NodeKind::If &&
                       g_[u].ctrl_in() == loop) {
                exit_if = u;
            }
        }
        // Exit condition node (0/1 selector value).
        NodeId cond = kInvalidNodeId;
        if (exit_if != kInvalidNodeId &&
            g_[exit_if].inputs.size() == ir::shape::kIfInputs) {
            cond = g_[exit_if].inputs[ir::shape::kIfCondIndex];
        }
        // No phis or no condition: nothing executable to emit for
        // this loop — leave its values undefined; the harness guard
        // reports it loudly rather than emitting wrong code.
        if (phis.empty() || cond == kInvalidNodeId) continue;

        // ---- Preheader: entry computations + phi inits. ----
        for (NodeId phi : phis) {
            if (g_[phi].inputs.size() < ir::shape::kPhiInputs2Branches)
                continue;
            const NodeId entry = g_[phi].inputs[1];
            emit_closure({entry});
            MachineInstr init;
            init.op = "mov";
            init.defs[0] = vreg_for(phi);
            init.uses[0] = vreg_for(entry);
            mf.instrs.push_back(init);
        }

        // ---- Head label. ----
        {
            MachineInstr lab;
            lab.op = "label";
            lab.has_imm = true;
            lab.imm = head_label;
            mf.instrs.push_back(lab);
        }

        // ---- Body: closure of back-edge values + the condition. ----
        std::vector<NodeId> roots;
        for (NodeId phi : phis) {
            if (g_[phi].inputs.size() < ir::shape::kPhiInputs2Branches)
                continue;
            roots.push_back(g_[phi].inputs[2]); // back value
        }
        roots.push_back(cond);
        emit_closure(roots);

        // ---- Exit check + updates + jump. ----
        {
            MachineInstr jz;
            jz.op = "jz";
            jz.uses[0] = vreg_for(cond);
            jz.has_imm = true;
            jz.imm = exit_label;
            mf.instrs.push_back(jz);
        }
        for (NodeId phi : phis) {
            if (g_[phi].inputs.size() < ir::shape::kPhiInputs2Branches)
                continue;
            MachineInstr upd;
            upd.op = "mov";
            upd.defs[0] = vreg_for(phi);
            upd.uses[0] = vreg_for(g_[phi].inputs[2]);
            mf.instrs.push_back(upd);
        }
        {
            MachineInstr jmp;
            jmp.op = "jmp";
            jmp.has_imm = true;
            jmp.imm = head_label;
            mf.instrs.push_back(jmp);
        }
        {
            MachineInstr lab;
            lab.op = "label";
            lab.has_imm = true;
            lab.imm = exit_label;
            mf.instrs.push_back(lab);
        }
    }

    // ---- Post-loop code: the return value's closure. ----
    NodeId ret_node = kInvalidNodeId;
    for (const auto& ni : per_node) {
        if (ni.mi.op == "ret") { ret_node = ni.node; break; }
    }
    if (ret_node != kInvalidNodeId) {
        const Node& rn = g_[ret_node];
        if (rn.inputs.size() >= ir::shape::kReturnInputs) {
            emit_closure({rn.inputs[ir::shape::kReturnValIndex]});
        }
        mf.instrs.push_back(per_node[static_cast<size_t>(
            instr_of[ret_node])].mi);
    }
}

} // namespace aegis
