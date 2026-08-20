// ir/Verifier.h + .cpp — Public verifier entry point (Rule 42).
#pragma once
#include <string>
#include "aegis/ir/Graph.hpp"

namespace aegis {
// Returns true if the graph is well-formed. On false, `why` carries a
// human-readable description of the violation.
bool verify_graph(const Graph& g, std::string& why);
} // namespace aegis
