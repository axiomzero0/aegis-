// backend/x86/Emitter_x86.hpp — Real ELF object file writer.
// ============================================================
// Emits a relocatable ELF64 object file. For the prototype we emit a
// minimal ELF with one .text section containing the compiled function's
// machine code bytes.
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "aegis/backend/Emitter.hpp"
#include "aegis/backend/MachineIR.hpp"

namespace aegis::backend::x86 {

class EmitterX8664 : public Emitter {
public:
    EmitterX8664() = default;
    void emit_function(const MachineFunction& fn) override;
    void finalize(std::vector<uint8_t>& out_bytes) override;
private:
    std::vector<uint8_t> text_bytes_{};
    std::vector<std::string> symbol_names_{};
    std::vector<uint32_t>    symbol_offsets_{};
};

} // namespace aegis::backend::x86
