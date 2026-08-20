// ir/Printer.h + .cpp — pretty-printer for IR graphs (debugging only).
#pragma once

#include <string>
#include <string_view>

#include "ir/Graph.h"

namespace aegis {

// Print a single node to a string. For debugging / golden-test output.
std::string format_node(const Graph& g, NodeId id);

// Print the whole graph as a textual SoN listing.
std::string format_graph(const Graph& g);

// Print only the effect chain (Crowded + Altered + Return roots) in
// topological order. Useful for E-DCE debugging.
std::string format_effect_chain(const Graph& g);

} // namespace aegis
