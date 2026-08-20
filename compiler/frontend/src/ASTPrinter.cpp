// frontend/ASTPrinter.cpp — minimal AST printer.
#include "aegis/frontend/ASTPrinter.hpp"

#include <sstream>

namespace aegis {

namespace {
std::string ind(int n) { return std::string(n * 2, ' '); }
}

std::string format_ast_node(const ASTNode& n, int indent) {
    std::ostringstream os;
    os << ind(indent) << "(kind=" << static_cast<int>(n.kind) << ")";
    // Truncated: real printer is more elaborate. This exists to satisfy
    // the build graph; golden tests use the IR printer, not the AST
    // printer.
    return os.str();
}

std::string format_ast(const ASTModule& mod) {
    std::ostringstream os;
    os << "(module\n";
    for (const auto& it : mod.items) os << format_ast_node(*it, 1) << "\n";
    os << ")\n";
    return os.str();
}

} // namespace aegis
