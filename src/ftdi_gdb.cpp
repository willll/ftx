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
    cdbg << "[TCPProxy][dbg] socket write begin fd=" << fd << " bytes=" << len << std::endl;
    size_t sent = 0;
    while (sent < len)
    {
        const ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                cdbg << "[TCPProxy][dbg] socket write interrupted, retrying" << std::endl;
                continue;
            }
            cdbg << "[TCPProxy][dbg] socket write error errno=" << errno << std::endl;
            return false;
        }
        if (n == 0)
        {
            cdbg << "[TCPProxy][dbg] socket write returned 0 bytes" << std::endl;
            return false;
        }
        sent += static_cast<size_t>(n);
        if (sent < len)
        {
            cdbg << "[TCPProxy][dbg] socket partial write sent=" << sent << "/" << len << std::endl;
        }
    }
    cdbg << "[TCPProxy][dbg] socket write complete bytes=" << sent << std::endl;
    return true;
}

bool write_all_ftdi(struct ftdi_context* dev, const unsigned char* data, size_t len, size_t* written_out)
{
    constexpr size_t kInitialUsbChunkSize = 64;
    constexpr int kMaxWriteRetries = 3;
    constexpr useconds_t kRetryDelayUs = 1000;

    cdbg << "[TCPProxy][dbg] ftdi write begin bytes=" << len << std::endl;
    size_t max_chunk_size = kInitialUsbChunkSize;
    size_t written_total = 0;
    while (written_total < len)
    {
        const size_t chunk = std::min(max_chunk_size, len - written_total);
        cdbg << "[TCPProxy][dbg] ftdi write chunk offset=" << written_total
             << " chunk=" << chunk << std::endl;

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
                cdbg << "[TCPProxy][dbg] ftdi write chunk success n=" << n
                     << " total=" << written_total << "/" << len << std::endl;
                break;
            }

            // Some devices transiently fail bulk writes; purge TX and retry.
            if (n < 0)
            {
                cdbg << "[TCPProxy][dbg] ftdi write failed attempt=" << (attempt + 1)
                     << "/" << kMaxWriteRetries << " err='" << ftdi_get_error_string(dev)
                     << "', flushing and retrying" << std::endl;
                (void)ftdi_tcioflush(dev);
                usleep(kRetryDelayUs);
            }
        }

        if (!wrote_chunk)
        {
            if (max_chunk_size > 1)
            {
                max_chunk_size = std::max<size_t>(1, max_chunk_size / 2);
                cdbg << "[TCPProxy][dbg] reducing write chunk size to "
                     << max_chunk_size << " and retrying" << std::endl;
                continue;
            }

            cdbg << "[TCPProxy][dbg] ftdi write failed after retries at minimum chunk size" << std::endl;
            if (written_out != nullptr)
            {
                *written_out = written_total;
            }
            return false;
        }

        // Recover chunk size after successful writes so throughput remains high.
        if (max_chunk_size < kInitialUsbChunkSize)
        {
            max_chunk_size = std::min(kInitialUsbChunkSize, max_chunk_size * 2);
        }
    }
    cdbg << "[TCPProxy][dbg] ftdi write complete bytes=" << written_total << std::endl;
    if (written_out != nullptr)
    {
        *written_out = written_total;
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
    cdbg << "[TCPProxy][dbg] start port=" << port << " verbose=" << verbose << std::endl;
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
    cdbg << "[TCPProxy][dbg] listen_fd=" << listen_fd << std::endl;

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
                cdbg << "[TCPProxy][dbg] accept poll interrupted" << std::endl;
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
                cdbg << "[TCPProxy][dbg] accept interrupted" << std::endl;
                continue;
            }
            std::cerr << "[TCPProxy] accept failed: " << strerror(errno) << std::endl;
            continue;
        }

        std::cout << "[TCPProxy] client connected" << std::endl;
        cdbg << "[TCPProxy][dbg] client_fd=" << client_fd << std::endl;

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
                    cdbg << "[TCPProxy][dbg] client poll interrupted" << std::endl;
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
                    cdbg << "[TCPProxy][dbg] client poll events revents=0x" << std::hex
                         << client_poll.revents << std::dec << std::endl;
                    client_alive = false;
                }
                else if ((client_poll.revents & POLLIN) != 0)
                {
                    const ssize_t recv_len = recv(client_fd, socket_rx, sizeof(socket_rx), 0);
                    if (recv_len <= 0)
                    {
                        cdbg << "[TCPProxy][dbg] client recv returned " << recv_len << std::endl;
                        client_alive = false;
                    }
                    else
                    {
                        cdbg << "[TCPProxy][dbg] recv from client bytes=" << recv_len << std::endl;
                        trace_rsp_stream("GDB>", gdb_trace_buffer, socket_rx, static_cast<size_t>(recv_len), verbose);
                        size_t bytes_forwarded = 0;
                        if (!write_all_ftdi(&g_Device, socket_rx, static_cast<size_t>(recv_len), &bytes_forwarded))
                        {
                            std::cerr << "[TCPProxy] FTDI write failed after forwarding "
                                      << bytes_forwarded << "/" << recv_len
                                      << " bytes: " << ftdi_get_error_string(&g_Device) << std::endl;
                            client_alive = false;
                        }
                        else
                        {
                            cdbg << "[TCPProxy][dbg] forwarded client->ftdi bytes="
                                 << bytes_forwarded << "/" << recv_len << std::endl;
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
                cdbg << "[TCPProxy][dbg] recv from ftdi bytes=" << ftdi_len << std::endl;
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
        cdbg << "[TCPProxy][dbg] client_fd closed" << std::endl;
    }

    close(listen_fd);
    std::cout << "[TCPProxy] stopped" << std::endl;
    cdbg << "[TCPProxy][dbg] listen_fd closed" << std::endl;
    return 0;
}

} // namespace ftdi
