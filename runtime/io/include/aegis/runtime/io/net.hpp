// aegis/runtime/io/net.hpp — Network I/O (TCP/UDP sockets).
#pragma once
#include <cstdint>
namespace aegis::runtime::io {

int  socket_create(int domain, int type, int protocol);
int  socket_bind(int fd, uint16_t port);
int  socket_listen(int fd, int backlog);
int  socket_accept(int fd);
int  socket_connect(int fd, const char* addr, uint16_t port);
void socket_close(int fd);

} // namespace aegis::runtime::io
