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

#include <ftdi.h>

#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>

#include "crc.hpp"
#include "ftdi.hpp"
#include "log.hpp"
#include "saturn.hpp"
#include "xfer.hpp"

#include "sc_common.h"

namespace xfer
{

  namespace
  {
    using DirectoryEntry = std::filesystem::directory_entry;
  }

  unsigned char SendBuf[2 * WRITE_PAYLOAD_SIZE];
  unsigned char RecvBuf[2 * READ_PAYLOAD_SIZE];

  /**
   * @brief Send a command with address and length to the device.
   * @param cmd Command code.
   * @param address Target address.
   * @param size Data size.
   * @return Number of bytes written, or negative on error.
   */
  int SendCommandWithAddressAndLength(unsigned int cmd, uint32_t address,
                                      unsigned int size)
  {
    uint8_t i = 0;

    // Step 1: Set the command code in the send buffer
    SendBuf[i++] = cmd;
    // Step 2: Extract and store the address bytes in big-endian order
    SendBuf[i++] = static_cast<unsigned char>(address >> 24);
    SendBuf[i++] = static_cast<unsigned char>(address >> 16);
    SendBuf[i++] = static_cast<unsigned char>(address >> 8);
    SendBuf[i++] = static_cast<unsigned char>(address);
    // Step 3: Extract and store the size bytes in big-endian order
    SendBuf[i++] = static_cast<unsigned char>(size >> 24);
    SendBuf[i++] = static_cast<unsigned char>(size >> 16);
    SendBuf[i++] = static_cast<unsigned char>(size >> 8);
    SendBuf[i++] = static_cast<unsigned char>(size);

    // Step 4: If the command is for execution, append four zero bytes for reset
    // flag
    if (USBDC_FUNC_EXEC_EXT == cmd)
    {
      SendBuf[i++] = 0x00;
      SendBuf[i++] = 0x00;
      SendBuf[i++] = 0x00;
      SendBuf[i++] = 0x00;
    }

    // Step 5: Write the command buffer to the FTDI device
    return ftdi_write_data(&ftdi::g_Device, SendBuf, i);
  }

  /**
   * @brief Print transfer performance statistics.
   * @param pStartTime Start time of transfer.
   * @param pEndTime End time of transfer.
   * @param size Number of bytes transferred.
   */
  void ReportPerformance(const std::chrono::steady_clock::time_point &start,
                         const std::chrono::steady_clock::time_point &end,
                         unsigned int size)
  {
    using namespace std::chrono;
    // Step 1: Calculate the duration of the transfer in microseconds
    auto delta_us = duration_cast<microseconds>(end - start).count();
    // Step 2: Convert duration to seconds
    double seconds = delta_us / 1'000'000.0;
    // Step 3: Calculate transfer speed in KB/s
    double speed_kb_s = (size / 1024.0) / seconds;
    // Step 4: Output transfer time
    std::cout << "Transfer time: " << std::fixed << std::setprecision(3)
              << seconds << " s" << std::endl;
    // Step 5: Output transfer speed
    std::cout << "Transfer speed: " << std::fixed << std::setprecision(2)
              << speed_kb_s << " KB/s" << std::endl;
  }

  int DoBiosDump(const char *filename)
  {
    // Step 1: Log the start of the BIOS dump process
    std::cout << "[DoBiosDump] Starting BIOS dump to file: " << filename
              << std::endl;
    // Step 2: Call DoDownload to perform the dump from the BIOS address and size
    return DoDownload(filename, saturn::bios_address, saturn::bios_size);
  }

  int DoList(const char *path)
  {
    std::error_code ec;
    std::filesystem::path inputPath(path);

    if (!std::filesystem::exists(inputPath, ec))
    {
      std::cerr << "[DoList] Path does not exist: " << path << std::endl;
      return 0;
    }

    if (std::filesystem::is_regular_file(inputPath, ec))
    {
      std::cout << "[DoList] " << inputPath.filename().string() << " [F]"
                << std::endl;
      return 1;
    }

    if (!std::filesystem::is_directory(inputPath, ec))
    {
      std::cerr << "[DoList] Path is neither a file nor a directory: " << path
                << std::endl;
      return 0;
    }

    std::vector<DirectoryEntry> entries;
    for (const auto &entry : std::filesystem::directory_iterator(inputPath, ec))
    {
      if (ec)
      {
        std::cerr << "[DoList] Directory iteration error: "
                  << ec.message() << std::endl;
        return 0;
      }
      entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry &lhs, const DirectoryEntry &rhs) {
                return lhs.path().filename().string() < rhs.path().filename().string();
              });

    std::cout << "[DoList] Listing: " << inputPath.string() << std::endl;
    for (const auto &entry : entries)
    {
      std::error_code statusEc;
      const bool isDir = entry.is_directory(statusEc);
      const bool isFile = entry.is_regular_file(statusEc);
      std::cout << "  " << (isDir ? "[D]" : isFile ? "[F]" : "[?]") << " "
                << entry.path().filename().string() << std::endl;
    }

    return 1;
  }

  int DoRemove(const char *path)
  {
    std::error_code ec;
    const std::filesystem::path target(path);

    if (!std::filesystem::exists(target, ec))
    {
      std::cerr << "[DoRemove] Path does not exist: " << path << std::endl;
      return 0;
    }

    if (!std::filesystem::remove(target, ec))
    {
      if (ec)
      {
        std::cerr << "[DoRemove] Remove failed: " << ec.message() << std::endl;
      }
      else
      {
        std::cerr << "[DoRemove] Path not removed (directory may not be empty): "
                  << path << std::endl;
      }
      return 0;
    }

    std::cout << "[DoRemove] Removed: " << path << std::endl;
    return 1;
  }

  int DoCrc(const char *filename)
  {
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
      std::cerr << "[DoCrc] Can't open file '" << filename << "'" << std::endl;
      return 0;
    }

    unsigned char buffer[4096];
    crc8::crc_t checksum = 0;
    size_t bytesRead = 0;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
      checksum = crc8::crc_update(checksum, buffer, bytesRead);
    }

    if (ferror(file))
    {
      std::cerr << "[DoCrc] File read error." << std::endl;
      fclose(file);
      return 0;
    }

    fclose(file);

    std::cout << "[DoCrc] " << filename << " CRC-8 = 0x"
              << std::hex << std::uppercase << std::setw(2)
              << std::setfill('0') << static_cast<unsigned int>(checksum)
              << std::nouppercase << std::dec << std::setfill(' ') << std::endl;
    return 1;
  }

  /**
   * @brief Download data from device and write to file.
   * @param filename Output file name.
   * @param address Device address to read from.
   * @param size Number of bytes to download.
   * @return 1 on success, 0 on error.
   */
  int DoDownload(const char *filename, uint32_t address, std::size_t size)
  {
    // Step 1: Log the start of the download process with file, address, and size
    // details
    std::cout << "[DoDownload] Starting download: file='" << filename
              << "', address=0x" << std::hex << address << ", size=" << std::dec
              << size << std::endl;
    // Step 2: Allocate a buffer to store the downloaded data
    std::unique_ptr<unsigned char[]> pFileBuffer(new unsigned char[size]);
    FILE *File = nullptr;
    std::size_t received = 0;
    int status = -1;
    crc8::crc_t readChecksum = 0, calcChecksum = 0;
    // Step 3: Record the start time for performance measurement
    auto before = std::chrono::steady_clock::now();
    // Step 4: Log sending of the download command
    std::cout << "[DoDownload] Sending download command..." << std::endl;
    // Step 5: Send the download command to the device
    status =
        xfer::SendCommandWithAddressAndLength(USBDC_FUNC_DOWNLOAD, address, size);
    if (status < 0)
    {
      // Step 6: Handle error if the download command fails
      std::cerr << "[DoDownload] Send download command error: "
                << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }

    // Step 7: Log the start of data reading from the device
    std::cout << "[DoDownload] Reading data from device..." << std::endl;
    // Step 8: Read data from the device until all requested bytes are received
    while (size - received > 0)
    {
      status = ftdi_read_data(&ftdi::g_Device, &pFileBuffer[received],
                              size - received);
      if (status < 0)
      {
        // Step 9: Handle error if reading data fails
        std::cerr << "[DoDownload] Read data error: "
                  << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
      received += status;
      // Step 10: Log progress of received bytes
      std::cout << "[DoDownload] Received " << received << "/" << size
                << " bytes..." << std::endl;
    }

    // Step 11: Log waiting for the checksum byte
    std::cout << "[DoDownload] Waiting for checksum byte..." << std::endl;
    // Step 12: Read the checksum byte, looping until received or an error occurs
    do
    {
      status = ftdi_read_data(
          &ftdi::g_Device, reinterpret_cast<unsigned char *>(&readChecksum), 1);
      if (status < 0)
      {
        // Step 13: Handle error if reading checksum fails
        std::cerr << "[DoDownload] Read data error: "
                  << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
    } while (status == 0);

    // Step 14: Record the end time for performance measurement
    auto after = std::chrono::steady_clock::now();
    // Step 15: Log and calculate performance metrics
    std::cout << "[DoDownload] Data received. Calculating performance..."
              << std::endl;
    xfer::ReportPerformance(before, after, size);

    // Step 16: Log and calculate the CRC checksum of the received data
    std::cout << "[DoDownload] Calculating CRC..." << std::endl;
    calcChecksum = crc8::crc_update(calcChecksum, pFileBuffer.get(), size);

    // Step 17: Verify the calculated checksum against the received checksum
    if (readChecksum != calcChecksum)
    {
      // Step 18: Handle checksum mismatch error
      std::cerr << "[DoDownload] Checksum error (" << std::hex
                << static_cast<int>(calcChecksum) << ", should be "
                << static_cast<int>(readChecksum) << ")" << std::endl;
      return 0;
    }

    // Step 19: Log and open the output file for writing
    std::cout << "[DoDownload] Writing to file..." << std::endl;
    File = fopen(filename, "wb");
    if (File == nullptr)
    {
      // Step 20: Handle error if file creation fails
      std::cerr << "[DoDownload] Error creating output file" << std::endl;
      return 0;
    }
    // Step 21: Write the received data to the file
    fwrite(pFileBuffer.get(), 1, size, File);
    // Step 22: Close the file
    fclose(File);
    // Step 23: Log successful completion
    std::cout << "[DoDownload] Download complete." << std::endl;
    // Step 24: Return success or failure based on status
    return status < 0 ? 0 : 1;
  }

  /**
   * @brief Upload data from file to device.
   * @param filename Input file name.
   * @param address Device address to write to.
   * @return 1 on success, 0 on error.
   */
  int DoUpload(const char *filename, uint32_t address, const bool execute)
  {
    // Step 1: Determine function name based on execution flag
    std::string functnName = execute ? "DoUploadExecute" : "DoUpload";

    // Step 2: Log the start of the upload process
    std::cout << "[" << functnName << "] Starting upload: file='" << filename
              << "', address=0x" << std::hex << address << std::dec << std::endl;
    // Step 3: Open the input file for reading
    FILE *File = fopen(filename, "rb");
    if (!File)
    {
      // Step 4: Handle error if file cannot be opened
      std::cerr << "[" << functnName << "] Can't open the file '" << filename
                << "'" << std::endl;
      return 0;
    }

    // Step 5: Get the file size
    fseek(File, 0, SEEK_END);
    long size = ftell(File);
    fseek(File, 0, SEEK_SET);
    if (size <= 0)
    {
      // Step 6: Handle error if file is empty or size cannot be determined
      std::cerr << "[" << functnName << "] File is empty or error reading size."
                << std::endl;
      fclose(File);
      return 0;
    }

    // Step 7: Allocate a buffer for the file data
    std::unique_ptr<unsigned char[]> pFileBuffer(new unsigned char[size]);
    // Step 8: Read the file data into the buffer
    if (fread(pFileBuffer.get(), 1, size, File) != static_cast<size_t>(size))
    {
      // Step 9: Handle error if file read fails
      std::cerr << "[" << functnName << "] File read error" << std::endl;
      fclose(File);
      return 0;
    }
    // Step 10: Close the file
    fclose(File);

    // Step 11: Calculate the CRC checksum of the file data
    crc8::crc_t checksum = crc8::crc_update(checksum, pFileBuffer.get(), size);

    // Step 12: Record the start time for performance measurement
    auto before = std::chrono::steady_clock::now();

    // Step 13: Set the command based on whether execution is requested
    if (execute)
    {
      std::cout << "[" << functnName << "] Uploading and executing at address 0x"
                << std::hex << address << std::dec << std::endl;
      SendBuf[0] = USBDC_FUNC_EXEC_EXT;
    }
    else
    {
      std::cout << "[" << functnName << "] Uploading to address 0x" << std::hex
                << address << std::dec << std::endl;
      SendBuf[0] = USBDC_FUNC_UPLOAD;
    }
    // Step 14: Send the upload command to the device
    int status = xfer::SendCommandWithAddressAndLength(SendBuf[0], address, size);
    if (status < 0)
    {
      // Step 15: Handle error if sending the upload command fails
      std::cerr << "[" << functnName << "] Send upload command error: "
                << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }

    // Step 16: Send the file data to the device in chunks
    unsigned int sent = 0;
    while (size - sent > 0)
    {
      status = ftdi_write_data(&ftdi::g_Device, &pFileBuffer[sent], size - sent);
      if (status < 0)
      {
        // Step 17: Handle error if sending data fails
        std::cerr << "[" << functnName << "] Send data error: "
                  << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
      sent += status;
      // Step 18: Log progress of sent bytes
      std::cout << "[" << functnName << "] Sent " << sent << "/" << size
                << " bytes..." << std::endl;
    }

    // Step 19: Send the checksum to the device
    SendBuf[0] = static_cast<unsigned char>(checksum);
    status = ftdi_write_data(&ftdi::g_Device, SendBuf, 1);
    if (status < 0)
    {
      // Step 20: Handle error if sending checksum fails
      std::cerr << "[" << functnName << "] Send checksum error: "
                << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }

    // Step 21: Wait for the device to report the upload result
    do
    {
      status = ftdi_read_data(&ftdi::g_Device, RecvBuf, 1);
      if (status < 0)
      {
        // Step 22: Handle error if reading upload result fails
        std::cerr << "[" << functnName << "] Read upload result failed: "
                  << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
    } while (status == 0);

    // Step 23: Check if the device reported an upload error
    if (RecvBuf[0] != 0)
    {
      std::cerr << "[" << functnName << "] Device reported upload error."
                << std::endl;
      return 0;
    }

    // Step 24: Record the end time and calculate performance
    auto after = std::chrono::steady_clock::now();
    xfer::ReportPerformance(before, after, size);
    // Step 25: Log successful completion
    std::cout << "[" << functnName << "] Upload complete." << std::endl;

    // Step 26: Return success
    return 1;
  }

  /**
   * @brief Send execute command to device at given address.
   * @param address Address to execute.
   * @return 1 on success, 0 on error.
   */
  int DoRun(uint32_t address)
  {
    // Step 1: Log the execution attempt at the specified address
    std::cout << "[DoRun] Executing at address 0x" << std::hex << address
              << std::dec << std::endl;
    // Step 2: Set the execute command in the send buffer
    SendBuf[0] = USBDC_FUNC_EXEC;
    // Step 3: Extract and store the address bytes in big-endian order
    SendBuf[1] = static_cast<unsigned char>(address >> 24);
    SendBuf[2] = static_cast<unsigned char>(address >> 16);
    SendBuf[3] = static_cast<unsigned char>(address >> 8);
    SendBuf[4] = static_cast<unsigned char>(address);
    // Step 4: Send the execute command to the device
    int status = ftdi_write_data(&ftdi::g_Device, SendBuf, 5);
    if (status < 0)
    {
      // Step 5: Handle error if sending the execute command fails
      std::cerr << "[DoRun] Send execute error: "
                << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }
    // Step 6: Log successful command send
    std::cout << "[DoRun] Execute command sent successfully." << std::endl;
    // Step 7: Return success
    return 1;
  }

  /**
   * @brief Upload file to device and execute at given address.
   * @param filename Input file name.
   * @param address Address to execute.
   * @return 1 on success, 0 on error.
   */
  int DoExecute(const char *filename, uint32_t address)
  {
    // Step 1: Log the start of the upload and execute process
    std::cout << "[DoExecute] Uploading and executing: file='" << filename
              << "', address=0x" << std::hex << address << std::dec << std::endl;
    int status = 0;
    // Step 2: Call DoUpload with execute flag set to true
    if (DoUpload(filename, address, true))
    {
      // Step 3: Log successful upload
      std::cout << "[DoExecute] Upload successful. Executing..." << std::endl;
    }
    else
    {
      // Step 4: Handle error if upload fails
      std::cerr << "[DoExecute] Upload failed. Execution aborted." << std::endl;
    }
    // Step 5: Return status (currently always 0 due to uninitialized status)
    return status;
  }

  int DoSdUpload(const char *host_filename, const char *saturn_sd_path)
  {
    auto write_all = [](const unsigned char *data, size_t length,
                        const char *stage) -> bool
    {
      size_t sent = 0;
      while (sent < length)
      {
        const int status =
            ftdi_write_data(&ftdi::g_Device, data + sent, length - sent);
        if (status < 0)
        {
          std::cerr << "[DoSdUpload] " << stage
                    << " write error: "
                    << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
          return false;
        }
        if (status == 0)
        {
          continue;
        }
        sent += static_cast<size_t>(status);
      }
      return true;
    };

    auto read_one = [](unsigned char *outByte) -> bool
    {
      int status = 0;
      do
      {
        status = ftdi_read_data(&ftdi::g_Device, outByte, 1);
        if (status < 0)
        {
          std::cerr << "[DoSdUpload] Read result error: "
                    << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
          return false;
        }
      } while (status == 0);

      return true;
    };

    char *end = nullptr;
    const unsigned long parsed = std::strtoul(saturn_sd_path, &end, 0);
    if (saturn_sd_path == end || (end != nullptr && *end != '\0') ||
        parsed > 0xFFFFFFFFUL)
    {
      std::cerr << "[DoSdUpload] Invalid SD sector: '" << saturn_sd_path
                << "'. Expected a numeric sector index." << std::endl;
      return 0;
    }

    const uint32_t start_sector = static_cast<uint32_t>(parsed);

    const char *filename_for_display = host_filename;
    for (const char *p = host_filename; *p != '\0'; ++p)
    {
      if (*p == '/' || *p == '\\')
      {
        filename_for_display = p + 1;
      }
    }

    const uint32_t filename_len =
        static_cast<uint32_t>(std::strlen(filename_for_display));

    FILE *file = fopen(host_filename, "rb");
    if (!file)
    {
      std::cerr << "[DoSdUpload] Can't open file '" << host_filename << "'"
                << std::endl;
      return 0;
    }

    // Get File Size
    fseek(file, 0, SEEK_END);
    const long file_size_long = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size_long < 0)
    {
      std::cerr << "[DoSdUpload] Failed to determine file size." << std::endl;
      fclose(file);
      return 0;
    }

    const uint32_t file_size = static_cast<uint32_t>(file_size_long);

    // 1. Send Command (0x10)
    unsigned char cmd = 0x10;
    if (!write_all(&cmd, 1, "Command"))
    {
      fclose(file);
      return 0;
    }

    // 2. Send display filename length (Big Endian)
    const unsigned char filename_len_buf[4] = {
        static_cast<unsigned char>(filename_len >> 24),
        static_cast<unsigned char>(filename_len >> 16),
        static_cast<unsigned char>(filename_len >> 8),
        static_cast<unsigned char>(filename_len)};
    if (!write_all(filename_len_buf, 4, "Filename length"))
    {
      fclose(file);
      return 0;
    }

    // 3. Send display filename bytes
    if (filename_len != 0 &&
        !write_all(reinterpret_cast<const unsigned char *>(filename_for_display),
                   filename_len, "Filename"))
    {
      fclose(file);
      return 0;
    }

    // 4. Send SD start sector (Big Endian)
    const unsigned char sector_buf[4] = {
        static_cast<unsigned char>(start_sector >> 24),
        static_cast<unsigned char>(start_sector >> 16),
        static_cast<unsigned char>(start_sector >> 8),
        static_cast<unsigned char>(start_sector)};
    if (!write_all(sector_buf, 4, "Start sector"))
    {
      fclose(file);
      return 0;
    }

    // 5. Send File Size (Big Endian)
    const unsigned char size_buf[4] = {
        static_cast<unsigned char>(file_size >> 24),
        static_cast<unsigned char>(file_size >> 16),
        static_cast<unsigned char>(file_size >> 8),
        static_cast<unsigned char>(file_size)};
    if (!write_all(size_buf, 4, "File size"))
    {
      fclose(file);
      return 0;
    }

    // 6. Send File Data & Calculate CRC
    unsigned char buffer[4096];
    crc8::crc_t checksum = 0;
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
      if (!write_all(buffer, bytes_read, "File data"))
      {
        fclose(file);
        return 0;
      }
      checksum = crc8::crc_update(checksum, buffer, bytes_read);
    }

    if (ferror(file))
    {
      std::cerr << "[DoSdUpload] File read error." << std::endl;
      fclose(file);
      return 0;
    }

    fclose(file);

    // 7. Send Checksum
    if (!write_all(&checksum, 1, "Checksum"))
    {
      return 0;
    }

    // 8. Read Result
    unsigned char result;
    if (!read_one(&result))
    {
      return 0;
    }

    return (result == 0x00) ? 1 : 0;
  }

} // namespace xfer
