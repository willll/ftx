#pragma once

#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace platform {

using socket_handle = SOCKET;
constexpr socket_handle invalid_socket = INVALID_SOCKET;

inline int socket_write(socket_handle fd, const void* data, std::size_t size)
{
    return send(fd, static_cast<const char*>(data), static_cast<int>(size), 0);
}

inline int socket_read(socket_handle fd, void* data, std::size_t size)
{
    return recv(fd, static_cast<char*>(data), static_cast<int>(size), 0);
}

inline int close_socket(socket_handle fd)
{
    return closesocket(fd);
}

inline int set_socket_reuse_address(socket_handle fd, int value)
{
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value));
}

inline int wait_for_socket_readable(socket_handle fd, int timeout_ms)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return select(0, &readfds, nullptr, nullptr, &timeout);
}

inline std::string last_socket_error_message()
{
    const int error_code = WSAGetLastError();
    char* raw_message = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageA(
        flags,
        nullptr,
        static_cast<DWORD>(error_code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&raw_message),
        0,
        nullptr);

    std::string message = (length > 0 && raw_message != nullptr) ? std::string(raw_message, length) : "Unknown Winsock error";
    if (raw_message != nullptr)
    {
        LocalFree(raw_message);
    }

    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
    {
        message.pop_back();
    }

    return message;
}

} // namespace platform

#else

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace platform {

using socket_handle = int;
constexpr socket_handle invalid_socket = -1;

inline int socket_write(socket_handle fd, const void* data, std::size_t size)
{
    return static_cast<int>(write(fd, data, size));
}

inline int socket_read(socket_handle fd, void* data, std::size_t size)
{
    return static_cast<int>(read(fd, data, size));
}

inline int close_socket(socket_handle fd)
{
    return close(fd);
}

inline int set_socket_reuse_address(socket_handle fd, int value)
{
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
}

inline int wait_for_socket_readable(socket_handle fd, int timeout_ms)
{
    pollfd pfd{fd, POLLIN, 0};
    return poll(&pfd, 1, timeout_ms);
}

inline std::string last_socket_error_message()
{
    return std::strerror(errno);
}

} // namespace platform

#endif
