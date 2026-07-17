/**
 * @file ftdi.hpp
 * @brief Main FTDI USB device communication interface.
 * @details Provides initialization, device listing, console, and TCP proxy modes.
 * @author Anders Montonen (original), contributors
 * @copyright 2012, 2013, 2015 Anders Montonen
 * @license BSD 2-Clause
 *
 * This header defines the core FTDI communication API used throughout ftx:
 * - Device initialization and cleanup
 * - Interactive console mode
 * - Debug console logging
 * - Raw TCP<->FTDI proxy for debugger support
 */

/*

    Sega Saturn USB flash cart transfer utility
    Copyright © 2012, 2013, 2015 Anders Montonen
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

*/

#pragma once

#include <ftdi.h>
#include <string>
#include <atomic>

namespace ftdi {

// Global interrupt flag
extern std::atomic<bool> g_interrupt_flag;

// Global FTDI device context
extern struct ftdi_context g_Device;

/**
 * @brief Initialize FTDI device communication.
 * @param VID USB Vendor ID.
 * @param PID USB Product ID.
 * @param Serial Device serial number (empty string to match any).
 * @param recover Flag indicating whether to attempt recovery on error.
 * @return 1 on success, 0 on error.
 */
int InitComms(int VID, int PID, const std::string& Serial, bool recover = false);

/**
 * @brief Close FTDI device communication and purge buffers.
 */
void CloseComms();

/**
 * @brief List available FTDI devices.
 * @param vid USB Vendor ID to filter.
 * @param pid USB Product ID to filter.
 */
void ListDevices(int vid, int pid);

/**
 * @brief Signal handler for clean exit on interrupt.
 * @param sig Signal number.
 */
void Signal(int sig);

/**
 * @brief Enter debug console mode, printing device output to stdout.
 * @param enable_stdin Whether to read user input and forward it to the device.
 * @param acknowledge Whether to send acknowledgment bytes to the device.
 */
void DoConsole(bool enable_stdin, bool acknowledge = false);

/**
 * @brief Start a raw TCP proxy that forwards bytes between TCP and FTDI.
 * @param port TCP port to listen on (default 1234).
 * @param verbose If true, prints traced RSP commands with GDB>/Target> prefixes.
 * @return Exit status code.
 */
int DoTcpProxy(uint16_t port = 1234, bool verbose = false);

/**
 * @brief Start a WebDAV server that exposes the Saturn FAT filesystem.
 * @param port TCP port to listen on (default 8080).
 * @return Exit status code.
 */
int DoWebDavServer(uint16_t port = 8080);

} // namespace ftdi
