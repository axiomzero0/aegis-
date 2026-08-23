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

// Is this node a REGION-pred merge phi (a select)?
bool is_select_phi(const Graph& g, NodeId id) {
    if (id >= g.size()) return false;
    const Node& n = g[id];
    return n.kind == NodeKind::Phi &&
           n.inputs.size() == ir::shape::kPhiInputs2Branches &&
           n.inputs[0] < g.size() &&
           g[n.inputs[0]].kind == NodeKind::Region;
}

// Is this node a loop phi (its region input is a Loop header)?
bool is_loop_phi_pub(const Graph& g, NodeId id) {
    if (id >= g.size()) return false;
    const Node& n = g[id];
    return n.kind == NodeKind::Phi && !n.inputs.empty() &&
           n.inputs[0] < g.size() && g[n.inputs[0]].kind == NodeKind::Loop;
}

// Is this node the VALUE projection (proj 0) of a Call node?
bool is_call_value_proj(const Graph& g, NodeId id) {
    if (id >= g.size()) return false;
    const Node& n = g[id];
    return n.kind == NodeKind::Proj && n.payload.proj_index == 0 &&
           !n.inputs.empty() && n.inputs[0] < g.size() &&
           (g[n.inputs[0]].kind == NodeKind::CallPure ||
            g[n.inputs[0]].kind == NodeKind::CallAltered ||
            g[n.inputs[0]].kind == NodeKind::CallCrowded);
}
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
            case NodeKind::CallPure:
            case NodeKind::CallAltered:
            case NodeKind::CallCrowded: {
                // `call` pseudo-instr: uses = argument vregs (SysV
                // order), defs[0] = result vreg, imm = callee
                // SymbolId. The module encoder resolves symbols to
                // code offsets and patches rel32; argument placement
                // into ABI registers happens at encode time (a
                // permutation-safe parallel move — destinations are
                // homes' registers, so naive sequential moves could
                // clobber an unread argument home).
                auto args = n.data_ins();
                if (args.size() > kMaxUsesPerInstr) {
                    // Loud skip is wrong here (silent wrong code);
                    // record nothing and let the harness guard reject
                    // the undefined result vreg downstream.
                    break;
                }
                MachineInstr mi;
                mi.op = "call";
                mi.defs[0] = get_or_assign(id);
                for (size_t a = 0; a < args.size(); ++a) {
                    mi.uses[a] = get_or_assign(args[a]);
                }
                mi.has_imm = true;
                mi.imm = static_cast<int64_t>(n.payload.sym);
                per_node.push_back(NodeInstr{id, mi});
                break;
            }
            case NodeKind::Proj: {
                // Proj(0) of a Call is the call's VALUE: alias its
                // vreg to the call's result (no instruction of its
                // own). Other projections are structural.
                if (n.payload.proj_index != 0) break;
                if (n.inputs.empty() || n.inputs[0] >= g_.size()) break;
                const NodeId base = n.inputs[0];
                if (base != kInvalidNodeId &&
                    (g_[base].kind == NodeKind::CallPure ||
                     g_[base].kind == NodeKind::CallAltered ||
                     g_[base].kind == NodeKind::CallCrowded)) {
                    vreg_per_node[id] = vreg_per_node[base];
                }
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
    // Node-id -> per_node index (shared by both emission paths and
    // the ownership pass below).
    instr_of_shared_.assign(g_.size(), -1);
    for (size_t k = 0; k < per_node.size(); ++k) {
        instr_of_shared_[per_node[k].node] = static_cast<int64_t>(k);
    }



    // ---- Select-phi ownership (emission-order correctness). ----
    //
    // A merge phi's arm computations must be emitted INSIDE its
    // branch region (guarded), never before it. The per_node loop
    // walks in node-id order and the arms usually have LOWER ids than
    // the phi (lowering creates arm values before the merge), so
    // without ownership the arms emit first and the "branch" only
    // guards the movs — the arms (including calls!) still ran
    // unconditionally: recursive fib(0) overflowed the stack (caught
    // by the runtime differential harness). Nodes whose entire use
    // set lives within one select's region are OWNED by it; the outer
    // walks skip owned nodes, the region emits them.
    {
        // Tentative ownership by closure walk (stopping at inner
        // select phis — they own their own subtrees).
        std::vector<NodeId> owner(g_.size(), kInvalidNodeId);
        for (const auto& ni : per_node) {
            if (!is_select_phi(g_, ni.node)) continue;
            const NodeId S = ni.node;
            std::vector<NodeId> work;
            work.push_back(g_[S].inputs[1]);
            work.push_back(g_[S].inputs[2]);
            const NodeId gif = governing_if(g_, g_[S].inputs[0]);
            if (gif != kInvalidNodeId &&
                g_[gif].inputs.size() == ir::shape::kIfInputs) {
                work.push_back(g_[gif].inputs[ir::shape::kIfCondIndex]);
            }
            std::vector<uint8_t> seen(g_.size(), 0);
            while (!work.empty()) {
                const NodeId n = work.back();
                work.pop_back();
                if (n == kInvalidNodeId || n >= g_.size()) continue;
                if (is_select_phi(g_, n)) continue; // inner owns below
                // Loop phis cut the walk: their inputs belong to the
                // LOOP machinery (entry consts, back-edge updates),
                // not to this select's arms. Descending through them
                // claimed those nodes as select property and every
                // emitter then skipped them — the loop's constants and
                // induction update were never emitted and the loop
                // spun forever on a stale register (caught as a
                // runtime timeout by the harness).
                if (is_loop_phi_pub(g_, n)) continue;
                if (seen[n] != 0) continue;
                seen[n] = 1;
                if (instr_of_shared_[n] != -1 &&
                    owner[n] == kInvalidNodeId) {
                    owner[n] = S;
                }
                for (NodeId in : g_[n].inputs) {
                    if (in == kInvalidNodeId || in >= g_.size()) continue;
                    work.push_back(in);
                }
            }
            select_owner_[S] = owner; // per-select view (validated next)
        }
        // Validation to fixpoint: an owned node's EVERY user must be
        // its owner or owned by the same owner. Shared escapes free
        // the node (emitted early, unguarded — safe, just less
        // optimal; the corpus arms don't escape).
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& [S, own] : select_owner_) {
                (void)S;
                for (NodeId n = 0; n < g_.size(); ++n) {
                    if (own[n] == kInvalidNodeId) continue;
                    for (NodeId u : g_.users_snapshot(n)) {
                        if (u >= g_.size()) continue;
                        const bool ok = u == S ||
                                        (own[u] != kInvalidNodeId &&
                                         own[u] == own[n]) ||
                                        select_owner_.count(u) == 0;
                        if (!ok && select_owner_.count(u) == 0 &&
                            u != S) {
                            // user escapes this select: free the node.
                            own[n] = kInvalidNodeId;
                            changed = true;
                            break;
                        }
                    }
                }
            }
        }
        // Flatten into one global owner view for the walkers.
        select_global_owner_.assign(g_.size(), kInvalidNodeId);
        for (auto& [S, own] : select_owner_) {
            for (NodeId n = 0; n < g_.size(); ++n) {
                if (own[n] == S &&
                    select_global_owner_[n] == kInvalidNodeId) {
                    select_global_owner_[n] = S;
                }
            }
        }
    }

        std::vector<uint8_t> state(g_.size(), 0); // 0=new 1=on-stack 2=done
        // Explicit stack: (node, inputs_pushed). Rule 73: no recursion
        // depth limits.
        struct Frame { NodeId node; bool expanded; };
        std::vector<Frame> stack;

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
        // instr_of is built once, before the ownership pass (below)
        // so BOTH emission paths share it.
        std::vector<int64_t> instr_of(instr_of_shared_);


        for (auto& [node_id, mi] : per_node) {
            (void)mi;
            if (state[node_id] != 0) continue;
            // Owned nodes emit inside their select's branch region.
            if (select_global_owner_[node_id] != kInvalidNodeId &&
                !is_select_phi(g_, node_id)) {
                continue;
            }
            stack.push_back(Frame{node_id, false});
            while (!stack.empty()) {
                Frame& top = stack.back();
                // Capture the node id by value: every use of `top`
                // AFTER a push_back below would read through a
                // dangling reference when the stack vector reallocates
                // (ASan-caught; Rule 73).
                const NodeId top_node = top.node;
                // SELECT PHI -> real branch region. Branchless cmov
                // evaluates BOTH arms: unsound when an arm is
                // non-terminating (recursive fib(0) still ran
                // fib(-1)+fib(-2) and overflowed the stack — caught by
                // the runtime differential harness) or effectful.
                // Emitted: cond-closure; jz L_else; then-closure; mov
                // dst,then; jmp L_end; L_else:; else-closure; mov
                // dst,else; L_end:.
                if (g_[top_node].kind == NodeKind::Phi &&
                    g_[top_node].inputs.size() ==
                        ir::shape::kPhiInputs2Branches &&
                    g_[top_node].inputs[0] < g_.size() &&
                    g_[g_[top_node].inputs[0]].kind == NodeKind::Region) {
                    stack.pop_back();
                    if (state[top_node] == g_VisitDone) continue;
                    // The region emitter works from an 'emitted' view;
                    // mirror the scheduler's visit state into it and
                    // back (single-source tracking, two views).
                    std::vector<uint8_t> emitted(g_.size(), 0);
                    for (NodeId q = 0; q < g_.size(); ++q) {
                        if (state[q] == g_VisitDone) emitted[q] = 1;
                    }
                    emitted[top_node] = 1;
                    EmitCtx c{&per_node, &instr_of, &emitted,
                              &vreg_per_node, &next_vreg, &mf};
                    emit_select_region(top_node, c);
                    for (NodeId q = 0; q < g_.size(); ++q) {
                        if (emitted[q] != 0) state[q] = g_VisitDone;
                    }
                    continue;
                }
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
                        // Follow call-value projections through to the
                        // call (see the structured emitter's note).
                        if (instr_of[in] == -1) {
                            if (!is_call_value_proj(g_, in)) continue;
                            in = g_[in].inputs[0];
                            if (instr_of[in] == -1) continue;
                        }
                        // Owned children emit inside their select.
                        if (select_global_owner_[in] != kInvalidNodeId &&
                            !is_select_phi(g_, in)) {
                            continue;
                        }
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

        // Cycle guard (Rule D.3): every collected node must have been
        // EMITTED (select phis emit branch REGIONS whose instruction
        // count differs from their collected entry, so a size
        // comparison is meaningless — check emission state per node).
        for (const auto& ni : per_node) {
            if (state[ni.node] != g_VisitDone) {
                std::fprintf(stderr,
                             "instrsel: node %u never scheduled "
                             "(use cycle?)\n", ni.node);
                std::abort();
            }
        }
    }
    return mf;
}

// ============================================================
// Structured emission (loops, selects, effect chain).
// ============================================================
//
// The canonical source-level loop shape (from the for-statement
// lowering):
//
//     Loop(back=body_ctrl, entry)
//       phi_k = phi(Loop, {entry_k, back_k})
//       cond  = cmp(phi_primary, bound)
//       If(Loop, cond) -> Proj0 (body ctrl) ... body ... back to Loop
//                       -> Proj1 (exit ctrl); eff_phi = phi(Proj1,
//                                             {entry_eff, body_eff})
//
// One loop's region (all phis hold machine registers for the whole
// loop; updates happen IN PLACE at the back edge):
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
// post-loop read convention of the lowering.
//
// NESTED loops: when a body closure walk reaches an inner loop's
// phi, the inner loop's REGION is emitted recursively at that point
// (its preheader inits re-run every outer iteration — correct, they
// are inside the outer body), and the inner phi is then a leaf whose
// vreg the inner machinery defines.
//
// SIDE-EFFECT ORDER: result-unused calls are reachable only through
// the EFFECT CHAIN, never from data roots. The structured path walks
// the chain from the return's effect input as ordered EVENTS
// (pre-loop calls ... loop barrier ... post-loop calls): a barrier
// emits its loop region (whose body roots include the body effect
// tail, emitting the body's calls INSIDE the region, in chain
// order). Without this, `for i in 0..n { tick(i); s = s + 2; }`
// silently dropped tick entirely — enforced loudly by the harness's
// IR-call-count == MF-call-count guard.

namespace {

// Is this node one of the three Call kinds?
bool is_call_kind(NodeKind k) noexcept {
    return k == NodeKind::CallPure || k == NodeKind::CallAltered ||
           k == NodeKind::CallCrowded;
}

// Is this node an EFFECT-MERGE phi (the lowering's loop-exit effect
// merge: a phi whose region input is a control Proj, not a Region or
// Loop)? It carries no instruction and no vreg; a walk reaching it
// passes through to its two effect values.
bool is_eff_merge_phi(const Graph& g, NodeId id) {
    if (id >= g.size()) return false;
    const Node& n = g[id];
    return n.kind == NodeKind::Phi && !n.inputs.empty() &&
           n.inputs[0] < g.size() &&
           g[n.inputs[0]].kind == NodeKind::Proj;
}

} // namespace

VRegId InstrSelector::vreg_of_node(NodeId id, EmitCtx& c) {
    if (id >= g_.size()) return kInvalidVReg;
    if ((*c.vreg_of)[id] != kInvalidVReg) return (*c.vreg_of)[id];
    return (*c.vreg_of)[id] = (*c.next_vreg)++;
}

void InstrSelector::emit_value_closure(NodeId root, EmitCtx& c,
                                       NodeId eff_scope) {
    if (root == kInvalidNodeId || root >= g_.size()) return;

    // Root-level special cases.
    if (is_loop_phi_pub(g_, root)) {
        // Defined by its loop's machinery: make sure the region exists.
        const NodeId loop = g_[root].inputs[0];
        if (loop < g_.size() && loop_emitted_[loop] == 0) {
            emit_loop_region(loop, c);
        }
        return;
    }
    if (is_call_value_proj(g_, root)) {
        root = g_[root].inputs[0]; // follow to the call
    }
    if (is_eff_merge_phi(g_, root)) {
        // Pass through to both effect values (pre-loop and body tail;
        // the loop region emits the body side once it runs).
        emit_value_closure(g_[root].inputs[1], c, eff_scope);
        if (g_[root].inputs.size() >= ir::shape::kPhiInputs2Branches) {
            emit_value_closure(g_[root].inputs[2], c, eff_scope);
        }
        return;
    }
    if ((*c.instr_of)[root] == -1) return;      // structural: no instr
    if ((*c.emitted)[root] != 0) return;

    struct Frame { NodeId node; bool expanded; };
    std::vector<Frame> st;
    std::vector<uint8_t> onpath(g_.size(), 0);
    st.push_back(Frame{root, false});

    // Push one child if it will produce an instruction (following
    // call-value projections through to the call).
    auto try_push = [&](NodeId in) {
        if (in == kInvalidNodeId || in >= g_.size()) return;
        if (is_loop_phi_pub(g_, in)) {
            // Inner loop's phi: emit its region NOW (this is where the
            // nested loop executes), then it is a leaf.
            const NodeId loop = g_[in].inputs[0];
            if (loop < g_.size() && loop_emitted_[loop] == 0) {
                emit_loop_region(loop, c);
            }
            return;
        }
        if (is_call_value_proj(g_, in)) {
            in = g_[in].inputs[0];
            if (in >= g_.size()) return;
        }
        if ((*c.instr_of)[in] == -1) return;
        if ((*c.emitted)[in] != 0) return;
        if (onpath[in] != 0) return;
        st.push_back(Frame{in, false});
    };

    while (!st.empty()) {
        const NodeId node = st.back().node;

        // Select phi -> guarded branch region.
        if (is_select_phi(g_, node)) {
            st.pop_back();
            if ((*c.emitted)[node] == 0) {
                (*c.emitted)[node] = 1;
                emit_select_region(node, c);
            }
            continue;
        }
        if (is_eff_merge_phi(g_, node)) {
            // Pass through (a merge reached as a child of a call's
            // effect edge).
            st.pop_back();
            emit_value_closure(g_[node].inputs[1], c, eff_scope);
            if (g_[node].inputs.size() >= ir::shape::kPhiInputs2Branches) {
                emit_value_closure(g_[node].inputs[2], c, eff_scope);
            }
            continue;
        }

        if (!st.back().expanded) {
            st.back().expanded = true; // BEFORE pushes (Rule 73)
            if (onpath[node] == 0) onpath[node] = 1;
            const auto& ins = g_[node].inputs;
            for (size_t k = ins.size(); k-- > 0;) {
                NodeId in = ins[k];
                // Scoped effect descent: when walking a select arm's
                // effect calls, do not chase effect edges out of the
                // arm (a pre-branch call must stay outside the guarded
                // region — first-writer emission would otherwise place
                // it inside).
                if (k == 1 && !g_[node].is_pure() &&
                    eff_scope != kInvalidNodeId &&
                    (in >= g_.size() || g_[in].ctrl_in() != eff_scope)) {
                    continue;
                }
                if (is_eff_merge_phi(g_, in)) {
                    // Effect merges reached as children pass through.
                    try_push(g_[in].inputs[1]);
                    if (g_[in].inputs.size() >= ir::shape::kPhiInputs2Branches) {
                        try_push(g_[in].inputs[2]);
                    }
                    continue;
                }
                try_push(in);
            }
            continue;
        }
        st.pop_back();
        if ((*c.emitted)[node] != 0) continue;
        (*c.emitted)[node] = 1;
        c.mf->instrs.push_back(
            (*c.per_node)[static_cast<size_t>((*c.instr_of)[node])].mi);
    }
}

void InstrSelector::emit_select_region(NodeId phi, EmitCtx& c) {
    const NodeId region = g_[phi].inputs[0];
    const NodeId if_node = governing_if(g_, region);
    if (if_node == kInvalidNodeId) return; // guard rejects downstream
    if (g_[if_node].inputs.size() != ir::shape::kIfInputs) return;
    const NodeId cond = g_[if_node].inputs[ir::shape::kIfCondIndex];

    // The If's two projections (arm control).
    NodeId true_proj = kInvalidNodeId;
    NodeId false_proj = kInvalidNodeId;
    for (NodeId u : g_.users_snapshot(if_node)) {
        if (u >= g_.size() || g_[u].kind != NodeKind::Proj) continue;
        if (g_[u].payload.proj_index == 0) true_proj = u;
        else if (g_[u].payload.proj_index == 1) false_proj = u;
    }

    // Arm effect roots: calls controlled by each projection (a
    // result-unused call inside a branch is reachable only via the
    // effect chain). Rooted INSIDE their half, with scoped effect
    // descent so pre-branch calls are not pulled in.
    auto arm_effect_calls = [&](NodeId proj) {
        std::vector<NodeId> out;
        if (proj == kInvalidNodeId) return out;
        for (NodeId u : g_.users_snapshot(proj)) {
            if (u >= g_.size()) continue;
            if (g_[u].flags.has(NodeFlagBit::IsDead)) continue;
            if (!is_call_kind(g_[u].kind)) continue;
            if (g_[u].ctrl_in() != proj) continue;
            out.push_back(u);
        }
        return out;
    };

    const int64_t l_else = static_cast<int64_t>(select_label_next_++);
    const int64_t l_end  = static_cast<int64_t>(select_label_next_++);
    const NodeId then_v = g_[phi].inputs[1];
    const NodeId else_v = g_[phi].inputs[2];

    emit_value_closure(cond, c);
    {
        MachineInstr jz; jz.op = "jz";
        jz.uses[0] = vreg_of_node(cond, c);
        jz.has_imm = true; jz.imm = l_else;
        c.mf->instrs.push_back(jz);
    }
    // THEN half: value closure, then its effect calls (scoped).
    emit_value_closure(then_v, c);
    for (NodeId call : arm_effect_calls(true_proj)) {
        emit_value_closure(call, c, true_proj);
    }
    {
        MachineInstr mv; mv.op = "mov";
        mv.defs[0] = vreg_of_node(phi, c);
        mv.uses[0] = vreg_of_node(then_v, c);
        c.mf->instrs.push_back(mv);
        MachineInstr jp; jp.op = "jmp"; jp.has_imm = true; jp.imm = l_end;
        c.mf->instrs.push_back(jp);
        MachineInstr le; le.op = "label"; le.has_imm = true;
        le.imm = l_else;
        c.mf->instrs.push_back(le);
    }
    // ELSE half: value closure, then its effect calls (scoped).
    emit_value_closure(else_v, c);
    for (NodeId call : arm_effect_calls(false_proj)) {
        emit_value_closure(call, c, false_proj);
    }
    {
        MachineInstr mv; mv.op = "mov";
        mv.defs[0] = vreg_of_node(phi, c);
        mv.uses[0] = vreg_of_node(else_v, c);
        c.mf->instrs.push_back(mv);
        MachineInstr le; le.op = "label"; le.has_imm = true;
        le.imm = l_end;
        c.mf->instrs.push_back(le);
    }
}

void InstrSelector::emit_loop_region(NodeId loop, EmitCtx& c) {
    if (loop >= g_.size() || loop_emitted_[loop] != 0) return;
    loop_emitted_[loop] = 1;

    // The loop's phis, exit If, and exit condition.
    std::vector<NodeId> phis;
    NodeId exit_if = kInvalidNodeId;
    for (NodeId u : g_.users_snapshot(loop)) {
        if (u >= g_.size()) continue;
        if (g_[u].flags.has(NodeFlagBit::IsDead)) continue;
        if (g_[u].kind == NodeKind::Phi && !g_[u].inputs.empty() &&
            g_[u].inputs[0] == loop) {
            phis.push_back(u);
        } else if (g_[u].kind == NodeKind::If && g_[u].ctrl_in() == loop) {
            exit_if = u;
        }
    }
    NodeId cond = kInvalidNodeId;
    if (exit_if != kInvalidNodeId &&
        g_[exit_if].inputs.size() == ir::shape::kIfInputs) {
        cond = g_[exit_if].inputs[ir::shape::kIfCondIndex];
    }

    // Body effect tail: the loop's effect-merge phi hangs off the exit
    // projection; its back value is the last effect node in the body.
    NodeId eff_root = kInvalidNodeId;
    if (exit_if != kInvalidNodeId) {
        for (NodeId proj : g_.users_snapshot(exit_if)) {
            if (proj >= g_.size() || g_[proj].kind != NodeKind::Proj)
                continue;
            for (NodeId m : g_.users_snapshot(proj)) {
                if (m >= g_.size() || g_[m].kind != NodeKind::Phi)
                    continue;
                if (!g_[m].inputs.empty() && g_[m].inputs[0] == proj &&
                    g_[m].inputs.size() >=
                        ir::shape::kPhiInputs2Branches) {
                    eff_root = g_[m].inputs[2];
                }
            }
        }
    }

    const int64_t head_label = static_cast<int64_t>(loop_label_next_++);
    const int64_t exit_label = static_cast<int64_t>(loop_label_next_++);

    // Preheader: entry closures + init movs. CONSTANT entries
    // rematerialize as mov_imm instead of copying a constant vreg:
    // a phi initialized from a constant would otherwise keep that
    // constant's register live across the WHOLE enclosing loop (and
    // across any call in it, burning a callee-saved home — found via
    // the nested-loop spill dump).
    for (NodeId phi : phis) {
        if (g_[phi].inputs.size() < ir::shape::kPhiInputs2Branches)
            continue;
        const NodeId entry = g_[phi].inputs[1];
        MachineInstr init;
        init.defs[0] = vreg_of_node(phi, c);
        if (entry < g_.size() &&
            g_[entry].kind == NodeKind::Constant) {
            init.op = "mov_imm";
            init.has_imm = true;
            init.imm = g_[entry].payload.i64;
        } else {
            emit_value_closure(entry, c);
            init.op = "mov";
            init.uses[0] = vreg_of_node(entry, c);
        }
        c.mf->instrs.push_back(init);
    }

    // Head label.
    {
        MachineInstr lab; lab.op = "label"; lab.has_imm = true;
        lab.imm = head_label;
        c.mf->instrs.push_back(lab);
    }

    // Body closure. ORDER MATTERS for register pressure: the body's
    // CALLS emit first (effect-chain order — their result-unused
    // instances are reachable only from here), then the PURE
    // back-value computations and the exit condition. Pure placement
    // is free in a sea of nodes; emitting them AFTER the calls keeps
    // their intervals from spanning the call, so they stay in
    // caller-saved homes (emitting them first forced every body
    // temporary callee-saved and spilled — caught by the bare-call
    // bench case the moment the call-count guard made it runnable).
    emit_value_closure(eff_root, c);
    for (NodeId phi : phis) {
        if (g_[phi].inputs.size() < ir::shape::kPhiInputs2Branches)
            continue;
        emit_value_closure(g_[phi].inputs[2], c);
    }
    emit_value_closure(cond, c);

    // Exit check + back-edge updates + jump.
    {
        MachineInstr jz; jz.op = "jz";
        if (cond != kInvalidNodeId && cond < g_.size()) {
            jz.uses[0] = vreg_of_node(cond, c);
        }
        jz.has_imm = true; jz.imm = exit_label;
        c.mf->instrs.push_back(jz);
    }
    for (NodeId phi : phis) {
        if (g_[phi].inputs.size() < ir::shape::kPhiInputs2Branches)
            continue;
        MachineInstr upd; upd.op = "mov";
        upd.defs[0] = vreg_of_node(phi, c);
        upd.uses[0] = vreg_of_node(g_[phi].inputs[2], c);
        c.mf->instrs.push_back(upd);
    }
    {
        MachineInstr jmp; jmp.op = "jmp"; jmp.has_imm = true;
        jmp.imm = head_label;
        c.mf->instrs.push_back(jmp);
    }
    {
        MachineInstr lab; lab.op = "label"; lab.has_imm = true;
        lab.imm = exit_label;
        c.mf->instrs.push_back(lab);
    }
}

void InstrSelector::emit_structured_loops(
    const std::vector<NodeId>& loops, std::vector<NodeInstr>& per_node,
    std::vector<VRegId>& vreg_per_node, VRegId& next_vreg,
    MachineFunction& mf) {
    (void)loops;
    // Per-function emission state.
    loop_emitted_.assign(g_.size(), 0);
    loop_label_next_ = 0;
    std::vector<uint8_t> emitted(g_.size(), 0);
    EmitCtx c{&per_node, &instr_of_shared_, &emitted, &vreg_per_node,
              &next_vreg, &mf};

    // ---- Prologue: ALL parameter instructions first. ----
    // ABI argument registers are also vreg homes; params execute
    // exactly once at entry, never inside a loop body.
    for (const auto& ni : per_node) {
        if (ni.mi.op != "param") continue;
        mf.instrs.push_back(ni.mi);
        emitted[ni.node] = 1;
    }

    // ---- Effect-chain events, in chain order. ----
    //
    // Walk BACKWARDS from the return's effect input: each call is an
    // event; each loop's effect-merge phi is a BARRIER (the loop runs
    // at its chain position; the global walk continues along the
    // phi's ENTRY value — the body side is the loop region's own
    // business). Reversing gives program order.
    NodeId ret_node = kInvalidNodeId;
    for (const auto& ni : per_node) {
        if (ni.mi.op == "ret") { ret_node = ni.node; break; }
    }
    std::vector<NodeId> events; // forward order after reverse below
    if (ret_node != kInvalidNodeId) {
        const Node& rn = g_[ret_node];
        NodeId cur = rn.inputs.size() >= 2 ? rn.inputs[1]
                                           : kInvalidNodeId;
        std::vector<NodeId> rev;
        uint32_t guard = 0;
        while (cur != kInvalidNodeId && cur < g_.size() &&
               guard++ < g_.size()) {
            if (is_eff_merge_phi(g_, cur)) {
                rev.push_back(cur); // loop barrier
                cur = g_[cur].inputs[1]; // entry chain continues
            } else if (is_call_kind(g_[cur].kind)) {
                rev.push_back(cur);
                cur = g_[cur].eff_in();
            } else {
                break; // Start projection / structural: chain start
            }
        }
        events.assign(rev.rbegin(), rev.rend());
    }

    // Execute events forward. Pre-loop calls emit at their position;
    // a loop barrier emits the loop's whole region (body calls inside,
    // via the region's own effect roots). Emitted[] dedups across the
    // data-side and effect-side walks.
    for (NodeId ev : events) {
        if (is_eff_merge_phi(g_, ev)) {
            // The loop owning this merge: its phi's region input is
            // the exit Proj; the Loop is the Proj's base's ctrl.
            // Simpler: find the Loop whose user set contains an If
            // whose proj user is this merge — walk from the merge's
            // region Proj back to its If, then to the If's ctrl (the
            // Loop node).
            const NodeId proj = g_[ev].inputs[0];
            const NodeId base = proj < g_.size() &&
                !g_[proj].inputs.empty() ? g_[proj].inputs[0]
                                         : kInvalidNodeId;
            if (base != kInvalidNodeId && base < g_.size() &&
                g_[base].kind == NodeKind::If &&
                g_[base].inputs.size() >= 1) {
                const NodeId loop = g_[base].inputs[0];
                if (loop < g_.size() &&
                    g_[loop].kind == NodeKind::Loop) {
                    emit_loop_region(loop, c);
                }
            }
            continue;
        }
        emit_value_closure(ev, c);
    }

    // ---- Any loop not yet emitted (e.g. a degenerate loop whose
    // value never flows to the return still must RUN its body). ----
    for (NodeId id = 0; id < g_.size(); ++id) {
        if (g_[id].flags.has(NodeFlagBit::IsDead)) continue;
        if (g_[id].kind != NodeKind::Loop) continue;
        emit_loop_region(id, c);
    }

    // ---- The return: value closure, effect closure, then the ret. ----
    if (ret_node != kInvalidNodeId) {
        const Node& rn = g_[ret_node];
        if (rn.inputs.size() >= ir::shape::kReturnInputs) {
            emit_value_closure(rn.inputs[ir::shape::kReturnValIndex], c);
            emit_value_closure(rn.inputs[1], c);
        }
        mf.instrs.push_back(
            per_node[static_cast<size_t>(instr_of_shared_[ret_node])].mi);
    }
}

} // namespace aegis
