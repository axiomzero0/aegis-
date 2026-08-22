// ir/Printer.cpp — debug-printer for IR graphs.
#include "aegis/ir/Printer.hpp"

#include <charconv>
#include <sstream>

#include "aegis/support/Assert.hpp"

namespace aegis {

namespace {
std::string fmt_payload(NodeKind k, NodePayload p) {
    std::ostringstream os;
    // Law: Rule D.3 — switch is exhaustive on the closed enum. Default
    // payload-less nodes print nothing; new NodeKinds MUST be added
    // here or the build will fail.
    switch (k) {
        // ---- Payload-bearing kinds ----
        case NodeKind::Constant:       os << " value=" << p.i64; break;
        case NodeKind::Parameter:      os << " sym=" << p.sym; break;
        case NodeKind::Proj:           os << " proj=" << p.proj_index; break;
        case NodeKind::GetFieldPtr:    os << " field=" << p.field_index; break;
        case NodeKind::CallPure:
        case NodeKind::CallAltered:
        case NodeKind::CallCrowded:    os << " callee=sym" << p.sym; break;
        case NodeKind::Guard:
        case NodeKind::Deopt:
        case NodeKind::FrameState:     os << " fs=" << p.u64; break;
        // ---- Payload-less kinds (print nothing) ----
        case NodeKind::Start:
        case NodeKind::Region:
        case NodeKind::Loop:
        case NodeKind::If:
        case NodeKind::Return:
        case NodeKind::Branch:
        case NodeKind::Stop:
        case NodeKind::Phi:
        case NodeKind::Add:
        case NodeKind::Sub:
        case NodeKind::Mul:
        case NodeKind::Div:
        case NodeKind::UDiv:
        case NodeKind::Mod:
        case NodeKind::UMod:
        case NodeKind::And:
        case NodeKind::Or:
        case NodeKind::Xor:
        case NodeKind::Shl:
        case NodeKind::Shr:
        case NodeKind::LShr:
        case NodeKind::CmpEq:
        case NodeKind::CmpNe:
        case NodeKind::CmpLt:
        case NodeKind::CmpLe:
        case NodeKind::CmpGt:
        case NodeKind::CmpGe:
        case NodeKind::CmpUlt:
        case NodeKind::CmpUle:
        case NodeKind::CmpUgt:
        case NodeKind::CmpUge:
        case NodeKind::Neg:
        case NodeKind::Not:
        case NodeKind::BitNot:
        case NodeKind::Load:
        case NodeKind::Store:
        case NodeKind::Alloc:
        case NodeKind::StackAlloc:
        case NodeKind::GetElementPtr:
        case NodeKind::Cast:
        case NodeKind::Select:
        case NodeKind::AtomicLoad:
        case NodeKind::AtomicStore:
        case NodeKind::AtomicRMW:
        case NodeKind::Fence:
        case NodeKind::ProfiledEntry:
        case NodeKind::MachineOp:
            break;
    }
    return os.str();
}

std::string fmt_inputs(const Graph& g, NodeId id) {
    const Node& n = g[id];
    std::ostringstream os;
    os << " ins=[";
    bool first = true;
    for (NodeId i : n.inputs) {
        if (!first) os << ",";
        if (i == kInvalidNodeId) os << "_";
        else os << i;
        first = false;
    }
    os << "]";
    return os.str();
}
} // namespace

std::string format_node(const Graph& g, NodeId id) {
    const Node& n = g[id];
    std::ostringstream os;
    os << "n" << id << " : " << node_kind_name(n.kind)
       << " (" << (n.is_pure() ? "Pure" : (n.is_altered() ? "Altered" : "Crowded")) << ")"
       << fmt_payload(n.kind, n.payload)
       << fmt_inputs(g, id);
    if (n.flags.has(NodeFlagBit::IsGuarded)) os << " +guarded";
    if (n.flags.has(NodeFlagBit::HasFrameState)) os << " +fs";
    if (n.flags.has(NodeFlagBit::IsPgoSpeculated)) os << " +pgo";
    if (n.flags.has(NodeFlagBit::IsDead)) os << " DEAD";
    return os.str();
}

std::string format_graph(const Graph& g) {
    std::ostringstream os;
    os << "graph v=" << g.version() << " n=" << g.size() << "\n";
    for (NodeId id = 0; id < g.size(); ++id) {
        os << format_node(g, id) << "\n";
    }
    return os.str();
}

std::string format_effect_chain(const Graph& g) {
    std::ostringstream os;
    for (NodeId id = 0; id < g.size(); ++id) {
        const Node& n = g[id];
        if (n.is_pure()) continue;
        os << format_node(g, id) << "\n";
    }
    return os.str();
}

} // namespace aegis
