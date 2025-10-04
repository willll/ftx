#pragma once

#include <iostream>

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

#else
// In debug mode (NDEBUG not defined), cdbg outputs to std::cout
#define cdbg std::cout
#endif