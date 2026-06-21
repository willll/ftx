/**
 * @file log.hpp
 * @brief Deduplicating debug logger with conditional compilation.
 * @details Provides `cdbg` global logger and `CDBG_LOG_ON_CHANGE` macro.
 *          In Release mode (NDEBUG defined), all debug output is compiled out.
 *          In Debug mode, output goes to stdout with deduplication support.
 */

#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

extern bool g_verbose;

/**
 * @defgroup Logging Debug Logging Utilities
 * @brief Runtime configurable debug logging.
 * @{
 */

class ConditionalLogger {
public:
    template<typename T>
    const ConditionalLogger& operator<<(const T& msg) const {
        if (g_verbose) {
            std::cout << msg;
        }
        return *this;
    }

    const ConditionalLogger& operator<<(std::ostream& (*manip)(std::ostream&)) const {
        if (g_verbose) {
            manip(std::cout);
        }
        return *this;
    }
};

static const ConditionalLogger cdbg;

inline bool cdbg_should_log_on_change(const std::string &key, const std::string &message)
{
    if (!g_verbose) return false;
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

#define CDBG_LOG_ON_CHANGE(KEY, EXPR)                                         \
    do                                                                        \
    {                                                                         \
        if (g_verbose) {                                                      \
            std::ostringstream cdbg_oss;                                      \
            cdbg_oss << EXPR;                                                 \
            const std::string cdbg_msg = cdbg_oss.str();                      \
            if (cdbg_should_log_on_change((KEY), cdbg_msg))                   \
            {                                                                 \
                std::cout << cdbg_msg;                                        \
            }                                                                 \
        }                                                                     \
    } while (0)

/** @} */ // end of Logging group