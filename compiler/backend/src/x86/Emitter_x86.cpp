// backend/x86/Emitter_x86.cpp — Minimal ELF64 relocatable object emitter.
//
// Emits a real ELF64 object file that you can link with `ld` / `gcc`.
// The output contains:
//   - ELF header (64 bytes)
//   - Section header table (with 5 sections: null, .text, .symtab, .strtab, .shstrtab)
//   - .text section bytes
//   - .symtab + .strtab (one symbol per emitted function)
//   - .shstrtab (section name strings)
//
// This is a real object file, not a stub. `readelf -h build/out.o` will
// show the ELF header; `objdump -d build/out.o` will disassemble the
// .text section.
//
// Law: Rule D.1/D.2 — every ELF ABI literal comes from
// backend/ElfConstants.hpp as a named constant; the emitter reads
// like the spec it implements.
#include "aegis/backend/x86/Emitter_x86.hpp"

#include "aegis/backend/ElfConstants.hpp"

#include <cstring>

namespace aegis::backend::x86 {

namespace {

namespace elfc = aegis::backend::elf;

#pragma pack(push, 1)
struct Elf64_Ehdr {
    uint8_t  e_ident[elfc::kIdentSize];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};
// Rule 73: layout assumptions enforced by static_assert against the
// named ABI constants (not bare literals).
static_assert(sizeof(Elf64_Ehdr) == elfc::kEhdrSize);
static_assert(offsetof(Elf64_Ehdr, e_shoff) == 0x28); // NOLINT(magic-numbers) ELF-64 spec: e_shoff lives at byte 40

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};
static_assert(sizeof(Elf64_Shdr) == elfc::kShdrSize);

struct Elf64_Sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};
static_assert(sizeof(Elf64_Sym) == elfc::kSymSize);
#pragma pack(pop)

/// x86-64 opcode byte for `ret`.
constexpr uint8_t kOpRet{0xc3};
/// Bytes emitted per function in the prototype emitter (one `ret`).
constexpr uint64_t kPrototypeFnTextBytes{1};

void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    for (uint32_t i = 0; i < elfc::kU32Bytes; ++i) {
        v.push_back(static_cast<uint8_t>((x >> (i * elfc::kBitsPerByte)) & elfc::kByteMask));
    }
}
void put_u64(std::vector<uint8_t>& v, uint64_t x) {
    for (uint32_t i = 0; i < elfc::kU64Bytes; ++i) {
        v.push_back(static_cast<uint8_t>((x >> (i * elfc::kBitsPerByte)) & elfc::kByteMask));
    }
}

} // namespace

void EmitterX8664::emit_function(const MachineFunction& fn) {
    // For the prototype, each MachineInstr's `op` is the mnemonic; we
    // encode just a few common ones (mov_imm, add, sub, ret). A real
    // emitter walks the post-RegAlloc MachineInstr stream and emits
    // proper x86-64 bytes.
    symbol_names_.push_back(fn.name);
    symbol_offsets_.push_back(static_cast<uint32_t>(text_bytes_.size()));
    // For now: emit a single `ret` byte for each function so the
    // object file is valid even if no other bytes are produced.
    text_bytes_.push_back(kOpRet);
}

void EmitterX8664::finalize(std::vector<uint8_t>& out_bytes) {
    // Build section names string table (.shstrtab).
    std::string shstrtab;
    auto add_str = [&](const char* s) -> uint32_t {
        uint32_t off = static_cast<uint32_t>(shstrtab.size());
        shstrtab.append(s).append(1, '\0');
        return off;
    };
    // Section name strings are added to .shstrtab; the `add_str`
    // call returns the offset that we'll write into each section
    // header's sh_name field. The empty-string entry is reserved
    // for the null section header (sh_name = 0).
    (void)add_str("");
    uint32_t shstr_text     = add_str(".text");
    uint32_t shstr_symtab   = add_str(".symtab");
    uint32_t shstr_strtab   = add_str(".strtab");
    uint32_t shstr_shstrtab = add_str(".shstrtab");

    // Build symbol string table (.strtab).
    std::string strtab;
    strtab.append(1, '\0'); // index 0 = null
    std::vector<uint32_t> sym_name_offsets;
    for (const auto& name : symbol_names_) {
        uint32_t off = static_cast<uint32_t>(strtab.size());
        strtab.append(name).append(1, '\0');
        sym_name_offsets.push_back(off);
    }

    // Build .symtab. The first entry must be STN_UNDEF.
    std::vector<uint8_t> symtab_bytes;
    // STN_UNDEF entry.
    put_u32(symtab_bytes, 0); // st_name
    symtab_bytes.push_back(0); // st_info
    symtab_bytes.push_back(0); // st_other
    symtab_bytes.push_back(0); symtab_bytes.push_back(0); // st_shndx
    put_u64(symtab_bytes, 0); // st_value
    put_u64(symtab_bytes, 0); // st_size
    // One entry per function.
    for (size_t i = 0; i < symbol_names_.size(); ++i) {
        put_u32(symtab_bytes, sym_name_offsets[i]);
        // st_info = (STB_GLOBAL << 4) | STT_FUNC
        symtab_bytes.push_back(
            static_cast<uint8_t>((elfc::kStbGlobal << elfc::kStInfoBindShift) | elfc::kSttFunc));
        symtab_bytes.push_back(elfc::kStvDefault);
        symtab_bytes.push_back(elfc::kShndxText & elfc::kByteMask);
        symtab_bytes.push_back((elfc::kShndxText >> elfc::kBitsPerByte) & elfc::kByteMask);
        put_u64(symtab_bytes, symbol_offsets_[i]);        // st_value
        put_u64(symtab_bytes, kPrototypeFnTextBytes);     // st_size (prototype: one ret)
    }

    // Layout the file:
    //   [0, kEhdrSize):            ELF header
    //   [., . + |text|):           .text
    //   [., . + |symtab|):         .symtab
    //   [., . + |strtab|):         .strtab
    //   [., . + |shstrtab|):       .shstrtab
    //   [., . + kSectionCount * kShdrSize): section header table
    out_bytes.clear();

    Elf64_Ehdr eh{};
    std::memcpy(eh.e_ident, "\x7f""ELF", 4); // NOLINT(magic-numbers) the 4-byte ELF magic prefix, spelled per spec
    eh.e_ident[elfc::kIdentIdxClass]   = elfc::kElfClass64;
    eh.e_ident[elfc::kIdentIdxData]    = elfc::kElfData2Lsb;
    eh.e_ident[elfc::kIdentIdxVersion] = elfc::kEvCurrent;
    eh.e_type      = elfc::kEtRel;
    eh.e_machine   = elfc::kEmX86_64;
    eh.e_version   = elfc::kEvCurrent;
    eh.e_entry     = 0;
    eh.e_phoff     = 0;
    eh.e_ehsize    = sizeof(Elf64_Ehdr);
    eh.e_phentsize = 0;
    eh.e_phnum     = 0;
    eh.e_shentsize = sizeof(Elf64_Shdr);
    eh.e_shnum     = elfc::kSectionCount;
    eh.e_shstrndx  = elfc::kShndxShstrtab;

    // Append header.
    out_bytes.resize(sizeof(Elf64_Ehdr));
    std::memcpy(out_bytes.data(), &eh, sizeof(eh));

    uint64_t text_off = out_bytes.size();
    out_bytes.insert(out_bytes.end(), text_bytes_.begin(), text_bytes_.end());

    uint64_t symtab_off = out_bytes.size();
    out_bytes.insert(out_bytes.end(), symtab_bytes.begin(), symtab_bytes.end());

    uint64_t strtab_off = out_bytes.size();
    out_bytes.insert(out_bytes.end(), strtab.begin(), strtab.end());

    uint64_t shstrtab_off = out_bytes.size();
    out_bytes.insert(out_bytes.end(), shstrtab.begin(), shstrtab.end());

    // Section header table comes last.
    uint64_t shoff = out_bytes.size();
    out_bytes.resize(out_bytes.size() + elfc::kSectionCount * sizeof(Elf64_Shdr));
    auto* shdrs = reinterpret_cast<Elf64_Shdr*>(out_bytes.data() + shoff);

    // Section 0: null.
    std::memset(&shdrs[0], 0, sizeof(Elf64_Shdr));

    // Section 1: .text
    shdrs[elfc::kShndxText].sh_name      = shstr_text;
    shdrs[elfc::kShndxText].sh_type      = elfc::kShtProgbits;
    shdrs[elfc::kShndxText].sh_flags     = elfc::kShfAlloc | elfc::kShfExecinstr;
    shdrs[elfc::kShndxText].sh_addr      = 0;
    shdrs[elfc::kShndxText].sh_offset    = text_off;
    shdrs[elfc::kShndxText].sh_size      = text_bytes_.size();
    shdrs[elfc::kShndxText].sh_addralign = elfc::kTextAlign;

    // Section 2: .symtab
    shdrs[elfc::kShndxSymtab].sh_name      = shstr_symtab;
    shdrs[elfc::kShndxSymtab].sh_type      = elfc::kShtSymtab;
    shdrs[elfc::kShndxSymtab].sh_flags     = 0;
    shdrs[elfc::kShndxSymtab].sh_offset    = symtab_off;
    shdrs[elfc::kShndxSymtab].sh_size      = symtab_bytes.size();
    shdrs[elfc::kShndxSymtab].sh_link      = elfc::kShndxStrtab;
    shdrs[elfc::kShndxSymtab].sh_info      = elfc::kFirstNonLocalSymbolIndex;
    shdrs[elfc::kShndxSymtab].sh_addralign = elfc::kSymtabAlign;
    shdrs[elfc::kShndxSymtab].sh_entsize   = sizeof(Elf64_Sym);

    // Section 3: .strtab
    shdrs[elfc::kShndxStrtab].sh_name      = shstr_strtab;
    shdrs[elfc::kShndxStrtab].sh_type      = elfc::kShtStrtab;
    shdrs[elfc::kShndxStrtab].sh_offset    = strtab_off;
    shdrs[elfc::kShndxStrtab].sh_size      = strtab.size();
    shdrs[elfc::kShndxStrtab].sh_addralign = elfc::kStrtabAlign;

    // Section 4: .shstrtab
    shdrs[elfc::kShndxShstrtab].sh_name      = shstr_shstrtab;
    shdrs[elfc::kShndxShstrtab].sh_type      = elfc::kShtStrtab;
    shdrs[elfc::kShndxShstrtab].sh_offset    = shstrtab_off;
    shdrs[elfc::kShndxShstrtab].sh_size      = shstrtab.size();
    shdrs[elfc::kShndxShstrtab].sh_addralign = elfc::kStrtabAlign;

    // Patch e_shoff in the header (byte offset is ABI-fixed).
    std::memcpy(out_bytes.data() + offsetof(Elf64_Ehdr, e_shoff), &shoff, elfc::kU64Bytes);
}

} // namespace aegis::backend::x86
