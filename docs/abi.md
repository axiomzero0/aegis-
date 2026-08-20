# Aegis ABI

**Status:** Draft
**Owner:** Aegis Backend Dev Team
**Last Updated:** 2026-08-21
**Related Sections:** `ir_spec.md`, `laws.md`

---

## 1. Overview

Aegis targets two production ABIs:

- **System V AMD64** (Linux / macOS / FreeBSD / Solaris) — the default.
- **AArch64 AAPCS64** (Linux / macOS / FreeBSD) — the default on ARM64.

A third ABI (Windows x64 / Windows ARM64) is supported via the
`Target::windows_cc()` accessor on the same `Target` interface.

---

## 2. Calling Convention (System V AMD64)

### 2.1 Argument Passing

- Integer/pointer arguments: passed in `rdi, rsi, rdx, rcx, r8, r9`
  (in that order). Overflow goes on the stack (right-to-left).
- Floating-point arguments: passed in `xmm0..xmm7`. Overflow goes on
  the stack.
- Struct arguments ≤ 16 bytes that contain only integers and pointers
  are passed in two GPR slots (e.g. a 16-byte struct goes in `rdi`
  + `rsi`).
- Struct arguments ≤ 16 bytes containing only floats are passed in
  two XMM slots.
- Struct arguments > 16 bytes are passed by pointer (caller allocates
  and passes the pointer).

### 2.2 Return Values

- Integer / pointer return: `rax` (and `rdx` if 16 bytes).
- Floating return: `xmm0`.
- Struct return > 16 bytes: caller passes an implicit pointer in
  `rdi` (i.e. the first argument slot is consumed).

### 2.3 Callee-Saved Registers

- `rbx, rbp, r12, r13, r14, r15` — callee must preserve.
- `rsp` — must be 16-byte aligned at the call site.

### 2.4 Red Zone

- The 128-byte region below `rsp` is reserved and not touched by
  signal handlers. Leaf functions may use it for temporary storage
  without adjusting `rsp`.

### 2.5 Stack Alignment

- `rsp` must be 16-byte aligned at the moment of `call` (which pushes
  the return address, leaving it 8-byte aligned on function entry).

---

## 3. Calling Convention (AAPCS64)

### 3.1 Argument Passing

- Integer/pointer arguments: passed in `x0..x7`.
- Floating-point arguments: passed in `v0..v7` (a.k.a. `d0..d7` /
  `s0..s7`).
- Struct arguments ≤ 16 bytes are passed in register slots depending
  on field layout (integer fields → xN, float fields → vN, mixed →
  split across both register files).
- Struct arguments > 16 bytes are passed by reference.

### 3.2 Return Values

- Integer / pointer return: `x0` (and `x1` if 16 bytes).
- Floating return: `v0`.
- Struct return > 16 bytes: implicit pointer in `x8` (caller-allocated
  space).

### 3.3 Callee-Saved Registers

- `x19..x28` — callee must preserve.
- `v8..v15` (bottom 64 bits) — callee must preserve.

### 3.4 Stack Alignment

- `sp` must be 16-byte aligned at all times. No red zone.

---

## 4. Internal Calling Convention (Compiler-Internal)

When the compiler knows the callee (e.g. after inlining is refused but
direct call is permitted), it may use a custom internal convention:

- All arguments passed in registers regardless of order (no stack
  spill for the first 16 arguments).
- No callee-saved registers (the caller spills everything it cares
  about).
- Used for hot-path calls between Aegis-emitted functions only.

This is invisible to FFI (Rule 48 — FFI optimizations must prove ABI
correctness).

---

## 5. Struct Layout

Structs use the **C layout rules** with two Aegis-specific extensions:

- `#[repr(aegis_packed)]` — pads fields to natural alignment but
  reorders for cache locality (PGDLO pass, Rule §II).
- `#[repr(simd)]` — packs the struct into a single SIMD register;
  required alignment is the natural SIMD width (16 for SSE, 32 for AVX,
  64 for AVX-512).

Default rules:

- The first field is at offset 0.
- Each subsequent field is aligned to its natural alignment.
- The struct's overall alignment is the max of its fields' alignments.
- The struct's size is rounded up to its alignment.

---

## 6. Panic and Abort

The runtime exposes a single panic path:

```cpp
namespace aegis::runtime::core {
[[noreturn]] void panic(const char* msg, std::size_t len);
}
```

Panic unwinds via the OS's default unwinder (no `std::exception` —
Rule B.1). On Linux/macOS this is `__cxa_abort` + `SIGABRT`; on Windows
this is `RaiseException(EXCEPTION_NONCONTINUABLE)`.

Aegis source code reaches panic via:

- Explicit `panic!(...)` calls (user-written).
- Failed bounds checks (when BCE cannot prove safety).
- Failed affine-borrow checks (compiler-inserted).
- Failed integer-overflow checks (when `CanOverflow` flag is set on
  a node and the user marked `checked` arithmetic).

---

## 7. FFI

FFI to C is supported via `extern "C"` functions. The compiler
emits calls using the target's default C calling convention.

Rule 48 — "No FFI optimization without ABI proof": FFI optimizations
(inlining, calling-convention customization) must prove:

- Calling-convention correctness (System V / AAPCS64 / MS x64).
- Stack alignment is preserved at the call site.
- Register clobbering is correctly handled (caller-saved registers
  may be clobbered; callee-saved must be preserved).
- Memory ownership transfer is explicit (the Aegis code must either
  give up ownership of any pointer it passes or pass an explicit
  borrow).

---

## 8. Exception Handling Tables

Aegis-generated code does not throw or catch C++ exceptions (Rule B.1).
For interoperability with C++ code that does, the backend emits
`.eh_frame` / `.eh_frame_hdr` tables (System V) or `.pdata` / `.xdata`
(Windows) so that:

- The OS unwinder can find the landing pad for Aegis-emitted code
  (which is always a panic path).
- Stack traces generated by debuggers and profilers (BOLT-style) include
  Aegis-emitted frames.

The backend never emits C++ `try`/`catch` blocks.
