// frontend/ASTPrinter.h — debug printer for AST.
#pragma once
#include <string>
#include "aegis/frontend/AST.hpp"
namespace aegis {
std::string format_ast(const ASTModule& mod);
std::string format_ast_node(const ASTNode& n, int indent);
} // namespace aegis
