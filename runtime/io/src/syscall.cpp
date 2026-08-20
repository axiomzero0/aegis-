// runtime/io/src/syscall.cpp — Real syscall wrappers.
//
// Direct Linux syscall implementations for write(2), read(2), open(2),
// close(2), etc. Used by the higher-level file.cpp and net.cpp
// wrappers. Falls back to libc on non-Linux platforms.
#include "aegis/runtime/io/io.hpp"

#if defined(__linux__)
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <fcntl.h>
#elif defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_RDWR   _O_RDWR
#define O_CREAT  _O_CREAT
#define O_TRUNC  _O_TRUNC
#endif

namespace aegis::runtime::io {

void write_stdout(const void* data, std::size_t len) {
#if defined(_WIN32)
    ::_write(1, data, static_cast<unsigned int>(len));
#else
    ::write(1, data, len);
#endif
}
void write_stderr(const void* data, std::size_t len) {
#if defined(_WIN32)
    ::_write(2, data, static_cast<unsigned int>(len));
#else
    ::write(2, data, len);
#endif
}
int read_stdin(void* dst, std::size_t max) {
#if defined(_WIN32)
    return static_cast<int>(::_read(0, dst, static_cast<unsigned int>(max)));
#else
    ssize_t n = ::read(0, dst, max);
    return static_cast<int>(n);
#endif
}

int open_file(const char* path, int flags) {
#if defined(_WIN32)
    return ::_open(path, flags | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return ::open(path, flags, 0644);
#endif
}

void close_file(int fd) {
#if defined(_WIN32)
    ::_close(fd);
#else
    ::close(fd);
#endif
}

std::size_t read_file(int fd, void* dst, std::size_t max) {
#if defined(_WIN32)
    return static_cast<std::size_t>(::_read(fd, dst, static_cast<unsigned int>(max)));
#else
    ssize_t n = ::read(fd, dst, max);
    return n < 0 ? 0 : static_cast<std::size_t>(n);
#endif
}

std::size_t write_file(int fd, const void* src, std::size_t len) {
#if defined(_WIN32)
    return static_cast<std::size_t>(::_write(fd, src, static_cast<unsigned int>(len)));
#else
    ssize_t n = ::write(fd, src, len);
    return n < 0 ? 0 : static_cast<std::size_t>(n);
#endif
}

} // namespace aegis::runtime::io
