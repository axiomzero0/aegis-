// runtime/core/src/panic.cpp — Real panic handler.
//
// Law (Rule B.1 — NO EXCEPTIONS ON THE HOT PATH):
//   The runtime panic path is the only abort mechanism. It does NOT
//   throw; it writes the message to stderr and calls abort(2). Stack
//   unwinding happens via the OS default unwinder, not via C++ EH.
#include "aegis/runtime/core/panic.hpp"
#include "aegis/runtime/core/intrinsics.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace aegis::runtime::core {

[[noreturn]] void panic(const char* msg, std::size_t len) {
    // Write the panic header.
    static constexpr char kHeader[] = "aegis panic: ";
    ::write(2, kHeader, sizeof(kHeader) - 1);
    ::write(2, msg, len);
    ::write(2, "\n", 1);
    ::abort();
}

[[noreturn]] void panic(const char* message) {
    panic(message, std::strlen(message));
}

void memcpy_bytes(void* dst, const void* src, size_t n) {
    // Use __builtin_memcpy to allow the compiler to vectorize.
    __builtin_memcpy(dst, src, n);
}
void memmove_bytes(void* dst, const void* src, size_t n) {
    __builtin_memmove(dst, src, n);
}
void memset_bytes(void* dst, uint8_t value, size_t n) {
    __builtin_memset(dst, value, n);
}

} // namespace aegis::runtime::core
