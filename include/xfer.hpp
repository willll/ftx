/**
 * @file xfer.hpp
 * @brief Data transfer operations (upload, download, execute).
 * @details Defines protocols and constants for communicating with Saturn cartridges.
 * @author Anders Montonen (original)
 * @copyright 2012, 2013, 2015 Anders Montonen
 * @license BSD 2-Clause
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
#include <cstdint>

/**
 * @namespace xfer
 * @brief Data transfer operations for Saturn cartridges.
 */
namespace xfer {

/**
 * @brief USB read packet size in bytes.
 * @details Specifies the maximum chunk size for reading from the device.
 */
const std::size_t USB_READPACKET_SIZE = 64 * 1024;
/**
 * @brief USB write packet size in bytes.
 * @details Specifies the maximum chunk size for writing to the device.
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
 * @brief List files and directories in a path.
 * @param path Directory path to list.
 * @return 1 on success, 0 on error.
 */
int DoList(const char *path);

/**
 * @brief Remove a file or an empty directory.
 * @param path File or directory path to remove.
 * @return 1 on success, 0 on error.
 */
int DoRemove(const char *path);

/**
 * @brief Create a directory.
 * @param path Directory path to create.
 * @return 1 on success, 0 on error.
 */
int DoMkdir(const char *path);

/**
 * @brief Delete a directory.
 * @param path Directory path to delete.
 * @return 1 on success, 0 on error.
 */
int DoRmdir(const char *path);

/**
 * @brief Compute and print the CRC-8 of a file.
 * @param filename Input file name.
 * @return 1 on success, 0 on error.
 */
int DoCrc(const char *filename);

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
 * @param execute If true, execute the uploaded data.
 * @return 1 on success, 0 on error.
 */
int DoUpload(const char* filename, uint32_t address, const bool execute = false);

/**
 * @brief Copy a local file to a raw SD card range.
 * @param host_filename Input file name.
 * @param saturn_sd_path Raw SD target path, typically sdraw:\<start\>:\<count\>.
 * @return 1 on success, 0 on error.
 */
int DoSdUpload(const char *host_filename, const char *saturn_sd_path);

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
 * @brief Parse a numeric argument from string.
 * @param arg Input string.
 * @param result Output parsed value.
 */
void ParseNumericArg(const char* arg, uint32_t * result);

} // namespace xfer
