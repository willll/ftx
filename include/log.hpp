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

/**
 * @defgroup Logging Debug Logging Utilities
 * @brief Compile-time configurable debug logging.
 * @{
 */

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

/**
 * @def CDBG_LOG_ON_CHANGE(KEY, EXPR)
 * @brief Log expression only if its value differs from the last logged value.
 * @param KEY A unique identifier for the value (for deduplication).
 * @param EXPR The expression to log (typically building a message with <<).
 *
 * In Release mode, this macro expands to nothing.
 */
#define CDBG_LOG_ON_CHANGE(KEY, EXPR) do { } while (0)

#else
// In debug mode (NDEBUG not defined), cdbg outputs to std::cout
/**
 * @def cdbg
 * @brief Debug output stream (no-op in Release mode).
 * @details Alias for std::cout in Debug mode, NullLogger in Release.
 * Usage: `cdbg << "message" << std::endl;`
 */
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

/**
 * @def CDBG_LOG_ON_CHANGE(KEY, EXPR)
 * @brief Log expression only if its value differs from the last logged value.
 * @param KEY A unique identifier for the value (for deduplication).
 * @param EXPR The expression to log (typically building a message with <<).
 *
 * Maintains a static map of message values keyed by KEY. Only logs if the
 * current message differs from the previous one, reducing log spam from
 * repeated identical events.
 */
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

/** @} */ // end of Logging group