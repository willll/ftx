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
#include <cstdint>

namespace xfer {


/**
 * @brief USB read packet size in bytes.
 */
constexpr std::size_t USB_READPACKET_SIZE = 64 * 1024;
/**
 * @brief USB write packet size in bytes.
 */
constexpr std::size_t USB_WRITEPACKET_SIZE = 4 * 1024;
/**
 * @brief Calculate payload size for a given packet size.
 * @param x Packet size in bytes.
 * @return Payload size in bytes.
 */
constexpr std::size_t USB_PAYLOAD(std::size_t x) { return x - ((x / 64) * 2); }
/**
 * @brief Payload size for read operations.
 */
constexpr std::size_t READ_PAYLOAD_SIZE = USB_PAYLOAD(USB_READPACKET_SIZE);
/**
 * @brief Payload size for write operations.
 */
constexpr std::size_t WRITE_PAYLOAD_SIZE = USB_WRITEPACKET_SIZE;

/**
 * @brief Dump BIOS.
 */
int DoBiosDump(const char *filename);

/**
 * @brief Download data from device and write to file.
 * @param filename Output file name.
 * @param address Device address to read from.
 * @param size Number of bytes to download.
 * @return 1 on success, 0 on error.
 */
int DoDownload(const char* filename, uint32_t address, std::size_t size);

/**
 * @brief Upload data from file to device.
 * @param filename Input file name.
 * @param address Device address to write to.
 * @return 1 on success, 0 on error.
 */
int DoUpload(const char* filename, uint32_t address, const bool execute = false);

/**
 * @brief Send execute command to device at given address.
 * @param address Address to execute.
 * @return 1 on success, 0 on error.
 */
int DoRun(unsigned int address);

/**
 * @brief Upload file to device and execute at given address.
 * @param filename Input file name.
 * @param address Address to execute.
 * @return 1 on success, 0 on error.
 */
int DoExecute(const char* filename, uint32_t address);

/**
 * @brief Initialize FTDI device communication.
 * @param VID USB Vendor ID.
 * @param PID USB Product ID.
 * @param recover Flag indicating whether to attempt recovery on error.
 * @return 1 on success, 0 on error.
 */
int InitComms(int VID, int PID, bool recover=false);

/**
 * @brief Close FTDI device communication and purge buffers.
 */
void CloseComms();

/**
 * @brief Parse a numeric argument from string.
 * @param arg Input string.
 * @param result Output parsed value.
 */
void ParseNumericArg(const char* arg, uint32_t * result);

/**
 * @brief Enter debug console mode, printing device output to stdout.
 */
void DoConsole(bool acknowledge = false);

/**
 * @brief Signal handler for clean exit on interrupt.
 * @param sig Signal number.
 */
void Signal(int sig);

} // namespace xfer
