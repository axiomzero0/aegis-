// ============================================================
// frontend/EffectInference.h — Infers Pure/Altered/Crowded per fn.
// ============================================================
// Spec §3:
//   Pure     — only reads args + immutable locals; calls only Pure.
//   Altered  — writes to a &mut / mutable global / local var.
//   Crowded  — calls any std.io, std.atomic, std.thread, FFI.
// ============================================================
#pragma once
#include "aegis/frontend/AST.hpp"
#include "aegis/support/Flags.hpp"

namespace aegis {

// Per-function inferred effect (the function's "effect class").
enum class InferredEffect : uint8_t {
    Pure    = 0,
    Altered = 1,
    Crowded = 2,
};

// SymbolId-keyed effect table built by the frontend.
struct FunctionEffectInfo {
    SymbolId     fn_name{kInvalidSymbolId};
    InferredEffect effect{InferredEffect::Pure};
    bool          has_mut_ref_param{false};
    bool          writes_to_local_var{false};
    bool          calls_io{false};
    bool          calls_atomic{false};
    bool          calls_thread{false};
};

// Walk an ASTFnDecl and compute its effect class. Returns the inferred
// effect. DiagnosticSink receives any warnings (e.g. "function marked
// Pure but writes through &mut").
InferredEffect infer_function_effect(const ASTFnDecl& fn, class SymbolTable* syms);

} // namespace aegis
