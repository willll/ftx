#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

// Define a debug logger that outputs to std::cout when debugging is enabled
#ifdef NDEBUG
// In release mode (NDEBUG defined), cdbg is a no-op stream
class NullLogger {
public:
    template<typename T>
    NullLogger& operator<<(const T&) { return *this; }
    // Handle manipulators like std::endl
    NullLogger& operator<<(std::ostream& (*manip)(std::ostream&)) { return *this; }
};

// Global no-op logger instance
static NullLogger cdbg;

#define CDBG_LOG_ON_CHANGE(KEY, EXPR) do { } while (0)

#else
// In debug mode (NDEBUG not defined), cdbg outputs to std::cout
#define cdbg std::cout

inline bool cdbg_should_log_on_change(const std::string &key, const std::string &message)
{
    static std::mutex s_mutex;
    static std::unordered_map<std::string, std::string> s_last_messages;

    std::lock_guard<std::mutex> guard(s_mutex);
    auto it = s_last_messages.find(key);
    if (it != s_last_messages.end() && it->second == message)
    {
        return false;
    }

    s_last_messages[key] = message;
    return true;
}

#define CDBG_LOG_ON_CHANGE(KEY, EXPR)                                        \
    do                                                                        \
    {                                                                         \
        std::ostringstream cdbg_oss;                                          \
        cdbg_oss << EXPR;                                                     \
        const std::string cdbg_msg = cdbg_oss.str();                          \
        if (cdbg_should_log_on_change((KEY), cdbg_msg))                       \
        {                                                                     \
            cdbg << cdbg_msg;                                                 \
        }                                                                     \
    } while (0)
#endif