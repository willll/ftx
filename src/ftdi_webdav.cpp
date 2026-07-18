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
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <thread>
#include <chrono>

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
 * @return Conforming XML response string.
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
        if (xfer::DoListStr(saturn_path.c_str(), listing) == 1) {
            std::vector<FileEntry> sub_entries = parse_directory_listing(listing);
            for (const auto& e : sub_entries) {
                std::string sub_path = (path == "/") ? ("/" + e.name) : (path + "/" + e.name);
                add_response(sub_path, e);
            }
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
        acceptor.bind({tcp::v4(), port});
        acceptor.listen(net::socket_base::max_listen_connections);
        acceptor.non_blocking(true);

        std::cout << "[WebDAV] Server listening on port " << port << " (via Boost.Beast)" << std::endl;

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
            http::request<http::string_body> req;
            http::read(socket, buffer, req, ec);
            if (ec) {
                continue;
            }

            std::string method{req.method_string()};
            std::string raw_path{req.target()};

            std::string depth{req["Depth"]};
            if (depth.empty()) {
                depth = "1";
            }

            std::string path = url_decode(raw_path);
            size_t q = path.find('?');
            if (q != std::string::npos) {
                path = path.substr(0, q);
            }
            if (path.size() > 1 && path.back() == '/') {
                path.pop_back();
            }

            std::cout << "[WebDAV] Request: " << method << " " << path << std::endl;

            if (method == "OPTIONS") {
                http::response<http::empty_body> res{http::status::ok, req.version()};
                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(http::field::allow, "OPTIONS, GET, PROPFIND, PUT, DELETE, MKCOL, MOVE, COPY");
                res.set("DAV", "1");
                res.set(http::field::connection, "close");
                res.prepare_payload();
                http::write(socket, res, ec);
            }
            else if (method == "PROPFIND") {
                FileEntry target_entry;
                if (!get_item_metadata(path, target_entry)) {
                    http::response<http::empty_body> res{http::status::not_found, req.version()};
                    res.set(http::field::connection, "close");
                    res.prepare_payload();
                    http::write(socket, res, ec);
                } else {
                    std::string xml = build_multistatus_xml(path, target_entry, depth);
                    http::response<http::string_body> res{http::status::multi_status, req.version()};
                    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                    res.set(http::field::content_type, "application/xml; charset=utf-8");
                    res.set(http::field::connection, "close");
                    res.body() = xml;
                    res.prepare_payload();
                    http::write(socket, res, ec);
                }
            }
            else if (method == "PUT") {
                if (!is_valid_83_path(path)) {
                    std::cerr << "[WebDAV] Rejected PUT: Path component violates strict 8.3 filename conventions: " << path << std::endl;
                    http::response<http::string_body> res{http::status::bad_request, req.version()};
                    res.set(http::field::connection, "close");
                    res.body() = "Error: Path violates strict 8.3 filename conventions (max 8 characters for name, 3 characters for extension).";
                    res.prepare_payload();
                    http::write(socket, res, ec);
                } else {
                    std::string temp_filename = "ftx_webdav_temp.bin";
                    std::ofstream temp_file(temp_filename, std::ios::binary);
                    if (!temp_file) {
                        http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                        res.set(http::field::connection, "close");
                        res.prepare_payload();
                        http::write(socket, res, ec);
                    } else {
                        temp_file.write(req.body().data(), req.body().size());
                        temp_file.close();

                        int status = xfer::DoSdUpload(temp_filename.c_str(), path.c_str());
                        std::remove(temp_filename.c_str());

                        if (status == 1) {
                            http::response<http::empty_body> res{http::status::created, req.version()};
                            res.set(http::field::connection, "close");
                            res.prepare_payload();
                            http::write(socket, res, ec);
                        } else {
                            http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                            res.set(http::field::connection, "close");
                            res.prepare_payload();
                            http::write(socket, res, ec);
                        }
                    }
                }
            }
            else if (method == "DELETE") {
                int status = xfer::DoRemove(path.c_str());
                if (status != 1) {
                    status = xfer::DoRmdir(path.c_str());
                }

                if (status == 1) {
                    http::response<http::empty_body> res{http::status::no_content, req.version()};
                    res.set(http::field::connection, "close");
                    res.prepare_payload();
                    http::write(socket, res, ec);
                } else {
                    http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                    res.set(http::field::connection, "close");
                    res.prepare_payload();
                    http::write(socket, res, ec);
                }
            }
            else if (method == "MKCOL") {
                if (!is_valid_83_path(path)) {
                    std::cerr << "[WebDAV] Rejected MKCOL: Path component violates strict 8.3 filename conventions: " << path << std::endl;
                    http::response<http::string_body> res{http::status::bad_request, req.version()};
                    res.set(http::field::connection, "close");
                    res.body() = "Error: Directory name violates strict 8.3 conventions (max 8 characters).";
                    res.prepare_payload();
                    http::write(socket, res, ec);
                } else {
                    int status = xfer::DoMkdir(path.c_str());
                    if (status == 1) {
                        http::response<http::empty_body> res{http::status::created, req.version()};
                        res.set(http::field::connection, "close");
                        res.prepare_payload();
                        http::write(socket, res, ec);
                    } else {
                        http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                        res.set(http::field::connection, "close");
                        res.prepare_payload();
                        http::write(socket, res, ec);
                    }
                }
            }
            else if (method == "MOVE") {
                std::string dest_header{req["Destination"]};
                if (dest_header.empty()) {
                    http::response<http::empty_body> res{http::status::bad_request, req.version()};
                    res.set(http::field::connection, "close");
                    res.prepare_payload();
                    http::write(socket, res, ec);
                } else {
                    std::string dest_path;
                    size_t proto_pos = dest_header.find("://");
                    if (proto_pos != std::string::npos) {
                        size_t path_pos = dest_header.find('/', proto_pos + 3);
                        if (path_pos != std::string::npos) {
                            dest_path = dest_header.substr(path_pos);
                        } else {
                            dest_path = "/";
                        }
                    } else {
                        dest_path = dest_header;
                    }
                    dest_path = url_decode(dest_path);
                    size_t q = dest_path.find('?');
                    if (q != std::string::npos) {
                        dest_path = dest_path.substr(0, q);
                    }
                    if (dest_path.size() > 1 && dest_path.back() == '/') {
                        dest_path.pop_back();
                    }

                    if (!is_valid_83_path(dest_path)) {
                        std::cerr << "[WebDAV] Rejected MOVE: Destination path component violates strict 8.3 filename conventions: " << dest_path << std::endl;
                        http::response<http::string_body> res{http::status::bad_request, req.version()};
                        res.set(http::field::connection, "close");
                        res.body() = "Error: Destination path violates strict 8.3 filename conventions (max 8 characters for name, 3 characters for extension).";
                        res.prepare_payload();
                        http::write(socket, res, ec);
                    } else {
                        FileEntry source_entry;
                        if (!get_item_metadata(path, source_entry)) {
                            http::response<http::empty_body> res{http::status::not_found, req.version()};
                            res.set(http::field::connection, "close");
                            res.prepare_payload();
                            http::write(socket, res, ec);
                        } else {
                            FileEntry dest_entry;
                            bool dest_exists = get_item_metadata(dest_path, dest_entry);
                            std::string overwrite{req["Overwrite"]};
                            if (dest_exists && overwrite == "F") {
                                http::response<http::empty_body> res{http::status::precondition_failed, req.version()};
                                res.set(http::field::connection, "close");
                                res.prepare_payload();
                                http::write(socket, res, ec);
                            } else {
                                if (dest_exists) {
                                    // Remove the destination first to allow f_rename to succeed
                                    int remove_status = xfer::DoRemove(dest_path.c_str());
                                    if (remove_status != 1) {
                                        xfer::DoRmdir(dest_path.c_str());
                                    }
                                }

                                int status = xfer::DoRename(path.c_str(), dest_path.c_str());
                                if (status == 1) {
                                    http::status response_status = dest_exists ? http::status::no_content : http::status::created;
                                    http::response<http::empty_body> res{response_status, req.version()};
                                    res.set(http::field::connection, "close");
                                    res.prepare_payload();
                                    http::write(socket, res, ec);
                                } else {
                                    http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                                    res.set(http::field::connection, "close");
                                    res.prepare_payload();
                                    http::write(socket, res, ec);
                                }
                            }
                        }
                    }
                }
            }
            else if (method == "GET") {
                FileEntry entry;
                if (!get_item_metadata(path, entry) || entry.is_dir) {
                    http::response<http::empty_body> res{http::status::not_found, req.version()};
                    res.set(http::field::connection, "close");
                    res.prepare_payload();
                    http::write(socket, res, ec);
                } else {
                    std::string temp_filename = "ftx_webdav_get_temp.bin";
                    int status = xfer::DoSdDownload(path.c_str(), temp_filename.c_str());
                    if (status != 1) {
                        std::remove(temp_filename.c_str());
                        http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                        res.set(http::field::connection, "close");
                        res.prepare_payload();
                        http::write(socket, res, ec);
                    } else {
                        std::ifstream file(temp_filename, std::ios::binary);
                        if (!file) {
                            std::remove(temp_filename.c_str());
                            http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                            res.set(http::field::connection, "close");
                            res.prepare_payload();
                            http::write(socket, res, ec);
                        } else {
                            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                            file.close();
                            std::remove(temp_filename.c_str());

                            http::response<http::string_body> res{http::status::ok, req.version()};
                            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                            res.set(http::field::content_type, "application/octet-stream");
                            res.set(http::field::connection, "close");
                            res.body() = std::move(content);
                            res.prepare_payload();
                            http::write(socket, res, ec);
                        }
                    }
                }
            }
            else if (method == "COPY") {
                std::string dest_header{req["Destination"]};
                if (dest_header.empty()) {
                    http::response<http::empty_body> res{http::status::bad_request, req.version()};
                    res.set(http::field::connection, "close");
                    res.prepare_payload();
                    http::write(socket, res, ec);
                } else {
                    std::string dest_path;
                    size_t proto_pos = dest_header.find("://");
                    if (proto_pos != std::string::npos) {
                        size_t path_pos = dest_header.find('/', proto_pos + 3);
                        if (path_pos != std::string::npos) {
                            dest_path = dest_header.substr(path_pos);
                        } else {
                            dest_path = "/";
                        }
                    } else {
                        dest_path = dest_header;
                    }
                    dest_path = url_decode(dest_path);
                    size_t q = dest_path.find('?');
                    if (q != std::string::npos) {
                        dest_path = dest_path.substr(0, q);
                    }
                    if (dest_path.size() > 1 && dest_path.back() == '/') {
                        dest_path.pop_back();
                    }

                    if (!is_valid_83_path(dest_path)) {
                        std::cerr << "[WebDAV] Rejected COPY: Destination path component violates strict 8.3 filename conventions: " << dest_path << std::endl;
                        http::response<http::string_body> res{http::status::bad_request, req.version()};
                        res.set(http::field::connection, "close");
                        res.body() = "Error: Destination path violates strict 8.3 filename conventions (max 8 characters for name, 3 characters for extension).";
                        res.prepare_payload();
                        http::write(socket, res, ec);
                    } else {
                        FileEntry source_entry;
                        if (!get_item_metadata(path, source_entry)) {
                            http::response<http::empty_body> res{http::status::not_found, req.version()};
                            res.set(http::field::connection, "close");
                            res.prepare_payload();
                            http::write(socket, res, ec);
                        } else {
                            FileEntry dest_entry;
                            bool dest_exists = get_item_metadata(dest_path, dest_entry);
                            std::string overwrite{req["Overwrite"]};
                            if (dest_exists && overwrite == "F") {
                                http::response<http::empty_body> res{http::status::precondition_failed, req.version()};
                                res.set(http::field::connection, "close");
                                res.prepare_payload();
                                http::write(socket, res, ec);
                            } else {
                                if (dest_exists) {
                                    int remove_status = xfer::DoRemove(dest_path.c_str());
                                    if (remove_status != 1) {
                                        xfer::DoRmdir(dest_path.c_str());
                                    }
                                }

                                std::string temp_filename = "ftx_webdav_copy_temp.bin";
                                int download_status = xfer::DoSdDownload(path.c_str(), temp_filename.c_str());
                                int upload_status = 0;
                                if (download_status == 1) {
                                    upload_status = xfer::DoSdUpload(temp_filename.c_str(), dest_path.c_str());
                                }
                                std::remove(temp_filename.c_str());

                                if (download_status == 1 && upload_status == 1) {
                                    http::status response_status = dest_exists ? http::status::no_content : http::status::created;
                                    http::response<http::empty_body> res{response_status, req.version()};
                                    res.set(http::field::connection, "close");
                                    res.prepare_payload();
                                    http::write(socket, res, ec);
                                } else {
                                    http::response<http::empty_body> res{http::status::internal_server_error, req.version()};
                                    res.set(http::field::connection, "close");
                                    res.prepare_payload();
                                    http::write(socket, res, ec);
                                }
                            }
                        }
                    }
                }
            }
            else {
                http::response<http::empty_body> res{http::status::method_not_allowed, req.version()};
                res.set(http::field::connection, "close");
                res.prepare_payload();
                http::write(socket, res, ec);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[WebDAV] Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

} // namespace ftdi
