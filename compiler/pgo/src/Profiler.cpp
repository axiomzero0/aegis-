// pgo/Profiler.cpp — Real Profiler implementation.
//
// Law (Rule 46 — "No profile data without confidence"):
//   "Profile data must include sample count, stability, age, decay,
//    variance, and deopt correlation (Meter)."
//
// The Profiler exposes register_counter() for the AOT compiler to
// reserve slots. The runtime bumps counters via the counter(id)
// accessor. Confidence is updated by the JIT when it consults the
// profile (it bumps age + decay).
#include "aegis/pgo/Profiler.hpp"

#include <algorithm>

namespace aegis::pgo {

uint32_t Profiler::register_counter() {
    counters_.push_back(std::make_unique<Counter>());
    return static_cast<uint32_t>(counters_.size() - 1);
}

std::vector<uint8_t> Profiler::serialize() const {
    // Format: <uint32 num_counters> <uint64 hits> <uint32 sample_count>
    //                                 <uint32 stability>
    //                                 <uint32 age> <uint32 decay>
    //                                 <uint32 variance> <uint32 deopt_corr>
    // per counter.
    std::vector<uint8_t> out;
    auto put_u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back((v >> (i * 8)) & 0xff);
    };
    auto put_u64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) out.push_back((v >> (i * 8)) & 0xff);
    };
    put_u32(static_cast<uint32_t>(counters_.size()));
    for (const auto& c : counters_) {
        put_u64(c->hits.load(std::memory_order_relaxed));
        put_u32(c->confidence.sample_count);
        put_u32(c->confidence.stability);
        put_u32(c->confidence.age);
        put_u32(c->confidence.decay);
        put_u32(c->confidence.variance);
        put_u32(c->confidence.deopt_correlation);
    }
    return out;
}

void Profiler::deserialize(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) return;
    auto get_u32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(bytes[off]) |
               (static_cast<uint32_t>(bytes[off+1]) << 8) |
               (static_cast<uint32_t>(bytes[off+2]) << 16) |
               (static_cast<uint32_t>(bytes[off+3]) << 24);
    };
    auto get_u64 = [&](size_t off) -> uint64_t {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<uint64_t>(bytes[off + i]) << (i * 8);
        return v;
    };
    uint32_t n = get_u32(0);
    counters_.clear();
    counters_.reserve(n);
    size_t off = 4;
    for (uint32_t i = 0; i < n; ++i) {
        auto c = std::make_unique<Counter>();
        if (off + 32 > bytes.size()) break;
        c->hits.store(get_u64(off), std::memory_order_relaxed);
        c->confidence.sample_count = get_u32(off + 8);
        c->confidence.stability = get_u32(off + 12);
        c->confidence.age = get_u32(off + 16);
        c->confidence.decay = get_u32(off + 20);
        c->confidence.variance = get_u32(off + 24);
        c->confidence.deopt_correlation = get_u32(off + 28);
        counters_.push_back(std::move(c));
        off += 32;
    }
}

} // namespace aegis::pgo
