// ============================================================
// aegis/passes/PassConstants.hpp — Named thresholds + budgets for passes.
// ============================================================
// Law: Rule 61 (No Hard-Coded Constants in Optimization Logic) +
//      Rule D.1 (No Magic Numbers or Unnamed Constants).
//
// Every threshold, budget, and limit used by any optimization pass
// MUST live here as a named, documented `constexpr`. This file is the
// single source of truth — passes reference these names, never the
// raw integer.
//
// Naming convention:
//   k<Domain><Thing>  e.g. kGvnReserveFactor, kLicmMaxIterations.
//
// All values are documented with:
//   - Why this specific value.
//   - When it was last validated against benchmarks.
//   - Whether it's a PGO-tunable (and the default).
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace aegis::passes::constants {

// ---- Hash-Consing / GVN ----

/// GVN reserve factor: when initializing the GVN hash-cons table, we
/// reserve `g.size() * kGvnReserveFactor` slots to keep load factor
/// under 7/8 (the SwissTable sweet spot). Validated 2026-08-21 against
/// the sccp/gvn/edce golden tests; not PGO-tunable.
constexpr uint32_t kGvnReserveFactor{2};

/// Maximum number of nodes that GVN will process before declaring
/// the graph "too large" and skipping the pass. Prevents pathological
/// compile times on >10M-node graphs (Rule B.6 budget).
constexpr uint32_t kGvnMaxNodeCount{10'000'000};

// ---- PassManager fixpoint ----

/// Default fixpoint budget per pass (Rule B.5). If a pass doesn't
/// converge to idempotency within this many iterations, the
/// PassManager emits a warning and stops.
constexpr uint32_t kFixpointBudgetPerPass{5};

/// Default soft cap on a single pass's wall-clock runtime in millis.
/// A pass that exceeds this may yield (insert a safe-point) but is
/// not required to. Tuned to keep total compile time reasonable.
constexpr uint32_t kDefaultMaxRuntimeMs{100};

/// Mask for the low 31 bits of an immediate value. Used by the
/// instruction selector to encode immediate values into VRegId-sized
/// slots (uint32_t). The full value is recovered by sign-extension at
/// emit time.
constexpr uint64_t kImmediateMaskLow31Bits{0x7FFFFFFFULL};

// ---- SCCP ----

/// Maximum number of SCCP worklist iterations before bailing. SCCP is
/// guaranteed to converge in O(n * diameter) but real-world graphs
/// converge much faster. This bound prevents pathological cases.
constexpr uint32_t kSccpMaxIterations{1000};

// ---- Escape Analysis ----

/// Maximum BFS depth for the escape walk. Deep walks indicate either
/// a bug (cycle) or a graph that should be chunked first.
constexpr uint32_t kEscapeMaxBfsDepth{10'000};

// ---- Bounds Check Elimination ----

/// Maximum literal value of an array index that BCE will consider as
/// "obviously safe" (0 <= idx < this). Indices beyond this require
/// the full length proof; we don't trust "small idx" as a heuristic.
constexpr int64_t kBceMaxLiteralIndex{1LL << 30};

// ---- Linear Scan Register Allocation ----

/// Maximum number of physical registers the allocator supports. The
/// `bool used[256]` array is sized to this value; if a target has more
/// than this many PRegs (it shouldn't), the allocator will fail loudly.
constexpr uint32_t kLinearScanMaxPRegs{256};

/// Default spill cost weight. Used when the cost model doesn't have
/// profile data (Rule 64 — empirically tuned to favor keeping loop
/// induction variables in registers).
constexpr uint32_t kLinearScanDefaultSpillCost{1};

// ---- Strength Reduction ----

/// Maximum power-of-two multiplier we'll rewrite to a shift. Larger
/// multipliers are left as Mul because the shift would be cheaper but
/// the multiplication is already a single instruction on modern x86.
constexpr uint64_t kStrengthReductionMaxPow2{1ULL << 32};

// ---- LICM ----

/// Maximum number of nodes in a loop body that LICM will scan. Larger
/// loops skip the pass (Rule B.6 budget).
constexpr uint32_t kLicmMaxLoopBodySize{100'000};

// ---- DSE (Dead Store Elimination) ----

/// Maximum effect-chain length DSE will walk before declaring the
/// function too large and skipping.
constexpr uint32_t kDseMaxEffectChainLength{100'000};

// ---- TCO (Tail Call Optimization) ----

/// Maximum number of arguments a tail call may have for us to tag it
/// as a jump. Above this the call/ret overhead is dwarfed by argument
/// setup.
constexpr uint32_t kTcoMaxTailCallArgs{16};

// ---- SCEV (Scalar Evolution) ----

/// Maximum trip count we'll record. Used to prevent integer overflow
/// in the trip_count subtraction (n - start).
constexpr int64_t kScevMaxTripCount{1LL << 40};

// ---- Verifier (Rule 42) ----

/// Maximum number of inputs a single node may have. Sanity bound to
/// catch IR corruption.
constexpr uint32_t kVerifierMaxNodeInputs{65'536};

} // namespace aegis::passes::constants
