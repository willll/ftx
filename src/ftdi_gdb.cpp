#include "ftdi.hpp"
#include "log.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <cstring>
#include <string>

namespace {

bool write_all_socket(int fd, const unsigned char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        const ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (n == 0)
        {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool write_all_ftdi(struct ftdi_context* dev, const unsigned char* data, size_t len)
{
    constexpr size_t kUsbChunkSize = 64;
    constexpr int kMaxWriteRetries = 3;

    size_t written_total = 0;
    while (written_total < len)
    {
        const size_t chunk = std::min(kUsbChunkSize, len - written_total);

        bool wrote_chunk = false;
        for (int attempt = 0; attempt < kMaxWriteRetries; ++attempt)
        {
            const int n = ftdi_write_data(dev,
                                          const_cast<unsigned char*>(data + written_total),
                                          static_cast<int>(chunk));
            if (n > 0)
            {
                written_total += static_cast<size_t>(n);
                wrote_chunk = true;
                break;
            }

            // Some devices transiently fail bulk writes; purge TX and retry.
            if (n < 0)
            {
                (void)ftdi_tcioflush(dev);
            }
        }

        if (!wrote_chunk)
        {
            return false;
        }
    }
    return true;
}

void trace_rsp_stream(const char* prefix, std::string& carry, const unsigned char* data, size_t len, bool verbose)
{
    if (!verbose)
    {
        return;
    }

    carry.append(reinterpret_cast<const char*>(data), len);

    size_t pos = 0;
    while (pos < carry.size())
    {
        if (carry[pos] == '$')
        {
            const size_t hash = carry.find('#', pos + 1);
            if (hash == std::string::npos)
            {
                break;
            }
            if (hash + 2 >= carry.size())
            {
                break;
            }

            std::cout << prefix << carry.substr(pos, hash - pos + 3) << std::endl;
            pos = hash + 3;
            continue;
        }

        if (carry[pos] == '+' || carry[pos] == '-')
        {
            std::cout << prefix << carry[pos] << std::endl;
            ++pos;
            continue;
        }

        ++pos;
    }

    if (pos > 0)
    {
        carry.erase(0, pos);
    }
}

} // namespace

namespace ftdi {

int DoTcpProxy(uint16_t port, bool verbose)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        std::cerr << "[TCPProxy] socket creation failed: " << strerror(errno) << std::endl;
        return 1;
    }

    const int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "[TCPProxy] setsockopt failed: " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "[TCPProxy] bind failed: " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 1) < 0)
    {
        std::cerr << "[TCPProxy] listen failed: " << strerror(errno) << std::endl;
        close(listen_fd);
        return 1;
    }

    std::cout << "[TCPProxy] listening on port " << port << std::endl;

    unsigned char socket_rx[2048];
    unsigned char ftdi_rx[2048];
    std::string gdb_trace_buffer;
    std::string target_trace_buffer;

    while (!g_interrupt_flag)
    {
        struct pollfd accept_poll;
        accept_poll.fd = listen_fd;
        accept_poll.events = POLLIN;
        accept_poll.revents = 0;

        const int accept_status = poll(&accept_poll, 1, 250);
        if (accept_status < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::cerr << "[TCPProxy] accept poll failed: " << strerror(errno) << std::endl;
            break;
        }
        if (accept_status == 0)
        {
            continue;
        }

        int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::cerr << "[TCPProxy] accept failed: " << strerror(errno) << std::endl;
            continue;
        }

        std::cout << "[TCPProxy] client connected" << std::endl;

        bool client_alive = true;
        while (client_alive && !g_interrupt_flag)
        {
            struct pollfd client_poll;
            client_poll.fd = client_fd;
            client_poll.events = POLLIN;
            client_poll.revents = 0;

            const int poll_status = poll(&client_poll, 1, 10);
            if (poll_status < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                std::cerr << "[TCPProxy] client poll failed: " << strerror(errno) << std::endl;
                client_alive = false;
                break;
            }

            if (poll_status > 0)
            {
                if ((client_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                {
                    client_alive = false;
                }
                else if ((client_poll.revents & POLLIN) != 0)
                {
                    const ssize_t recv_len = recv(client_fd, socket_rx, sizeof(socket_rx), 0);
                    if (recv_len <= 0)
                    {
                        client_alive = false;
                    }
                    else
                    {
                        trace_rsp_stream("GDB>", gdb_trace_buffer, socket_rx, static_cast<size_t>(recv_len), verbose);
                        if (!write_all_ftdi(&g_Device, socket_rx, static_cast<size_t>(recv_len)))
                        {
                            std::cerr << "[TCPProxy] FTDI write failed: " << ftdi_get_error_string(&g_Device) << std::endl;
                            client_alive = false;
                        }
                    }
                }
            }

            const int ftdi_len = ftdi_read_data(&g_Device, ftdi_rx, sizeof(ftdi_rx));
            if (ftdi_len < 0)
            {
                std::cerr << "[TCPProxy] FTDI read failed: " << ftdi_get_error_string(&g_Device) << std::endl;
                client_alive = false;
                break;
            }

            if (ftdi_len > 0)
            {
                trace_rsp_stream("Target>", target_trace_buffer, ftdi_rx, static_cast<size_t>(ftdi_len), verbose);
                if (!write_all_socket(client_fd, ftdi_rx, static_cast<size_t>(ftdi_len)))
                {
                    std::cerr << "[TCPProxy] socket write failed: " << strerror(errno) << std::endl;
                    client_alive = false;
                }
            }
        }

        close(client_fd);
        std::cout << "[TCPProxy] client disconnected" << std::endl;
    }

    close(listen_fd);
    std::cout << "[TCPProxy] stopped" << std::endl;
    return 0;
}

} // namespace ftdi
