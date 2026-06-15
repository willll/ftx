#ifndef GDB_STUB_HPP
#define GDB_STUB_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "platform.hpp"

/**
 * GDB Remote Serial Protocol (RSP) stub.
 * Implements a subset of GDB remote debugging protocol for Saturn cartridge communication.
 * 
 * Protocol overview:
 * - GDB sends packets: $<command>#<checksum>
 * - Checksum is XOR of all command bytes, in hex
 * - Valid responses: '+' (ack), '-' (nak), or $<data>#<checksum>
 * 
 * Supported commands:
 * - g     : Read all registers (returns hex-encoded register state)
 * - m     : Read memory (format: mADDR,LENGTH)
 * - c     : Continue execution
 * - s     : Single step
 * - ?     : Query halt reason
 * - q/Q   : General query/set (supported: qAttached)
 * - vKill : Kill target (exits stub)
 */

namespace gdb_stub {

// RSP packet structure
struct RSPPacket {
    std::string command;  // Raw command string (without $, #, checksum)
    uint8_t checksum;     // Received checksum (for validation)
};

// Parsed memory read request
struct MemoryReadRequest {
    uint64_t address;
    uint32_t length;
};

// Parsed register/memory response for device
struct DeviceRequest {
    enum Type {
        REG_READ,      // Read all registers
        MEM_READ,      // Read memory range
        MEM_WRITE,     // Write memory range
        STEP,          // Single step
        CONTINUE,      // Resume execution
        HALT_REASON,   // Query halt reason
    } type;
    
    uint64_t address;  // For MEM_READ/MEM_WRITE
    uint32_t length;   // For MEM_READ/MEM_WRITE
    std::vector<uint8_t> data;  // For MEM_WRITE
};

// RSP packet encoding/decoding
class RSPCodec {
public:
    /**
     * Calculate XOR checksum for RSP packet content
     */
    static uint8_t calculate_checksum(const std::string& data);
    
    /**
     * Parse incoming RSP packet (expects format: $command#checksum)
     * Returns true if packet is valid, false if malformed
     */
    static bool parse_packet(const std::string& raw, RSPPacket& out_packet);
    
    /**
     * Encode RSP packet for transmission
     */
    static std::string encode_packet(const std::string& data);
    
    /**
     * Encode hex string (e.g., "0x12ab" -> "12ab")
     */
    static std::string to_hex(const std::vector<uint8_t>& data);
    
    /**
     * Decode hex string to bytes
     */
    static bool from_hex(const std::string& hex_str, std::vector<uint8_t>& out);
};

// GDB command parser
class GDBCommandParser {
public:
    /**
     * Parse RSP command into device request
     * Returns true if command is valid
     */
    static bool parse_command(const RSPPacket& packet, DeviceRequest& out_request);
    
    /**
     * Format device response as RSP packet data
     */
    static std::string format_response(const DeviceRequest& request, 
                                       const std::string& device_response);
};

// GDB stub server
class GDBStub {
public:
    /**
     * Create stub listening on port (or Unix socket if port == 0)
     */
    GDBStub(uint16_t port = 1234);
    ~GDBStub();
    
    /**
     * Start listening for GDB connections
     * Blocks until Ctrl+C or fatal error
     */
    int run();
    
private:
    uint16_t port_;
    platform::socket_handle listen_socket_;
    
    /**
     * Handle a single GDB client connection
     */
    void handle_client(platform::socket_handle client_fd);
    
    /**
     * Send RSP packet (with +/- ack handling)
     */
    bool send_packet(platform::socket_handle fd, const std::string& data);
    
    /**
     * Receive RSP packet with timeout
     */
    bool receive_packet(platform::socket_handle fd, RSPPacket& out_packet);
    
    /**
     * Process single command and send response
     */
    bool process_command(platform::socket_handle fd, const RSPPacket& packet);
};

}  // namespace gdb_stub

#endif
