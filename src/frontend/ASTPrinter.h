// frontend/ASTPrinter.h — debug printer for AST.
#pragma once
#include <string>
#include "frontend/AST.h"
namespace aegis {
std::string format_ast(const ASTModule& mod);
std::string format_ast_node(const ASTNode& n, int indent);
} // namespace aegis
