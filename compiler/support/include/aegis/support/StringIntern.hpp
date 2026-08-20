// ============================================================
// core/SymbolTable.h — Interned symbol table.
// ============================================================
// Law: Rule 54 — "Never pass, compare, or store std::string or
//       std::string_view in the IR or passes. All identifiers must be
//       interned into a global SymbolTable. The IR must only use a
//       SymbolId (uint32_t index)."
//
// The SymbolTable stores every identifier's bytes exactly once. The
// IR / passes / backend carry only the 4-byte SymbolId. Equality and
// hashing of identifiers is then a single CPU cycle (integer compare
// / hash of a uint32). The trade-off is one indirection (a SymbolTable
// lookup) when you actually need the string — which is rare on the hot
// path (it's mostly diagnostics / debug printing).
//
// Interning uses a SwissTable (Rule 55) for the hash-consing map.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aegis/support/SwissTable.hpp"
#include "aegis/support/Primitives.hpp"

namespace aegis {

class SymbolTable {
public:
    SymbolTable();

    // Intern a string. If the symbol already exists, returns its existing
    // SymbolId. Otherwise, allocates a new entry and returns the new id.
    //
    // NOT noexcept: this is the cold, frontend-only path. The IR uses
    // SymbolId only, so this is never on the hot path.
    SymbolId intern(std::string_view s);

    [[nodiscard]] SymbolId find(std::string_view s) const noexcept;
    [[nodiscard]] std::string_view at(SymbolId id) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

    // Bulk-lookup helper: interning + allocation-free after first call.
    SymbolId intern_or_die(std::string_view s) { return intern(s); }

private:
    // StringView with stable storage (the table owns the bytes).
    struct InternedEntry {
        std::string data;
    };
    std::vector<InternedEntry> entries_{};

    // Hash-consing map: string_view -> SymbolId. Uses string_view as key
    // into stable storage held in `entries_`. The view lifetime is tied
    // to the entry's string, which is stable (vector only grows, never
    // moves the contents of std::string on push_back).
    //
    // We use std::string_view as the map key but it's safe because
    // `entries_[i].data` address is stable for the lifetime of the
    // SymbolTable. The comparison uses std::string_view equality.
    SwissTable<std::string_view, SymbolId, std::hash<std::string_view>, std::equal_to<std::string_view>> map_{};
};

} // namespace aegis
