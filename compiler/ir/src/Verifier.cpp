// ir/Verifier.cpp — delegates to Graph::verify (which implements Rule 42).
#include "aegis/ir/Verifier.hpp"

namespace aegis {
bool verify_graph(const Graph& g, std::string& why) {
    return g.verify(why);
}
} // namespace aegis
