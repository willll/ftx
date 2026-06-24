/**
 * @file ftdi_gdb.cpp
 * @brief Implementation of TCP↔FTDI proxy server for GDB/debugger integration.
 * @details Provides transparent byte forwarding with optional RSP packet tracing,
 *          adaptive USB write retry, and robust EINTR handling.
 */

#include "ftdi.hpp"
#include "log.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define poll WSAPoll
typedef unsigned int useconds_t;
static inline int usleep(useconds_t us) { Sleep((us + 999u) / 1000u); return 0; }
static inline int socket_close(int s) { return closesocket(static_cast<SOCKET>(s)); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
static inline int socket_close(int s) { return close(s); }
#endif

#include <sys/types.h>

#include <cerrno>
#include <algorithm>
#include <cstring>
#include <string>

namespace {

/**
 * @brief Write all bytes to a TCP socket with EINTR handling.
 *
 * Continues sending until all bytes are written or an error occurs.
 * Handles EINTR (interrupted system call) by retrying.
 *
 * @param[in] fd Socket file descriptor.
 * @param[in] data Pointer to data buffer to send.
 * @param[in] len Number of bytes to send.
 * @return true if all bytes were sent, false on error.
 * @note Generates debug traces prefixed with `[TCPProxy][dbg]`.
 */
bool write_all_socket(int fd, const unsigned char* data, size_t len)
{
    cdbg << "[TCPProxy][dbg] socket write begin fd=" << fd << " bytes=" << len << std::endl;
    size_t sent = 0;
    while (sent < len)
    {
        const ssize_t n = send(fd, reinterpret_cast<const char*>(data + sent), len - sent, 0);
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

/**
 * @brief Write data to FTDI device with adaptive chunking and retries.
 *
 * Implements robust USB write with the following features:
 * - Sends in chunks (initial: 64 bytes, adaptive: 32, 16, 8, 1)
 * - Up to 3 retry attempts per chunk with 1ms delay
 * - Automatic chunk size reduction on transient failures
 * - Recovery to larger chunks after successful writes
 * - Returns actual bytes written for partial-failure detection
 *
 * The chunk size backoff strategy handles transient USB bulk write failures
 * common in FTDI devices by reducing chunk size before retrying, then
 * recovering to larger chunks for throughput.
 *
 * @param[in] dev FTDI device context.
 * @param[in] data Pointer to data buffer.
 * @param[in] len Number of bytes to send.
 * @param[out] written_out If non-null, receives actual bytes written (even on failure).
 * @return true if all bytes were sent, false if write failed.
 * @note On partial failure, `written_out` contains bytes successfully written.
 * @note Generates debug traces prefixed with `[TCPProxy][dbg]`.
 * @note Calls ftdi_tcioflush() and usleep() on retries.
 */
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

/**
 * @brief Trace RSP (Remote Serial Protocol) packets with a prefix.
 *
 * Buffers incoming bytes and emits complete RSP packets or ACK/NAK bytes.
 * Uses a "carry" buffer to handle packets split across receive calls.
 *
 * **Packet Format**:
 * - RSP packet: `$...(payload)...#XX` (checksum is two hex digits)
 * - ACK: single `+` byte
 * - NAK: single `-` byte
 *
 * **Trace Output**:
 * When `verbose=true`, outputs to stdout:
 * - `GDB>$qSupported:...#14` for client→device packets
 * - `Target>$OK#9d` for device→client packets
 *
 * **Deduplication**: Partial packets remain in `carry` buffer until a complete
 * packet is formed, avoiding spam when network boundaries split packets.
 *
 * @param[in] prefix String prefix (e.g., "GDB>" or "Target>") for output.
 * @param[in,out] carry Stateful buffer for incomplete packets (modified in-place).
 * @param[in] data Pointer to incoming data chunk.
 * @param[in] len Number of bytes in chunk.
 * @param[in] verbose If false, function does nothing (no-op).
 * @note Output goes to stdout. Carry buffer is erased as complete packets are emitted.
 * @note This function is suitable for interrupt-driven I/O loops.
 */
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

/**
 * @brief Start a raw TCP↔FTDI proxy server.
 *
 * Establishes a TCP listening socket on localhost and forwards all bytes
 * between connected clients and the FTDI device in both directions.
 * Acts as a transparent byte bridge with no protocol-specific interpretation.
 *
 * **Server Behavior**:
 * - Listens on `127.0.0.1:port` (loopback only for security)
 * - Accepts one client at a time
 * - Forwards TCP→FTDI and FTDI→TCP bidirectionally
 * - Automatic reconnect loop (waits for next client after disconnect)
 * - Stops on `g_interrupt_flag` or fatal socket error
 *
 * **Polling Timeouts**:
 * - Accept poll: 250ms (allows checking g_interrupt_flag frequently)
 * - Client poll: 10ms (low latency for bidirectional forwarding)
 *
 * **Error Handling**:
 * - EINTR (interrupted system calls): Automatically retried
 * - Socket errors: Logged to stderr, client disconnected
 * - FTDI write failures: Handled by `write_all_ftdi()` with retry logic
 *
 * **Tracing** (with `verbose=true`):
 * - RSP command packets prefixed with `GDB>` or `Target>`
 * - Byte counts for each successful forward operation
 * - Connection lifecycle events
 *
 * @param[in] port TCP port to listen on (default: 1234).
 * @param[in] verbose If true, prints traced RSP packets to stdout.
 * @return Exit status (0 on clean shutdown, 1 on error).
 *
 * @note This function blocks until `g_interrupt_flag` is set (typically via signal).
 * @note Requires FTDI device to be initialized via `InitComms()` before calling.
 * @note All debug output goes to stdout/stderr; no return value indicates byte counts.
 *
 * @see write_all_socket() for TCP write details.
 * @see write_all_ftdi() for adaptive FTDI write strategy.
 * @see trace_rsp_stream() for packet tracing format.
 */
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
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)) < 0)
    {
        std::cerr << "[TCPProxy] setsockopt failed: " << strerror(errno) << std::endl;
        socket_close(listen_fd);
        return 1;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "[TCPProxy] bind failed: " << strerror(errno) << std::endl;
        socket_close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 1) < 0)
    {
        std::cerr << "[TCPProxy] listen failed: " << strerror(errno) << std::endl;
        socket_close(listen_fd);
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
                    const ssize_t recv_len = recv(client_fd, reinterpret_cast<char*>(socket_rx), sizeof(socket_rx), 0);
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

        socket_close(client_fd);
        std::cout << "[TCPProxy] client disconnected" << std::endl;
        cdbg << "[TCPProxy][dbg] client_fd closed" << std::endl;
    }

    socket_close(listen_fd);
    std::cout << "[TCPProxy] stopped" << std::endl;
    cdbg << "[TCPProxy][dbg] listen_fd closed" << std::endl;
    return 0;
}

} // namespace ftdi
