/**
 * @file ftdi_webdav.cpp
 * @brief WebDAV server implementation mapping WebDAV requests to Saturn FAT filesystem.
 */

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "ftdi.hpp"
#include "xfer.hpp"
#include "log.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <thread>
#include <chrono>
#include <cstdint>
#include <limits>

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace {

/**
 * @brief Represents a file or directory entry on the Sega Saturn filesystem.
 */
struct FileEntry {
    bool is_dir = false; ///< True if entry is a directory, false if file
    size_t size = 0; ///< File size in bytes
    std::string mtime = "2026-07-17T09:00:00Z"; ///< Last modified time in ISO 8601 format
    std::string name; ///< Entry name (base name)
};

/**
 * @brief Decode a URL-encoded string.
 * @param src URL-encoded source string.
 * @return URL-decoded string.
 */
std::string url_decode(const std::string& src) {
    std::string ret;
    char ch;
    int ii;
    for (size_t pos = 0; pos < src.length(); ++pos) {
        if (src[pos] == '%') {
            if (pos + 2 < src.length() && sscanf(src.substr(pos + 1, 2).c_str(), "%x", &ii) == 1) {
                ch = static_cast<char>(ii);
                ret += ch;
                pos += 2;
            }
        } else if (src[pos] == '+') {
            ret += ' ';
        } else {
            ret += src[pos];
        }
    }
    return ret;
}

/**
 * @brief Parse a raw directory listing string returned by the Saturn cartridge.
 * @param listing Raw listing payload.
 * @return Vector of FileEntry structures parsed from the listing.
 */
std::vector<FileEntry> parse_directory_listing(const std::string& listing) {
    std::vector<FileEntry> entries;
    std::istringstream iss(listing);
    std::string line;
    while (std::getline(iss, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        line = line.substr(first);

        if (line.size() < 4) continue;

        bool is_dir = false;
        if (line.substr(0, 3) == "[D]") {
            is_dir = true;
        } else if (line.substr(0, 3) == "[F]") {
            is_dir = false;
        } else {
            continue;
        }

        std::istringstream line_ss(line.substr(3));
        size_t size = 0;
        std::string date, time_str, name;
        if (line_ss >> size >> date >> time_str) {
            std::getline(line_ss, name);
            size_t name_start = name.find_first_not_of(" ");
            if (name_start != std::string::npos) {
                name = name.substr(name_start);
            }
            if (!name.empty() && name.back() == '\r') {
                name.pop_back();
            }
            if (name == "." || name == "..") {
                continue;
            }

            FileEntry entry;
            entry.is_dir = is_dir;
            entry.size = size;
            entry.mtime = date + "T" + time_str + ":00Z";
            entry.name = name;
            entries.push_back(entry);
        }
    }
    return entries;
}

/**
 * @brief Format an ISO 8601 date string to the HTTP-date format (RFC 1123).
 * @param iso_date ISO 8601 formatted date (e.g. YYYY-MM-DDTHH:MM:00Z).
 * @return RFC 1123 HTTP-date string.
 */
std::string format_http_date(const std::string& iso_date) {
    if (iso_date.size() < 16) {
        return "Fri, 17 Jul 2026 09:00:00 GMT";
    }
    int year = 2026, month = 7, day = 17, hour = 9, minute = 0;
    sscanf(iso_date.c_str(), "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute);

    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;

    char buf[128];
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    std::time_t t = std::mktime(&tm);
    if (t != -1) {
        std::tm* gmt = std::gmtime(&t);
        if (gmt) {
            strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmt);
            return buf;
        }
    }
    
    sprintf(buf, "Fri, %02d %s %04d %02d:%02d:00 GMT", day, months[month - 1], year, hour, minute);
    return buf;
}

/**
 * @brief Retrieve metadata for a single file or directory on the target cartridge.
 * @param path Filesystem path of the item (updated to matched case on success).
 * @param entry Output reference populated with item details on success.
 * @return True if item is found and metadata is retrieved, false otherwise.
 */
bool get_item_metadata(std::string& path, FileEntry& entry) {
    if (path == "/" || path.empty()) {
        entry.is_dir = true;
        entry.size = 0;
        entry.mtime = "2026-07-17T09:00:00Z";
        entry.name = "";
        return true;
    }

    size_t last_slash = path.find_last_of('/');
    std::string parent = (last_slash == 0) ? "/" : path.substr(0, last_slash);
    std::string name = path.substr(last_slash + 1);

    std::string parent_listing;
    std::string saturn_path = "-l " + parent;
    if (xfer::DoListStr(saturn_path.c_str(), parent_listing) != 1) {
        return false;
    }

    std::vector<FileEntry> entries = parse_directory_listing(parent_listing);
    for (const auto& e : entries) {
        if (boost::iequals(e.name, name)) {
            entry = e;
            path = (parent == "/") ? ("/" + e.name) : (parent + "/" + e.name);
            return true;
        }
    }
    return false;
}

/**
 * @brief Generate a WebDAV multi-status XML response containing properties for a requested path.
 * @param path Requested request URI path.
 * @param target_entry Metadata of the requested item.
 * @param depth Depth header value indicating traversal depth ("0" or "1").
 * @return Conforming XML response string, or an empty string if the directory listing failed.
 */
std::string build_multistatus_xml(const std::string& path, const FileEntry& target_entry, const std::string& depth) {
    using boost::property_tree::ptree;

    ptree pt;
    ptree& multistatus = pt.add("D:multistatus", "");
    multistatus.put("<xmlattr>.xmlns:D", "DAV:");

    auto add_response = [&](const std::string& href_path, const FileEntry& entry) {
        ptree response;
        std::string escaped_href = href_path;
        if (entry.is_dir && !escaped_href.empty() && escaped_href.back() != '/') {
            escaped_href += '/';
        }
        response.put("D:href", escaped_href);

        ptree propstat;
        ptree prop;

        if (entry.is_dir) {
            prop.add("D:resourcetype.D:collection", "");
        } else {
            prop.add("D:resourcetype", "");
            prop.put("D:getcontentlength", entry.size);
            prop.put("D:getcontenttype", "application/octet-stream");
        }
        prop.put("D:displayname", entry.name);
        prop.put("D:getlastmodified", format_http_date(entry.mtime));

        propstat.add_child("D:prop", prop);
        propstat.put("D:status", "HTTP/1.1 200 OK");

        response.add_child("D:propstat", propstat);
        multistatus.add_child("D:response", response);
    };

    add_response(path, target_entry);

    if (depth == "1" && target_entry.is_dir) {
        std::string listing;
        std::string saturn_path = "-l " + path;
        if (xfer::DoListStr(saturn_path.c_str(), listing) != 1) {
            return std::string(); // an empty or partial folder would look real to the client
        }
        std::vector<FileEntry> sub_entries = parse_directory_listing(listing);
        for (const auto& e : sub_entries) {
            std::string sub_path = (path == "/") ? ("/" + e.name) : (path + "/" + e.name);
            add_response(sub_path, e);
        }
    }

    std::ostringstream oss;
    boost::property_tree::xml_parser::xml_writer_settings<std::string> settings(' ', 2);
    boost::property_tree::xml_parser::write_xml(oss, pt, settings);
    return oss.str();
}

/**
 * @brief Checks if a path conforms to strict 8.3 FAT filename limits.
 * @param path_str The path to check.
 * @return True if conforming, false if any component violates 8.3 limits.
 */
bool is_valid_83_path(const std::string& path_str) {
    if (path_str.empty() || path_str[0] != '/') {
        return false; // not an SD FAT path (e.g. a raw "sdraw:" range)
    }
    size_t start = 0;
    while (start < path_str.size()) {
        size_t end = path_str.find('/', start);
        if (end == std::string::npos) {
            end = path_str.size();
        }
        std::string component = path_str.substr(start, end - start);
        if (!component.empty()) {
            size_t dot = component.rfind('.');
            std::string name = (dot == std::string::npos) ? component : component.substr(0, dot);
            std::string ext = (dot == std::string::npos) ? "" : component.substr(dot + 1);
            size_t dot_count = std::count(component.begin(), component.end(), '.');
            if (name.length() > 8 || ext.length() > 3 || dot_count > 1) {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

/// Path part of a request target or Destination header: no scheme/host, query or trailing slash.
std::string normalize_path(const std::string& target) {
    std::string path = target;
    size_t proto_pos = path.find("://");
    if (proto_pos != std::string::npos) {
        size_t path_pos = path.find('/', proto_pos + 3);
        path = (path_pos != std::string::npos) ? path.substr(path_pos) : "/";
    }
    path = url_decode(path);
    size_t q = path.find('?');
    if (q != std::string::npos) {
        path = path.substr(0, q);
    }
    if (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

// Request bodies are streamed into kUploadFile and file downloads streamed back out of
// kDownloadFile, so a transfer never has to fit in memory.
using Request = http::request<http::file_body>;
using Response = http::response<http::string_body>;
const char* const kUploadFile = "ftx_webdav_temp.bin";
const char* const kDownloadFile = "ftx_webdav_get_temp.bin";

Response make_response(const Request& req, http::status status, std::string body = {}) {
    Response res{status, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.body() = std::move(body);
    return res;
}

/// Delete a file, or an (empty) directory if it is one. Returns 1 on success.
int remove_item(const std::string& path) {
    return (xfer::DoRemove(path.c_str()) == 1) ? 1 : xfer::DoRmdir(path.c_str());
}

Response move_or_copy(const Request& req, const std::string& method, std::string path) {
    std::string dest_header{req["Destination"]};
    if (dest_header.empty()) {
        return make_response(req, http::status::bad_request);
    }
    std::string dest_path = normalize_path(dest_header);
    if (!is_valid_83_path(dest_path)) {
        std::cerr << "[WebDAV] Rejected " << method << ": Destination path component violates strict 8.3 filename conventions: " << dest_path << std::endl;
        return make_response(req, http::status::bad_request,
            "Error: Destination path violates strict 8.3 filename conventions (max 8 characters for name, 3 characters for extension).");
    }

    FileEntry source_entry;
    if (!get_item_metadata(path, source_entry)) {
        return make_response(req, http::status::not_found);
    }
    FileEntry dest_entry;
    const std::string requested_dest = dest_path;
    const bool dest_exists = get_item_metadata(dest_path, dest_entry);
    // Lookups ignore case, so "a.sav" -> "A.SAV" finds the source itself.
    const bool same_item = dest_exists && dest_path == path;
    if (same_item && (method == "COPY" || requested_dest == path)) {
        return make_response(req, http::status::forbidden);
    }
    if (dest_exists && !same_item && std::string(req["Overwrite"]) == "F") {
        return make_response(req, http::status::precondition_failed);
    }

    int status = 0;
    if (method == "MOVE") {
        if (dest_exists && !same_item) {
            remove_item(dest_path); // f_rename fails if the destination exists
        }
        status = xfer::DoRename(path.c_str(), requested_dest.c_str());
    } else {
        const char* temp_filename = "ftx_webdav_copy_temp.bin";
        status = xfer::DoSdDownload(path.c_str(), temp_filename);
        if (status == 1) {
            // Only touch the destination once the source is safely on the host.
            if (dest_exists) {
                remove_item(dest_path);
            }
            status = xfer::DoSdUpload(temp_filename, requested_dest.c_str());
        }
        std::remove(temp_filename);
    }
    if (status != 1) {
        return make_response(req, http::status::internal_server_error);
    }
    return make_response(req, dest_exists ? http::status::no_content : http::status::created);
}

/// Map one WebDAV request onto cartridge file commands.
/// @param send_file Set to a file whose contents must be sent as the response body.
Response handle_request(Request& req, std::string& send_file) {
    const std::string method{req.method_string()};
    std::string path = normalize_path(std::string(req.target()));
    std::cout << "[WebDAV] Request: " << method << " " << path << std::endl;

    // Only SD FAT paths: the cartridge treats e.g. "sdraw:0:64" as raw sectors to erase or overwrite.
    if (path.empty() || path[0] != '/') {
        return make_response(req, http::status::bad_request);
    }

    if (method == "OPTIONS") {
        Response res = make_response(req, http::status::ok);
        res.set(http::field::allow, "OPTIONS, GET, PROPFIND, PUT, DELETE, MKCOL, MOVE, COPY");
        res.set("DAV", "1");
        return res;
    }
    if (method == "PROPFIND") {
        std::string depth{req["Depth"]};
        if (depth.empty()) {
            depth = "1";
        }
        FileEntry target_entry;
        if (!get_item_metadata(path, target_entry)) {
            return make_response(req, http::status::not_found);
        }
        std::string xml = build_multistatus_xml(path, target_entry, depth);
        if (xml.empty()) {
            return make_response(req, http::status::internal_server_error);
        }
        Response res = make_response(req, http::status::multi_status, std::move(xml));
        res.set(http::field::content_type, "application/xml; charset=utf-8");
        return res;
    }
    if (method == "PUT") {
        if (!is_valid_83_path(path)) {
            std::cerr << "[WebDAV] Rejected PUT: Path component violates strict 8.3 filename conventions: " << path << std::endl;
            return make_response(req, http::status::bad_request,
                "Error: Path violates strict 8.3 filename conventions (max 8 characters for name, 3 characters for extension).");
        }
        req.body().close(); // flush the streamed body before uploading it
        int status = xfer::DoSdUpload(kUploadFile, path.c_str());
        return make_response(req, status == 1 ? http::status::created : http::status::internal_server_error);
    }
    if (method == "DELETE") {
        return make_response(req, remove_item(path) == 1 ? http::status::no_content : http::status::internal_server_error);
    }
    if (method == "MKCOL") {
        if (!is_valid_83_path(path)) {
            std::cerr << "[WebDAV] Rejected MKCOL: Path component violates strict 8.3 filename conventions: " << path << std::endl;
            return make_response(req, http::status::bad_request,
                "Error: Directory name violates strict 8.3 conventions (max 8 characters).");
        }
        return make_response(req, xfer::DoMkdir(path.c_str()) == 1 ? http::status::created : http::status::internal_server_error);
    }
    if (method == "GET") {
        FileEntry entry;
        if (!get_item_metadata(path, entry) || entry.is_dir) {
            return make_response(req, http::status::not_found);
        }
        if (xfer::DoSdDownload(path.c_str(), kDownloadFile) != 1) {
            return make_response(req, http::status::internal_server_error);
        }
        send_file = kDownloadFile;
        Response res = make_response(req, http::status::ok);
        res.set(http::field::content_type, "application/octet-stream");
        return res;
    }
    if (method == "MOVE" || method == "COPY") {
        return move_or_copy(req, method, path);
    }
    return make_response(req, http::status::method_not_allowed);
}

} // namespace

namespace ftdi {

/**
 * @copydoc ftdi::DoWebDavServer
 * @brief Runs a local WebDAV server that translates standard WebDAV methods to cartridge file commands.
 * @details Listens on the specified port, accepts connection requests, and processes WebDAV methods:
 * - OPTIONS: Returns supported methods (OPTIONS, GET, PROPFIND, PUT, DELETE, MKCOL, MOVE, COPY).
 * - PROPFIND: Retrieves item metadata and directory listings from the Saturn cartridge.
 * - PUT: Uploads host file contents to the cartridge.
 * - DELETE: Deletes files or directories from the cartridge.
 * - MKCOL: Creates directories on the cartridge.
 * - MOVE: Renames or moves files/directories on the cartridge.
 * - GET: Downloads files from the cartridge.
 * - COPY: Copies files on the cartridge by downloading and re-uploading locally.
 *
 * It uses case-insensitive lookup (via `get_item_metadata`) to safely resolve user-provided paths 
 * against the FatFS on the console.
 *
 * @param port The TCP port to listen on.
 * @return 0 on successful termination, 1 on critical server failure.
 */
int DoWebDavServer(uint16_t port) {
    try {
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc};
        acceptor.open(tcp::v4());
        acceptor.set_option(net::socket_base::reuse_address(true));
        // No authentication, so never expose the SD card to the network.
        acceptor.bind({net::ip::address_v4::loopback(), port});
        acceptor.listen(net::socket_base::max_listen_connections);
        acceptor.non_blocking(true);

        std::cout << "[WebDAV] Server listening on 127.0.0.1:" << port << " (via Boost.Beast)" << std::endl;

        while (!g_interrupt_flag) {
            boost::system::error_code ec;
            tcp::socket socket{ioc};
            
            acceptor.accept(socket, ec);
            if (ec == net::error::would_block) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (ec) {
                std::cerr << "[WebDAV] Accept error: " << ec.message() << std::endl;
                continue;
            }

            beast::flat_buffer buffer;
            http::request_parser<http::file_body> parser;
            parser.body_limit(std::numeric_limits<std::uint32_t>::max()); // FAT32's file size limit; Beast defaults to 1 MB
            parser.get().body().open(kUploadFile, beast::file_mode::write, ec);
            if (ec) {
                std::cerr << "[WebDAV] Cannot create " << kUploadFile << ": " << ec.message() << std::endl;
                continue;
            }

            // Read with a deadline: a client that connects and sends nothing must not block the server or Ctrl-C.
            bool done = false;
            http::async_read(socket, buffer, parser, [&](beast::error_code e, std::size_t) {
                ec = e;
                done = true;
            });
            ioc.restart();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!done && !g_interrupt_flag && std::chrono::steady_clock::now() < deadline) {
                ioc.run_for(std::chrono::milliseconds(200));
            }
            if (!done) {
                socket.close(ec);
                ioc.restart();
                ioc.run(); // let the cancelled read complete
                if (!g_interrupt_flag) {
                    std::cerr << "[WebDAV] Client sent no complete request within 30 s; dropped." << std::endl;
                }
                continue;
            }
            if (ec) {
                std::cerr << "[WebDAV] Bad request: " << ec.message() << std::endl;
                continue;
            }

            std::string send_file;
            Response res = handle_request(parser.get(), send_file);
            parser.get().body().close();
            std::remove(kUploadFile);
            res.set(http::field::connection, "close");
            if (send_file.empty()) {
                res.prepare_payload();
                http::write(socket, res, ec);
            } else {
                {
                    http::response<http::file_body> file_res;
                    file_res.base() = res.base();
                    file_res.body().open(send_file.c_str(), beast::file_mode::scan, ec);
                    if (!ec) {
                        file_res.prepare_payload();
                        http::write(socket, file_res, ec);
                    } else {
                        Response error_res{http::status::internal_server_error, res.version()};
                        error_res.set(http::field::connection, "close");
                        error_res.prepare_payload();
                        boost::system::error_code ignored;
                        http::write(socket, error_res, ignored);
                    }
                }
                std::remove(send_file.c_str()); // after the file is closed (Windows can't delete open files)
                if (ec) {
                    std::cerr << "[WebDAV] Sending " << send_file << " failed: " << ec.message() << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[WebDAV] Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

} // namespace ftdi
