// ============================================================
// aegis/backend/ElfConstants.hpp — Named ELF64 ABI format constants.
// ============================================================
// Laws:
//   Rule D.1 / D.2 — ABI definitions must be declarative tables of
//   named constants, never literals scattered through emitter logic.
//   Rule 73 — format/layout assumptions are enforced at the use site
//   via static_assert against these constants.
//
// Values follow the System V ABI / ELF-64 specification (elf.h).
// Every constant documents what it means in the ELF spec so the
// emitter reads like the spec.
// ============================================================
#pragma once

#include <cstdint>

namespace aegis::backend::elf {

// ---- e_ident (identification index constants) ----
/// EI_NIDENT: size of e_ident[] in the ELF header.
constexpr uint32_t kIdentSize{16};
/// Byte index of ELFCLASS within e_ident.
constexpr uint32_t kIdentIdxClass{4};
/// ELFCLASS64: 64-bit objects.
constexpr uint8_t  kElfClass64{2};
/// Byte index of endianness within e_ident.
constexpr uint32_t kIdentIdxData{5};
/// ELFDATA2LSB: two's-complement little-endian.
constexpr uint8_t  kElfData2Lsb{1};
/// Byte index of the ELF version within e_ident.
constexpr uint32_t kIdentIdxVersion{6};
/// EV_CURRENT: current ELF version.
constexpr uint8_t  kEvCurrent{1};

// ---- File types / machine ----
/// ET_REL: relocatable object file (what the AOT emitter produces).
constexpr uint16_t kEtRel{1};
/// EM_X86_64: AMD x86-64 architecture.
constexpr uint16_t kEmX86_64{62};

// ---- Structure sizes (ELF-64) ----
/// sizeof(Elf64_Ehdr): the ELF header is exactly 64 bytes.
constexpr uint32_t kEhdrSize{64};
/// sizeof(Elf64_Shdr): one section header entry.
constexpr uint32_t kShdrSize{64};
/// sizeof(Elf64_Sym): one symbol table entry.
constexpr uint32_t kSymSize{24};

// ---- Section types ----
/// SHT_PROGBITS: program-defined contents (.text).
constexpr uint32_t kShtProgbits{1};
/// SHT_SYMTAB: symbol table.
constexpr uint32_t kShtSymtab{2};
/// SHT_STRTAB: string table.
constexpr uint32_t kShtStrtab{3};

// ---- Section flags ----
/// SHF_ALLOC: section occupies memory at execution.
constexpr uint64_t kShfAlloc{0x2};
/// SHF_EXECINSTR: section contains executable machine code.
constexpr uint64_t kShfExecinstr{0x4};

// ---- Symbol binding / type ----
/// STB_GLOBAL: global symbol visibility.
constexpr uint8_t kStbGlobal{1};
/// STB_LOCAL: local (non-exported) symbol.
constexpr uint8_t kStbLocal{0};
/// st_info bind nibble shift: (bind << 4) | type.
constexpr uint8_t kStInfoBindShift{4};
/// STT_FUNC: symbol is a function.
constexpr uint8_t kSttFunc{2};
/// STV_DEFAULT: default symbol visibility.
constexpr uint8_t kStvDefault{0};

// ---- Section layout of the emitted object ----
/// Section index of the null (index 0) section header.
constexpr uint16_t kShndxNull{0};
/// Section index of .text in our emitted object.
constexpr uint16_t kShndxText{1};
/// Section index of .symtab in our emitted object.
constexpr uint16_t kShndxSymtab{2};
/// Section index of .strtab (referenced by .symtab's sh_link).
constexpr uint16_t kShndxStrtab{3};
/// Section index of .shstrtab (the section-name string table).
constexpr uint16_t kShndxShstrtab{4};
/// Total number of emitted sections: null + .text + .symtab +
/// .strtab + .shstrtab.
constexpr uint16_t kSectionCount{5};
/// Index of the first non-local symbol in .symtab (after STN_UNDEF).
constexpr uint32_t kFirstNonLocalSymbolIndex{1};

// ---- Alignment of the emitted sections ----
/// .text alignment: 16 bytes (max x86-64 SIMD alignment).
constexpr uint64_t kTextAlign{16};
/// .symtab alignment: 8 bytes (Elf64_Sym contains u64 fields).
constexpr uint64_t kSymtabAlign{8};
/// String tables align to 1 byte.
constexpr uint64_t kStrtabAlign{1};

// ---- Little-endian byte serialization ----
/// Mask of one byte.
constexpr uint32_t kByteMask{0xff};
/// Bits per byte (serialization shift unit).
constexpr uint32_t kBitsPerByte{8};
/// Bytes in a u32 (put_u32 emits exactly this many).
constexpr uint32_t kU32Bytes{4};
/// Bytes in a u64 (put_u64 emits exactly this many).
constexpr uint32_t kU64Bytes{8};

} // namespace aegis::backend::elf
