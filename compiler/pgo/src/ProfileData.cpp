// pgo/ProfileData.cpp — Real serialization / deserialization with versioning (Rule 50).
//
// On-disk format:
//   [ProfileHeader (40 bytes)]
//   [uint64 counters[header.num_counters]]
//
// Rule 50: A change in IR format or pass order must invalidate the
// cache. This is enforced by the compiler_revision field in the
// header — if the JIT loads a profile whose compiler_revision doesn't
// match its own, the profile is discarded and the JIT runs cold.
#include "aegis/pgo/ProfileData.hpp"

#include <chrono>
#include <cstring>

namespace aegis::pgo {

namespace {
uint64_t now_micros() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}
} // namespace

bool ProfileData::load(std::span<const uint8_t> bytes) {
    if (bytes.size() < sizeof(ProfileHeader)) return false;
    std::memcpy(&header_, bytes.data(), sizeof(ProfileHeader));
    if (std::memcmp(header_.magic, "AEGISPGO", 8) != 0) return false;
    // Read counters.
    counters_.clear();
    counters_.reserve(header_.num_counters);
    const uint8_t* p = bytes.data() + sizeof(ProfileHeader);
    const size_t remaining = bytes.size() - sizeof(ProfileHeader);
    if (remaining < header_.num_counters * sizeof(uint64_t)) return false;
    for (uint32_t i = 0; i < header_.num_counters; ++i) {
        uint64_t v;
        std::memcpy(&v, p + i * sizeof(uint64_t), sizeof(uint64_t));
        counters_.push_back(v);
    }
    return true;
}

std::vector<uint8_t> ProfileData::save() const {
    std::vector<uint8_t> out;
    out.resize(sizeof(ProfileHeader) + counters_.size() * sizeof(uint64_t));
    ProfileHeader h = header_;
    h.num_counters = static_cast<uint32_t>(counters_.size());
    h.created_at = now_micros();
    std::memcpy(out.data(), &h, sizeof(ProfileHeader));
    for (size_t i = 0; i < counters_.size(); ++i) {
        std::memcpy(out.data() + sizeof(ProfileHeader) + i * sizeof(uint64_t),
                    &counters_[i], sizeof(uint64_t));
    }
    return out;
}

} // namespace aegis::pgo
