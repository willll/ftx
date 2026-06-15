#include "gdb_stub.hpp"
#include "ftdi.hpp"
#include "log.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif
#include <csignal>
#include <cstring>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace gdb_stub {

// =============================================================================
// RSPCodec Implementation
// =============================================================================

uint8_t RSPCodec::calculate_checksum(const std::string& data) {
    uint8_t checksum = 0;
    for (char c : data) {
        checksum ^= static_cast<uint8_t>(c);
    }
    return checksum;
}

bool RSPCodec::parse_packet(const std::string& raw, RSPPacket& out_packet) {
    // Expected format: $command#checksum
    if (raw.size() < 4 || raw[0] != '$' || raw.find('#') == std::string::npos) {
        CDBG_LOG_ON_CHANGE("gdb_parse_invalid", "RSP: malformed packet: " << raw);
        return false;
    }
    
    size_t hash_pos = raw.find('#');
    out_packet.command = raw.substr(1, hash_pos - 1);
    
    // Parse checksum
    std::string checksum_hex = raw.substr(hash_pos + 1);
    if (checksum_hex.length() != 2) {
        return false;
    }
    
    char* endptr = nullptr;
    out_packet.checksum = static_cast<uint8_t>(strtol(checksum_hex.c_str(), &endptr, 16));
    if (*endptr != '\0') {
        return false;
    }
    
    // Verify checksum
    uint8_t calculated = calculate_checksum(out_packet.command);
    if (calculated != out_packet.checksum) {
        CDBG_LOG_ON_CHANGE("gdb_checksum_fail", 
            "RSP: checksum mismatch (got 0x" << std::hex << (int)out_packet.checksum 
            << ", expected 0x" << (int)calculated << std::dec << ")");
        return false;
    }
    
    return true;
}

std::string RSPCodec::encode_packet(const std::string& data) {
    uint8_t checksum = calculate_checksum(data);
    
    std::ostringstream oss;
    oss << "$" << data << "#" << std::hex << std::setfill('0') << std::setw(2) << (int)checksum;
    return oss.str();
}

std::string RSPCodec::to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (uint8_t byte : data) {
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)byte;
    }
    return oss.str();
}

bool RSPCodec::from_hex(const std::string& hex_str, std::vector<uint8_t>& out) {
    if (hex_str.length() % 2 != 0) {
        return false;
    }
    
    out.clear();
    for (size_t i = 0; i < hex_str.length(); i += 2) {
        char* endptr = nullptr;
        long byte_val = strtol(hex_str.substr(i, 2).c_str(), &endptr, 16);
        if (*endptr != '\0' || byte_val < 0 || byte_val > 255) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(byte_val));
    }
    
    return true;
}

// =============================================================================
// GDBCommandParser Implementation
// =============================================================================

bool GDBCommandParser::parse_command(const RSPPacket& packet, DeviceRequest& out_request) {
    if (packet.command.empty()) {
        return false;
    }
    
    char cmd = packet.command[0];
    std::string args = packet.command.substr(1);
    
    switch (cmd) {
        case 'g':
            // Read all registers
            out_request.type = DeviceRequest::REG_READ;
            return true;
            
        case 'm': {
            // Read memory: mADDR,LENGTH
            out_request.type = DeviceRequest::MEM_READ;
            
            size_t comma_pos = args.find(',');
            if (comma_pos == std::string::npos) {
                return false;
            }
            
            char* endptr = nullptr;
            out_request.address = strtoull(args.substr(0, comma_pos).c_str(), &endptr, 16);
            if (*endptr != '\0') return false;
            
            out_request.length = strtoul(args.substr(comma_pos + 1).c_str(), &endptr, 16);
            if (*endptr != '\0') return false;
            
            return true;
        }
        
        case 'M': {
            // Write memory: MADDR,LENGTH:data
            out_request.type = DeviceRequest::MEM_WRITE;
            
            size_t comma_pos = args.find(',');
            if (comma_pos == std::string::npos) return false;
            
            size_t colon_pos = args.find(':');
            if (colon_pos == std::string::npos) return false;
            
            char* endptr = nullptr;
            out_request.address = strtoull(args.substr(0, comma_pos).c_str(), &endptr, 16);
            if (*endptr != '\0') return false;
            
            out_request.length = strtoul(args.substr(comma_pos + 1, colon_pos - comma_pos - 1).c_str(), &endptr, 16);
            if (*endptr != '\0') return false;
            
            std::string data_hex = args.substr(colon_pos + 1);
            if (!RSPCodec::from_hex(data_hex, out_request.data)) {
                return false;
            }
            
            return true;
        }
        
        case 'c':
            // Continue
            out_request.type = DeviceRequest::CONTINUE;
            return true;
            
        case 's':
            // Single step
            out_request.type = DeviceRequest::STEP;
            return true;
            
        case '?':
            // Query halt reason
            out_request.type = DeviceRequest::HALT_REASON;
            return true;
            
        case 'q':
            // General query
            if (args == "Attached") {
                // qAttached: respond with "1" (attached to existing process)
                out_request.type = DeviceRequest::HALT_REASON;  // Reuse to signal special response
                return true;
            }
            CDBG_LOG_ON_CHANGE("gdb_unknown_query", "RSP: unknown query: q" << args);
            return false;
            
        case 'v':
            // Extended commands
            if (args == "Kill") {
                // vKill: terminate debugging session
                CDBG_LOG_ON_CHANGE("gdb_kill", "RSP: GDB requested kill");
                return true;
            }
            return false;
            
        case 'Q':
            // General set - most are unsupported, just acknowledge
            CDBG_LOG_ON_CHANGE("gdb_query_set", "RSP: query set (unsupported): Q" << args);
            return true;
            
        default:
            CDBG_LOG_ON_CHANGE("gdb_unknown_cmd", "RSP: unknown command: " << cmd << args);
            return false;
    }
}

std::string GDBCommandParser::format_response(const DeviceRequest& request, 
                                             const std::string& device_response) {
    // This is a placeholder - in a real implementation, you'd parse the device response
    // based on the request type and format it appropriately for GDB
    
    // For now, return the raw response as hex-encoded data
    // In practice, you'd decode Saturn debugger output and re-encode for GDB
    return device_response;
}

// =============================================================================
// GDBStub Implementation
// =============================================================================

GDBStub::GDBStub(uint16_t port) 
    : port_(port), listen_socket_(platform::invalid_socket) {
}

GDBStub::~GDBStub() {
    if (listen_socket_ != platform::invalid_socket) {
        platform::close_socket(listen_socket_);
    }
}

bool GDBStub::send_packet(platform::socket_handle fd, const std::string& data) {
    std::string packet = RSPCodec::encode_packet(data);
    
    CDBG_LOG_ON_CHANGE("gdb_send", "RSP <- " << packet);
    
    int written = platform::socket_write(fd, packet.c_str(), packet.length());
    if (written < 0) {
        cdbg << "[GDB] write error: " << platform::last_socket_error_message() << std::endl;
        return false;
    }
    
    // Wait for ACK ('+') or NAK ('-')
    char ack;
    int poll_result = platform::wait_for_socket_readable(fd, 1000);  // 1 second timeout
    
    if (poll_result <= 0) {
        CDBG_LOG_ON_CHANGE("gdb_no_ack", "RSP: no ACK received (timeout)");
        return false;
    }
    
    int read_result = platform::socket_read(fd, &ack, 1);
    if (read_result != 1) {
        return false;
    }
    
    if (ack == '+') {
        return true;
    } else if (ack == '-') {
        CDBG_LOG_ON_CHANGE("gdb_nak", "RSP: received NAK, will retry");
        return false;
    }
    
    // Not an ACK/NAK, might be start of next packet
    return true;
}

bool GDBStub::receive_packet(platform::socket_handle fd, RSPPacket& out_packet) {
    std::string raw_packet;
    char byte;

    // Wait for start of packet ('$')
    while (true) {
        int poll_result = platform::wait_for_socket_readable(fd, 1000);
        if (poll_result <= 0) {
            return false;  // Timeout
        }
        
        int n = platform::socket_read(fd, &byte, 1);
        if (n != 1) {
            return false;
        }
        
        if (byte == '$') {
            break;
        }
        // Discard other characters (ACKs, etc.)
    }
    
    // Read until '#'
    while (true) {
        int n = platform::socket_read(fd, &byte, 1);
        if (n != 1) {
            return false;
        }
        
        if (byte == '#') {
            break;
        }
        
        raw_packet += byte;
    }
    
    // Read checksum (2 hex digits)
    char checksum_buf[3];
    int n = platform::socket_read(fd, checksum_buf, 2);
    if (n != 2) {
        return false;
    }
    checksum_buf[2] = '\0';
    
    // Reconstruct full packet
    std::string full_packet = "$" + raw_packet + "#" + std::string(checksum_buf);
    
    CDBG_LOG_ON_CHANGE("gdb_receive", "RSP -> " << full_packet);
    
    // Parse and validate
    if (!RSPCodec::parse_packet(full_packet, out_packet)) {
        // Send NAK
        const char nak = '-';
        platform::socket_write(fd, &nak, 1);
        return false;
    }
    
    // Send ACK
    const char ack = '+';
    platform::socket_write(fd, &ack, 1);
    return true;
}

bool GDBStub::process_command(platform::socket_handle fd, const RSPPacket& packet) {
    DeviceRequest device_req;
    
    // Parse the command
    if (!GDBCommandParser::parse_command(packet, device_req)) {
        // Send error response
        return send_packet(fd, "E01");
    }
    
    // Special handling for some commands
    if (packet.command == "qAttached") {
        // qAttached: always respond "1" (attached)
        return send_packet(fd, "1");
    }
    
    if (packet.command[0] == 'v' && packet.command.substr(1) == "Kill") {
        // vKill: OK, then close connection
        send_packet(fd, "OK");
        return false;  // Signal to close connection
    }
    
    // For memory/register operations, we need to communicate with the device
    // This is a placeholder that sends a generic response
    
    switch (device_req.type) {
        case DeviceRequest::REG_READ:
            // In a real implementation, read registers from Saturn debugger
            // For now, return empty register state
            CDBG_LOG_ON_CHANGE("gdb_reg_read", "RSP: register read (not implemented)");
            return send_packet(fd, "");
            
        case DeviceRequest::MEM_READ:
            CDBG_LOG_ON_CHANGE("gdb_mem_read", 
                "RSP: memory read addr=0x" << std::hex << device_req.address 
                << " len=0x" << device_req.length << std::dec);
            // In a real implementation, read memory from Saturn
            return send_packet(fd, "");
            
        case DeviceRequest::MEM_WRITE:
            CDBG_LOG_ON_CHANGE("gdb_mem_write", 
                "RSP: memory write addr=0x" << std::hex << device_req.address 
                << " len=0x" << device_req.length << std::dec);
            // In a real implementation, write memory to Saturn
            return send_packet(fd, "OK");
            
        case DeviceRequest::CONTINUE:
            CDBG_LOG_ON_CHANGE("gdb_continue", "RSP: continue execution");
            // Send device command to continue
            return send_packet(fd, "S05");  // Stopped by signal 5 (SIGTRAP)
            
        case DeviceRequest::STEP:
            CDBG_LOG_ON_CHANGE("gdb_step", "RSP: single step");
            // Send device command to step
            return send_packet(fd, "S05");
            
        case DeviceRequest::HALT_REASON:
            CDBG_LOG_ON_CHANGE("gdb_halt_reason", "RSP: query halt reason");
            return send_packet(fd, "S05");  // Stopped by signal 5 (SIGTRAP)
    }
    
    return send_packet(fd, "E02");
}

void GDBStub::handle_client(platform::socket_handle client_fd) {
    cdbg << "[GDB] client connected" << std::endl;
    
    RSPPacket packet;
    while (receive_packet(client_fd, packet)) {
        if (!process_command(client_fd, packet)) {
            break;  // Connection closed
        }
    }
    
    platform::close_socket(client_fd);
    cdbg << "[GDB] client disconnected" << std::endl;
}

int GDBStub::run() {
#ifdef _WIN32
    WSADATA wsa_data{};
    const int wsa_status = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_status != 0) {
        cdbg << "[GDB] WSAStartup failed: " << wsa_status << std::endl;
        return 1;
    }
#endif

    // Create listening socket
    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ == platform::invalid_socket) {
        cdbg << "[GDB] socket creation failed: " << platform::last_socket_error_message() << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    
    // Allow reuse of port
    int reuse = 1;
    if (platform::set_socket_reuse_address(listen_socket_, reuse) < 0) {
        cdbg << "[GDB] setsockopt failed: " << platform::last_socket_error_message() << std::endl;
        platform::close_socket(listen_socket_);
        listen_socket_ = platform::invalid_socket;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    
    // Bind to port
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    
    if (bind(listen_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        cdbg << "[GDB] bind failed: " << platform::last_socket_error_message() << std::endl;
        platform::close_socket(listen_socket_);
        listen_socket_ = platform::invalid_socket;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    
    // Listen
    if (listen(listen_socket_, 1) < 0) {
        cdbg << "[GDB] listen failed: " << platform::last_socket_error_message() << std::endl;
        platform::close_socket(listen_socket_);
        listen_socket_ = platform::invalid_socket;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    
    cdbg << "[GDB] listening on port " << port_ << std::endl;
    
    // Accept connections
    while (!ftdi::g_interrupt_flag) {
        int poll_result = platform::wait_for_socket_readable(listen_socket_, 1000);  // 1 second timeout
        
        if (poll_result <= 0) {
            continue;
        }
        
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        
        platform::socket_handle client_fd = accept(listen_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == platform::invalid_socket) {
            cdbg << "[GDB] accept failed: " << platform::last_socket_error_message() << std::endl;
            continue;
        }
        
        handle_client(client_fd);
    }
    
    platform::close_socket(listen_socket_);
    listen_socket_ = platform::invalid_socket;
    cdbg << "[GDB] stub shutdown" << std::endl;

#ifdef _WIN32
    WSACleanup();
#endif
    
    return 0;
}

}  // namespace gdb_stub
