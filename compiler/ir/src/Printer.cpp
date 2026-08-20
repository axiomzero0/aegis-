// ir/Printer.cpp — debug-printer for IR graphs.
#include "aegis/ir/Printer.hpp"

#include <charconv>
#include <sstream>

namespace aegis {

namespace {
std::string fmt_payload(NodeKind k, NodePayload p) {
    std::ostringstream os;
    switch (k) {
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
        default: break;
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
