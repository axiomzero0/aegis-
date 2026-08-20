// core/SymbolTable.cpp — string interning implementation.
#include "aegis/support/StringIntern.hpp"

#include <cstring>

namespace aegis {

SymbolTable::SymbolTable() {
    // Pre-intern the empty symbol as id=0 (used by Span::file_id when no
    // source file exists).
    intern(std::string_view{});
}

SymbolId SymbolTable::intern(std::string_view s) {
    if (SymbolId id = find(s); id != kInvalidSymbolId) return id;

    SymbolId new_id = static_cast<SymbolId>(entries_.size());
    entries_.push_back(InternedEntry{std::string(s)});
    // Insert the stable view. The address of the std::string in `entries_`
    // is stable because we only ever push_back and never resize smaller.
    std::string_view stable_view{entries_.back().data};
    map_.insert(stable_view, new_id);
    return new_id;
}

SymbolId SymbolTable::find(std::string_view s) const noexcept {
    // SwissTable's const overload returns `const SymbolId*`; we dereference
    // to read it. No mutation of the table happens on the const path.
    if (const SymbolId* p = map_.get(s); p != nullptr) {
        return *p;
    }
    return kInvalidSymbolId;
}

std::string_view SymbolTable::at(SymbolId id) const noexcept {
    if (id >= entries_.size()) return {};
    return entries_[id].data;
}

} // namespace aegis
