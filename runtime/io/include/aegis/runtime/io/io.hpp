// aegis/runtime/io/io.hpp — Low-level I/O primitives (all Crowded).
#pragma once
#include <cstddef>
#include <cstdint>
namespace aegis::runtime::io {

void write_stdout(const void* data, std::size_t len);
void write_stderr(const void* data, std::size_t len);
int  read_stdin(void* dst, std::size_t max);

int  open_file(const char* path, int flags);
void close_file(int fd);
std::size_t read_file(int fd, void* dst, std::size_t max);
std::size_t write_file(int fd, const void* src, std::size_t len);

} // namespace aegis::runtime::io
