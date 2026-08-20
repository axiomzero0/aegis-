// ============================================================
// aegis/backend/Emitter.hpp — Final machine code emission & object writing.
// ============================================================
// Law: Section §II "Backend & Low-Level":
//   "Prologue/Epilogue Generation: Function entry/exit code generation.
//    Exception Handling Table Generation: Unwind tables and landing pads.
//    Debug Information Generation: Maps machine code back to source."
//
// Emitter takes a finalized MachineFunction (post-RegAlloc, post-Peephole)
// and produces a relocatable object file.
// ============================================================
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "aegis/backend/MachineIR.hpp"

namespace aegis::backend {

enum class ObjectFormat : uint8_t {
    ELF,     // Linux/BSD
    MachO,   // macOS
    PE,      // Windows
    Raw,     // raw bytes (for JIT)
};

struct EmissionOptions {
    ObjectFormat format{ObjectFormat::ELF};
    bool         emit_unwind_tables{true};
    bool         emit_debug_info{false};
    bool         emit_relocations{true};
    std::string_view triple;     // e.g. "x86_64-unknown-linux-gnu"
};

class Emitter {
public:
    virtual ~Emitter() = default;
    virtual void emit_function(const MachineFunction& fn) = 0;
    virtual void finalize(std::vector<uint8_t>& out_bytes) = 0;
};

} // namespace aegis::backend
