// aegis/runtime/io/file.hpp — File handle wrapper.
#pragma once
#include <cstddef>
#include <cstdint>
#include "aegis/runtime/io/io.hpp"
namespace aegis::runtime::io {

class File {
public:
    File() = default;
    explicit File(int fd) : fd_(fd) {}
    ~File() { if (fd_ >= 0) close_file(fd_); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    File& operator=(File&& o) noexcept { if (fd_ >= 0) close_file(fd_); fd_ = o.fd_; o.fd_ = -1; return *this; }
    std::size_t read(void* dst, std::size_t max) { return read_file(fd_, dst, max); }
    std::size_t write(const void* src, std::size_t len) { return write_file(fd_, src, len); }
    int fd() const noexcept { return fd_; }
private:
    int fd_{-1};
};

} // namespace aegis::runtime::io
