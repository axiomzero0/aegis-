// runtime/io/src/net.cpp — Network I/O (TCP/UDP sockets).
//
// POSIX socket wrappers. On Linux/macOS these are real syscalls. On
// Windows we'd use WinSock2 (not implemented for the prototype).
#include "aegis/runtime/io/net.hpp"

#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace aegis::runtime::io {

int socket_create(int domain, int type, int protocol) {
#if defined(__linux__) || defined(__APPLE__)
    return ::socket(domain, type, protocol);
#else
    return -1; // WinSock not supported in prototype
#endif
}

int socket_bind(int fd, uint16_t port) {
#if defined(__linux__) || defined(__APPLE__)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    return ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#else
    (void)fd; (void)port;
    return -1;
#endif
}

int socket_listen(int fd, int backlog) {
#if defined(__linux__) || defined(__APPLE__)
    return ::listen(fd, backlog);
#else
    (void)fd; (void)backlog;
    return -1;
#endif
}

int socket_accept(int fd) {
#if defined(__linux__) || defined(__APPLE__)
    return ::accept(fd, nullptr, nullptr);
#else
    (void)fd;
    return -1;
#endif
}

int socket_connect(int fd, const char* addr, uint16_t port) {
#if defined(__linux__) || defined(__APPLE__)
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (::inet_pton(AF_INET, addr, &sa.sin_addr) <= 0) {
        return -1;
    }
    return ::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
#else
    (void)fd; (void)addr; (void)port;
    return -1;
#endif
}

void socket_close(int fd) {
#if defined(__linux__) || defined(__APPLE__)
    ::close(fd);
#else
    (void)fd;
#endif
}

} // namespace aegis::runtime::io
