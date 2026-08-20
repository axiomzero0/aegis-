// ============================================================
// aegis/pgo/ProfileData.hpp — Serialization/deserialization of profile data.
// ============================================================
// Law (Rule 50 — "No persistent state without versioning"):
//   "Profile caches, code caches, and AOT artifacts must be versioned.
//    A change in the IR format or pass order invalidates the cache."
//
// ProfileData is the on-disk format. The version stamp must match the
// compiler's current pipeline version; otherwise the JIT ignores the
// loaded profile and starts cold.
// ============================================================
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace aegis::pgo {

struct ProfileHeader {
    char     magic[8]{'A','E','G','I','S','P','G','O'};
    uint32_t version{1};         // bumped on any IR or pass-order change
    uint32_t compiler_revision{0}; // hash of the IR format + pass order
    uint64_t created_at{0};
    uint32_t num_counters{0};
};

class ProfileData {
public:
    ProfileData() = default;

    // Load from a byte buffer (e.g. memory-mapped profile file).
    [[nodiscard]] bool load(std::span<const uint8_t> bytes);

    // Save to a byte buffer (for write-out).
    [[nodiscard]] std::vector<uint8_t> save() const;

    [[nodiscard]] const ProfileHeader& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const uint64_t> counters() const noexcept { return counters_; }

private:
    ProfileHeader header_{};
    std::vector<uint64_t> counters_{};
};

} // namespace aegis::pgo
