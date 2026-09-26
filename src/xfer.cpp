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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

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
    /**
     * @brief Custom deleter for std::unique_ptr managing FILE pointers.
     * 
     * Ensures clean closing of file descriptors when unique_ptr goes out of scope,
     * avoiding ignored calling convention attributes on fclose when using decltype.
     */
    struct FileDeleter {
        /**
         * @brief Closes the file descriptor.
         * @param f File pointer to close.
         */
        void operator()(FILE* f) const {
            if (f) {
                fclose(f);
            }
        }
    };

    constexpr uint8_t REMOTE_IO_MAGIC[4] = {'S', 'R', 'L', '1'};
    constexpr uint32_t SDC_BLOCK_SIZE = 512;

    enum class RemoteIoCommand : uint8_t
    {
      LIST = 1,
      REMOVE = 2,
      CRC = 3,
      UPLOAD = 4,
      MKDIR = 5,
      RMDIR = 6,
      RENAME = 7,
      DOWNLOAD = 8
    };

    enum class RemoteIoStatus : uint8_t
    {
      OK = 0,
      ERR = 1,
      UNSUPPORTED = 2,
      BAD_REQUEST = 3
    };

    struct RemoteIoReply
    {
      RemoteIoStatus status = RemoteIoStatus::ERR;
      std::string payload;
    };

    struct SdUploadTarget
    {
      uint32_t start_sector = 0;
      uint32_t sector_count = 0;
      bool has_range = false;
    };

    bool ParseSdUploadTarget(const char *saturn_sd_path, SdUploadTarget &target)
    {
      if (saturn_sd_path == nullptr)
      {
        std::cerr << "[DoSdUpload] Missing SD target path." << std::endl;
        return false;
      }

      const std::string path(saturn_sd_path);
      const std::string prefix = "sdraw:";
      if (path.rfind(prefix, 0) == 0)
      {
        const std::string remainder = path.substr(prefix.size());
        const std::size_t firstColon = remainder.find(':');
        if (firstColon == std::string::npos || remainder.find(':', firstColon + 1) != std::string::npos)
        {
          std::cerr << "[DoSdUpload] Invalid SD range: '" << saturn_sd_path
                    << "'. Expected sdraw:<start>:<count>." << std::endl;
          return false;
        }

        const std::string startText = remainder.substr(0, firstColon);
        const std::string countText = remainder.substr(firstColon + 1);
        if (startText.empty() || countText.empty())
        {
          std::cerr << "[DoSdUpload] Invalid SD range: '" << saturn_sd_path
                    << "'. Expected sdraw:<start>:<count>." << std::endl;
          return false;
        }

        char *end = nullptr;
        const unsigned long parsedStart = std::strtoul(startText.c_str(), &end, 0);
        if (*end != '\0' || parsedStart > 0xFFFFFFFFUL)
        {
          std::cerr << "[DoSdUpload] Invalid SD start sector: '" << startText
                    << "'." << std::endl;
          return false;
        }

        end = nullptr;
        const unsigned long parsedCount = std::strtoul(countText.c_str(), &end, 0);
        if (*end != '\0' || parsedCount == 0 || parsedCount > 0xFFFFFFFFUL)
        {
          std::cerr << "[DoSdUpload] Invalid SD sector count: '" << countText
                    << "'." << std::endl;
          return false;
        }

        target.start_sector = static_cast<uint32_t>(parsedStart);
        target.sector_count = static_cast<uint32_t>(parsedCount);
        target.has_range = true;
        return true;
      }

      char *end = nullptr;
      const unsigned long parsed = std::strtoul(saturn_sd_path, &end, 0);
      if (saturn_sd_path == end || (end != nullptr && *end != '\0') ||
          parsed > 0xFFFFFFFFUL)
      {
        std::cerr << "[DoSdUpload] Invalid SD target: '" << saturn_sd_path
                  << "'. Expected sdraw:<start>:<count> or a numeric sector index."
                  << std::endl;
        return false;
      }

      target.start_sector = static_cast<uint32_t>(parsed);
      target.sector_count = 0;
      target.has_range = false;
      return true;
    }

    bool WriteAllToDevice(const uint8_t *data, std::size_t size)
    {
      std::size_t written = 0;
      while (written < size && !ftdi::g_interrupt_flag)
      {
        int rc = ftdi_write_data(&ftdi::g_Device,
                                 const_cast<unsigned char *>(data + written),
                                 static_cast<int>(size - written));
        if (rc < 0)
        {
          std::cerr << "[RemoteIO] Write error: "
                    << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
          return false;
        }
        if (rc == 0)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }
        written += static_cast<std::size_t>(rc);
      }
      return written == size;
    }

    bool ReadExactFromDevice(uint8_t *data, std::size_t size,
                             int max_idle_cycles = 5000)
    {
      std::size_t read = 0;
      int idleCycles = 0;

      while (read < size && !ftdi::g_interrupt_flag)
      {
        int rc = ftdi_read_data(&ftdi::g_Device,
                                reinterpret_cast<unsigned char *>(data + read),
                                static_cast<int>(size - read));
        if (rc < 0)
        {
          std::cerr << "[RemoteIO] Read error: "
                    << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
          return false;
        }
        if (rc == 0)
        {
          if (++idleCycles >= max_idle_cycles)
          {
            std::cerr << "[RemoteIO] Timeout waiting for device reply." << std::endl;
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }
        idleCycles = 0;
        read += static_cast<std::size_t>(rc);
      }
      return read == size;
    }

    bool SendRemoteIoCommand(RemoteIoCommand command, const char *argument)
    {
      if (argument == nullptr)
      {
        std::cerr << "[RemoteIO] Missing command argument." << std::endl;
        return false;
      }

      const std::size_t payloadLen = std::strlen(argument);
      if (payloadLen > 0xFFFFU)
      {
        std::cerr << "[RemoteIO] Path/file argument too long." << std::endl;
        return false;
      }

      uint8_t header[7] = {
          REMOTE_IO_MAGIC[0],
          REMOTE_IO_MAGIC[1],
          REMOTE_IO_MAGIC[2],
          REMOTE_IO_MAGIC[3],
          static_cast<uint8_t>(command),
          static_cast<uint8_t>((payloadLen >> 8) & 0xFFU),
          static_cast<uint8_t>(payloadLen & 0xFFU)};

      if (!WriteAllToDevice(header, sizeof(header)))
      {
        return false;
      }

      if (payloadLen > 0 &&
          !WriteAllToDevice(reinterpret_cast<const uint8_t *>(argument), payloadLen))
      {
        return false;
      }

      return true;
    }

    // Returns: 1 = reply received, 0 = timed out (no more data), -1 = protocol error
    int TryReadRemoteIoReply(RemoteIoReply &reply, int header_idle_cycles)
    {
      uint8_t header[7] = {};
      // Use the short timeout only for the header; if nothing arrives, it's end-of-list.
      if (!ReadExactFromDevice(header, sizeof(header), header_idle_cycles))
      {
        // Distinguish timeout (idle cycles exhausted) from read error by checking
        // whether we received any bytes at all.  ReadExactFromDevice already printed
        // a message for actual errors; for a clean timeout we just return 0.
        return 0;
      }

      if (std::memcmp(header, REMOTE_IO_MAGIC, sizeof(REMOTE_IO_MAGIC)) != 0)
      {
        std::cerr << "[RemoteIO] Invalid response magic from device." << std::endl;
        return -1;
      }

      reply.status = static_cast<RemoteIoStatus>(header[4]);
      const std::size_t payloadLen =
          (static_cast<std::size_t>(header[5]) << 8) |
          static_cast<std::size_t>(header[6]);

      reply.payload.clear();
      reply.payload.resize(payloadLen);
      if (payloadLen > 0 &&
          !ReadExactFromDevice(reinterpret_cast<uint8_t *>(&reply.payload[0]),
                               payloadLen))
      {
        return -1;
      }
      return 1;
    }

    bool ReadRemoteIoReply(RemoteIoReply &reply)
    {
      return TryReadRemoteIoReply(reply, 5000) == 1;
    }

    int ExecuteRemoteIoCommand(RemoteIoCommand command, const char *argument,
                               const char *label)
    {
      if (!SendRemoteIoCommand(command, argument))
      {
        return 0;
      }

      RemoteIoReply reply;
      if (!ReadRemoteIoReply(reply))
      {
        return 0;
      }

      if (!reply.payload.empty())
      {
        std::cout << reply.payload;
        if (reply.payload.back() != '\n')
        {
          std::cout << std::endl;
        }
      }

      if (reply.status == RemoteIoStatus::OK)
      {
        return 1;
      }

      std::cerr << "[" << label << "] Device returned status "
                << static_cast<int>(reply.status) << std::endl;
      return 0;
    }

    /**
     * @brief Send a Remote I/O rename (move) command packet to the device.
     * @details Packs the old and new path payloads separated by a null byte and sends them to the cartridge.
     * @param old_path The current file/directory path on the Saturn.
     * @param new_path The new file/directory target path on the Saturn.
     * @return true on success, false on write or length failure.
     */
    bool SendRemoteIoRenameCommand(const char *old_path, const char *new_path)
    {
      if (old_path == nullptr || new_path == nullptr)
      {
        std::cerr << "[RemoteIO] Missing command argument." << std::endl;
        return false;
      }

      const std::size_t oldLen = std::strlen(old_path);
      const std::size_t newLen = std::strlen(new_path);
      const std::size_t payloadLen = oldLen + 1 + newLen;
      if (payloadLen > 0xFFFFU)
      {
        std::cerr << "[RemoteIO] Path/file argument too long." << std::endl;
        return false;
      }

      uint8_t header[7] = {
          REMOTE_IO_MAGIC[0],
          REMOTE_IO_MAGIC[1],
          REMOTE_IO_MAGIC[2],
          REMOTE_IO_MAGIC[3],
          static_cast<uint8_t>(RemoteIoCommand::RENAME),
          static_cast<uint8_t>((payloadLen >> 8) & 0xFFU),
          static_cast<uint8_t>(payloadLen & 0xFFU)};

      if (!WriteAllToDevice(header, sizeof(header)))
      {
        return false;
      }

      if (!WriteAllToDevice(reinterpret_cast<const uint8_t *>(old_path), oldLen + 1))
      {
        return false;
      }

      if (newLen > 0 &&
          !WriteAllToDevice(reinterpret_cast<const uint8_t *>(new_path), newLen))
      {
        return false;
      }

      return true;
    }

    /**
     * @brief Send rename command and read/process the status response from the cartridge.
     * @param old_path The source path on the Saturn.
     * @param new_path The destination path on the Saturn.
     * @param label A label string for log messages (e.g. "DoRename").
     * @return 1 on success, 0 on failure.
     */
    int ExecuteRemoteIoRenameCommand(const char *old_path, const char *new_path,
                                     const char *label)

    {
      if (!SendRemoteIoRenameCommand(old_path, new_path))
      {
        return 0;
      }

      RemoteIoReply reply;
      if (!ReadRemoteIoReply(reply))
      {
        return 0;
      }

      if (!reply.payload.empty())
      {
        std::cout << reply.payload;
        if (reply.payload.back() != '\n')
        {
          std::cout << std::endl;
        }
      }

      if (reply.status == RemoteIoStatus::OK)
      {
        return 1;
      }

      std::cerr << "[" << label << "] Device returned status "
                << static_cast<int>(reply.status) << std::endl;
      return 0;
    }

    template <typename WriteFunc>
    bool StreamAndCrc(FILE* f, crc8::crc_t& checksum_out, WriteFunc write_func)
    {
      // Use a larger buffer (e.g., 64KB) to optimize file I/O and match USB chunk capabilities
      std::vector<unsigned char> buffer(xfer::USB_READPACKET_SIZE);
      crc8::crc_t checksum = 0;
      size_t bytes_read;
      while ((bytes_read = fread(buffer.data(), 1, buffer.size(), f)) > 0)
      {
        if (!write_func(buffer.data(), bytes_read))
        {
          return false;
        }
        checksum = crc8::crc_update(checksum, buffer.data(), bytes_read);
      }
      if (ferror(f))
      {
        std::cerr << "[DoSdUpload] File read error." << std::endl;
        return false;
      }
      checksum_out = checksum;
      return true;
    }
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
   * @param start Start time of transfer.
   * @param end End time of transfer.
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
    cdbg << "Transfer time: " << std::fixed << std::setprecision(3)
              << seconds << " s" << std::endl;
    // Step 5: Output transfer speed
    cdbg << "Transfer speed: " << std::fixed << std::setprecision(2)
              << speed_kb_s << " KB/s" << std::endl;
  }

  /**
   * @copydoc xfer::DoBiosDump
   */
  int DoBiosDump(const char *filename)
  {
    // Step 1: Log the start of the BIOS dump process
    cdbg << "[DoBiosDump] Starting BIOS dump to file: " << filename
              << std::endl;
    // Step 2: Call DoDownload to perform the dump from the BIOS address and size
    return DoDownload(filename, saturn::bios_address, saturn::bios_size);
  }

  /**
   * @copydoc xfer::DoList
   */
  int DoList(const char *path)
  {
    if (!SendRemoteIoCommand(RemoteIoCommand::LIST, path))
    {
      return 0;
    }

    // First packet uses the full timeout so the device has time to respond.
    RemoteIoReply reply;
    if (!ReadRemoteIoReply(reply))
    {
      return 0;
    }

    for (;;)
    {
      if (!reply.payload.empty())
      {
        std::cout << reply.payload;
        if (reply.payload.back() != '\n')
        {
          std::cout << '\n';
        }
      }

      if (reply.status != RemoteIoStatus::OK)
      {
        std::cerr << "[DoList] Device returned status "
                  << static_cast<int>(reply.status) << std::endl;
        return 0;
      }

      if (reply.payload.empty())
      {
        // Empty payload + OK = explicit end-of-listing sentinel from device.
        return 1;
      }

      // Try to read the next packet.  Use a short inter-packet timeout (200 ms)
      // so that a single-packet response exits promptly instead of hanging for
      // the full 5-second idle timeout.
      const int rc = TryReadRemoteIoReply(reply, 200);
      if (rc == 0)
      {
        // Timeout: no more packets — listing is complete.
        return 1;
      }
      if (rc < 0)
      {
        return 0;
      }
    }
  }

  /**
   * @copydoc xfer::DoListStr
   */
  int DoListStr(const char *path, std::string &out_listing)
  {
    if (!SendRemoteIoCommand(RemoteIoCommand::LIST, path))
    {
      return 0;
    }

    // First packet uses the full timeout so the device has time to respond.
    RemoteIoReply reply;
    if (!ReadRemoteIoReply(reply))
    {
      return 0;
    }

    for (;;)
    {
      if (!reply.payload.empty())
      {
        out_listing += reply.payload;
        if (out_listing.back() != '\n')
        {
          out_listing += '\n';
        }
      }

      if (reply.status != RemoteIoStatus::OK)
      {
        std::cerr << "[DoListStr] Device returned status "
                  << static_cast<int>(reply.status) << std::endl;
        return 0;
      }

      if (reply.payload.empty())
      {
        // Empty payload + OK = explicit end-of-listing sentinel from device.
        return 1;
      }

      // Try to read the next packet.  Use a short inter-packet timeout (200 ms)
      // so that a single-packet response exits promptly instead of hanging for
      // the full 5-second idle timeout.
      const int rc = TryReadRemoteIoReply(reply, 200);
      if (rc == 0)
      {
        // Timeout: no more packets — listing is complete.
        return 1;
      }
      if (rc < 0)
      {
        return 0;
      }
    }
  }

  /**
   * @copydoc xfer::DoRemove
   */
  int DoRemove(const char *path)
  {
    return ExecuteRemoteIoCommand(RemoteIoCommand::REMOVE, path, "DoRemove");
  }

  /**
   * @copydoc xfer::DoRename
   */
  int DoRename(const char *old_path, const char *new_path)
  {
    return ExecuteRemoteIoRenameCommand(old_path, new_path, "DoRename");
  }

  /**
   * @copydoc xfer::DoMkdir
   */
  int DoMkdir(const char *path)
  {
    return ExecuteRemoteIoCommand(RemoteIoCommand::MKDIR, path, "DoMkdir");
  }

  /**
   * @copydoc xfer::DoRmdir
   */
  int DoRmdir(const char *path)
  {
    return ExecuteRemoteIoCommand(RemoteIoCommand::RMDIR, path, "DoRmdir");
  }

  /**
   * @copydoc xfer::DoCrc
   */
  int DoCrc(const char *filename)
  {
    return ExecuteRemoteIoCommand(RemoteIoCommand::CRC, filename, "DoCrc");
  }

  /**
   * @copydoc xfer::DoDownload
   */
  int DoDownload(const char *filename, uint32_t address, std::size_t size)
  {
    cdbg << "[DoDownload] Starting download: file='" << filename
         << "', address=0x" << std::hex << address << ", size=" << std::dec << size << std::endl;

    std::unique_ptr<FILE, FileDeleter> file(fopen(filename, "wb"));
    if (!file)
    {
      std::cerr << "[DoDownload] Error creating output file" << std::endl;
      return 0;
    }

    int status = -1;
    crc8::crc_t readChecksum = 0, calcChecksum = 0;
    
    auto before = std::chrono::steady_clock::now();

    cdbg << "[DoDownload] Sending download command..." << std::endl;
    status = xfer::SendCommandWithAddressAndLength(USBDC_FUNC_DOWNLOAD, address, size);
    if (status < 0)
    {
      std::cerr << "[DoDownload] Send download command error: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }

    cdbg << "[DoDownload] Reading data from device..." << std::endl;
    
    // Allocate a buffer matching the configured FTDI read chunk size to maximize USB throughput
    std::vector<unsigned char> buffer(xfer::USB_READPACKET_SIZE);
    std::size_t received = 0;
    while (size - received > 0 && !ftdi::g_interrupt_flag)
    {
      size_t to_read = std::min<size_t>(buffer.size(), size - received);
      status = ftdi_read_data(&ftdi::g_Device, buffer.data(), to_read);
      if (status < 0)
      {
        std::cerr << "[DoDownload] Read data error: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
      if (status > 0)
      {
        if (fwrite(buffer.data(), 1, status, file.get()) != static_cast<size_t>(status))
        {
          std::cerr << "[DoDownload] File write error" << std::endl;
          return 0;
        }
        calcChecksum = crc8::crc_update(calcChecksum, buffer.data(), status);
        received += status;
        cdbg << "[DoDownload] Received " << received << "/" << size << " bytes..." << std::endl;
      }
    }

    cdbg << "[DoDownload] Waiting for checksum byte..." << std::endl;
    do
    {
      status = ftdi_read_data(&ftdi::g_Device, reinterpret_cast<unsigned char *>(&readChecksum), 1);
      if (status < 0)
      {
        std::cerr << "[DoDownload] Read data error: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
    } while (status == 0 && !ftdi::g_interrupt_flag);

    auto after = std::chrono::steady_clock::now();
    cdbg << "[DoDownload] Data received. Calculating performance..." << std::endl;
    xfer::ReportPerformance(before, after, size);

    if (readChecksum != calcChecksum)
    {
      std::cerr << "[DoDownload] Checksum error (" << std::hex
                << static_cast<int>(calcChecksum) << ", should be "
                << static_cast<int>(readChecksum) << ")" << std::endl;
      return 0;
    }

    cdbg << "[DoDownload] Download complete." << std::endl;
    return 1;
  }

  /**
   * @copydoc xfer::DoUpload
   */
  int DoUpload(const char *filename, uint32_t address, const bool execute)
  {
    std::string functnName = execute ? "DoUploadExecute" : "DoUpload";
    cdbg << "[" << functnName << "] Starting upload: file='" << filename
         << "', address=0x" << std::hex << address << std::dec << std::endl;

    std::error_code ec;
    uintmax_t file_size_raw = std::filesystem::file_size(filename, ec);
    if (ec || file_size_raw == 0)
    {
      std::cerr << "[" << functnName << "] File is empty or error reading size: " << ec.message() << std::endl;
      return 0;
    }
    
    if (file_size_raw > std::numeric_limits<uint32_t>::max())
    {
      std::cerr << "[" << functnName << "] File is too large for 32-bit address space." << std::endl;
      return 0;
    }
    const uint32_t size = static_cast<uint32_t>(file_size_raw);

    std::unique_ptr<FILE, FileDeleter> file(fopen(filename, "rb"));
    if (!file)
    {
      std::cerr << "[" << functnName << "] Can't open the file '" << filename << "'" << std::endl;
      return 0;
    }

    auto before = std::chrono::steady_clock::now();

    if (execute)
    {
      cdbg << "[" << functnName << "] Uploading and executing at address 0x" << std::hex << address << std::dec << std::endl;
      SendBuf[0] = USBDC_FUNC_EXEC_EXT;
    }
    else
    {
      cdbg << "[" << functnName << "] Uploading to address 0x" << std::hex << address << std::dec << std::endl;
      SendBuf[0] = USBDC_FUNC_UPLOAD;
    }

    int status = xfer::SendCommandWithAddressAndLength(SendBuf[0], address, size);
    if (status < 0)
    {
      std::cerr << "[" << functnName << "] Send upload command error: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }

    crc8::crc_t checksum = 0;
    if (!StreamAndCrc(file.get(), checksum, [&](const unsigned char* data, size_t len) {
        size_t sent = 0;
        while (sent < len && !ftdi::g_interrupt_flag)
        {
          int write_status = ftdi_write_data(&ftdi::g_Device, const_cast<unsigned char*>(data + sent), len - sent);
          if (write_status < 0)
          {
            std::cerr << "[" << functnName << "] Send data error: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
            return false;
          }
          if (write_status == 0) continue;
          sent += write_status;
          cdbg << "[" << functnName << "] Sent chunk..." << std::endl;
        }
        return true;
    }))
    {
        return 0;
    }

    SendBuf[0] = static_cast<unsigned char>(checksum);
    status = ftdi_write_data(&ftdi::g_Device, SendBuf, 1);
    if (status < 0)
    {
      std::cerr << "[" << functnName << "] Send checksum error: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }

    do
    {
      status = ftdi_read_data(&ftdi::g_Device, RecvBuf, 1);
      if (status < 0)
      {
        std::cerr << "[" << functnName << "] Read upload result failed: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
    } while (status == 0 && !ftdi::g_interrupt_flag);

    if (RecvBuf[0] != 0)
    {
      std::cerr << "[" << functnName << "] Device reported upload error." << std::endl;
      return 0;
    }

    auto after = std::chrono::steady_clock::now();
    xfer::ReportPerformance(before, after, size);
    cdbg << "[" << functnName << "] Upload complete." << std::endl;
    return 1;
  }

  /**
   * @copydoc xfer::DoRun
   */
  int DoRun(uint32_t address)
  {
    cdbg << "[DoRun] Executing at address 0x" << std::hex << address
              << std::dec << std::endl;

    // Issue the standard execution command followed by the 4-byte big-endian address
    SendBuf[0] = USBDC_FUNC_EXEC;
    SendBuf[1] = static_cast<unsigned char>(address >> 24);
    SendBuf[2] = static_cast<unsigned char>(address >> 16);
    SendBuf[3] = static_cast<unsigned char>(address >> 8);
    SendBuf[4] = static_cast<unsigned char>(address);

    int status = ftdi_write_data(&ftdi::g_Device, SendBuf, 5);
    if (status < 0)
    {
      std::cerr << "[DoRun] Send execute error: "
                << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
      return 0;
    }
    
    cdbg << "[DoRun] Execute command sent successfully." << std::endl;
    return 1;
  }

  /**
   * @copydoc xfer::DoExecute
   */
  int DoExecute(const char *filename, uint32_t address)
  {
    cdbg << "[DoExecute] Uploading and executing: file='" << filename
              << "', address=0x" << std::hex << address << std::dec << std::endl;

    // Delegate to DoUpload with the execute flag enabled, and return its status
    if (DoUpload(filename, address, true))
    {
      cdbg << "[DoExecute] Upload successful. Executing..." << std::endl;
      return 1;
    }
    else
    {
      std::cerr << "[DoExecute] Upload failed. Execution aborted." << std::endl;
      return 0;
    }
  }

  /**
   * @copydoc xfer::DoSdUpload
   */
  int DoSdUpload(const char *host_filename, const char *saturn_sd_path)
  {
    std::error_code ec;
    uintmax_t file_size_raw = std::filesystem::file_size(host_filename, ec);
    if (ec)
    {
      std::cerr << "[DoSdUpload] Failed to determine file size: " << ec.message() << std::endl;
      return 0;
    }
    if (file_size_raw > std::numeric_limits<uint32_t>::max())
    {
      std::cerr << "[DoSdUpload] File is too large." << std::endl;
      return 0;
    }
    const uint32_t file_size = static_cast<uint32_t>(file_size_raw);

    std::unique_ptr<FILE, FileDeleter> file(fopen(host_filename, "rb"));
    if (!file)
    {
      std::cerr << "[DoSdUpload] Can't open file '" << host_filename << "'" << std::endl;
      return 0;
    }

    if (saturn_sd_path != nullptr && saturn_sd_path[0] == '/')
    {
      // Warn if target path violates 8.3 filename limits (Saturn side limitation)
      std::string path_str(saturn_sd_path);
      size_t start = 0;
      while (start < path_str.size())
      {
        size_t end = path_str.find('/', start);
        if (end == std::string::npos)
        {
          end = path_str.size();
        }
        std::string component = path_str.substr(start, end - start);
        if (!component.empty())
        {
          size_t dot = component.rfind('.');
          std::string name = (dot == std::string::npos) ? component : component.substr(0, dot);
          std::string ext = (dot == std::string::npos) ? "" : component.substr(dot + 1);
          size_t dot_count = std::count(component.begin(), component.end(), '.');
          if (name.length() > 8 || ext.length() > 3 || dot_count > 1)
          {
            std::cerr << "[DoSdUpload] Warning: Target path component \"" << component 
                      << "\" violates strict 8.3 conventions (max 8 chars name, 3 chars extension). "
                      << "Saturn device is likely to reject this upload." << std::endl;
            break;
          }
        }
        start = end + 1;
      }

      if (!SendRemoteIoCommand(RemoteIoCommand::UPLOAD, saturn_sd_path))
      {
        return 0;
      }

      RemoteIoReply reply;
      if (!ReadRemoteIoReply(reply))
      {
        return 0;
      }

      if (reply.status != RemoteIoStatus::OK)
      {
        std::cerr << "[DoSdUpload] Device rejected upload: "
                  << static_cast<int>(reply.status) << std::endl;
        return 0;
      }

      const unsigned char size_buf[4] = {
          static_cast<unsigned char>(file_size >> 24),
          static_cast<unsigned char>(file_size >> 16),
          static_cast<unsigned char>(file_size >> 8),
          static_cast<unsigned char>(file_size)};
      if (!WriteAllToDevice(size_buf, 4))
      {
        return 0;
      }

      crc8::crc_t checksum = 0;
      if (!StreamAndCrc(file.get(), checksum, [](const unsigned char* data, size_t len) {
            return WriteAllToDevice(data, len);
          }))
      {
        return 0;
      }

      if (!WriteAllToDevice(&checksum, 1))
      {
        return 0;
      }

      unsigned char result;
      if (!ReadExactFromDevice(&result, 1))
      {
        return 0;
      }

      if (result != 0x00)
      {
        std::cerr << "[DoSdUpload] Device reported CRC error." << std::endl;
        return 0;
      }

      return 1;
    }

    auto write_all = [](const unsigned char *data, size_t length,
                        const char *stage) -> bool
    {
      size_t sent = 0;
      while (sent < length && !ftdi::g_interrupt_flag)
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
      } while (status == 0 && !ftdi::g_interrupt_flag);

      return true;
    };

    SdUploadTarget target;
    if (!ParseSdUploadTarget(saturn_sd_path, target))
    {
      return 0;
    }

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

    if (target.has_range)
    {
      const uint64_t max_bytes = static_cast<uint64_t>(target.sector_count) *
                                 static_cast<uint64_t>(SDC_BLOCK_SIZE);
      if (static_cast<uint64_t>(file_size) > max_bytes)
      {
        std::cerr << "[DoSdUpload] File is too large for target range '"
                  << saturn_sd_path << "' (" << file_size << " bytes > "
                  << max_bytes << " bytes)." << std::endl;
        return 0;
      }
    }

    // 1. Send Command (0x10)
    unsigned char cmd = 0x10;
    if (!write_all(&cmd, 1, "Command"))
    {
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
      return 0;
    }

    // 3. Send display filename bytes
    if (filename_len != 0 &&
        !write_all(reinterpret_cast<const unsigned char *>(filename_for_display),
                   filename_len, "Filename"))
    {
      return 0;
    }

    // 4. Send SD start sector (Big Endian)
    const unsigned char sector_buf[4] = {
      static_cast<unsigned char>(target.start_sector >> 24),
      static_cast<unsigned char>(target.start_sector >> 16),
      static_cast<unsigned char>(target.start_sector >> 8),
      static_cast<unsigned char>(target.start_sector)};
    if (!write_all(sector_buf, 4, "Start sector"))
    {
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
      return 0;
    }

    // 6. Send File Data & Calculate CRC
    crc8::crc_t checksum = 0;
    if (!StreamAndCrc(file.get(), checksum, [&](const unsigned char* data, size_t len) {
          return write_all(data, len, "File data");
        }))
    {
      return 0;
    }

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

  /**
   * @copydoc xfer::DoSdDownload
   * @details Downloads files from the Sega Saturn SD card over HostIO.
   * Protocol sequence:
   * 1. Host sends RemoteIoCommand::DOWNLOAD with target path payload.
   * 2. Saturn replies with RemoteIoStatus and a 4-byte big-endian file size payload.
   * 3. Host reads file data sequentially, accumulating CRC-8 checksum.
   * 4. Saturn sends 1-byte final CRC-8 checksum for verification.
   */
  int DoSdDownload(const char *saturn_sd_path, const char *host_filename)
  {
    // Step 1: Validate parameters and attempt to open destination file on the host machine.
    if (saturn_sd_path == nullptr || host_filename == nullptr)
    {
      std::cerr << "[DoSdDownload] Missing path/filename argument." << std::endl;
      return 0;
    }

    std::unique_ptr<FILE, FileDeleter> file(fopen(host_filename, "wb"));
    if (!file)
    {
      std::cerr << "[DoSdDownload] Can't open file '" << host_filename << "' for writing" << std::endl;
      return 0;
    }

    // Step 2: Send DOWNLOAD command packet containing target SD card path to the console.
    if (!SendRemoteIoCommand(RemoteIoCommand::DOWNLOAD, saturn_sd_path))
    {
      return 0;
    }

    // Step 3: Wait and parse the Remote I/O response status from the console.
    RemoteIoReply reply;
    if (!ReadRemoteIoReply(reply))
    {
      std::cerr << "[DoSdDownload] Failed to read reply." << std::endl;
      return 0;
    }

    if (reply.status != RemoteIoStatus::OK)
    {
      std::cerr << "[DoSdDownload] Device rejected download: "
                << static_cast<int>(reply.status) << std::endl;
      return 0;
    }

    // Step 4: Extract the 32-bit big-endian file size from the reply payload.
    if (reply.payload.size() < 4)
    {
      std::cerr << "[DoSdDownload] Invalid response payload size." << std::endl;
      return 0;
    }

    const uint32_t file_size =
        (static_cast<uint32_t>(static_cast<uint8_t>(reply.payload[0])) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(reply.payload[1])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(reply.payload[2])) << 8) |
        static_cast<uint32_t>(static_cast<uint8_t>(reply.payload[3]));

    // Step 5: Read the file stream chunk-by-chunk from FTDI and calculate local checksum.
    uint8_t buffer[4096];
    uint32_t received = 0;
    crc8::crc_t checksum = 0;
    while (received < file_size)
    {
      uint32_t chunk = file_size - received;
      if (chunk > sizeof(buffer))
      {
        chunk = sizeof(buffer);
      }

      if (!ReadExactFromDevice(buffer, chunk))
      {
        std::cerr << "[DoSdDownload] Read data failed." << std::endl;
        return 0;
      }

      checksum = crc8::crc_update(checksum, buffer, chunk);
      if (fwrite(buffer, 1, chunk, file.get()) != chunk)
      {
        std::cerr << "[DoSdDownload] Write file error." << std::endl;
        return 0;
      }

      received += chunk;
    }

    // Step 6: Retrieve and verify the trailing CRC-8 checksum sent by the Saturn.
    uint8_t device_checksum = 0;
    if (!ReadExactFromDevice(&device_checksum, 1))
    {
      std::cerr << "[DoSdDownload] Read checksum failed." << std::endl;
      return 0;
    }

    if (device_checksum != checksum)
    {
      std::cerr << "[DoSdDownload] Checksum mismatch." << std::endl;
      return 0;
    }

    return 1;
  }

  struct SdSyncEntry {
    std::string rel_path;
    bool is_dir = false;
    uint32_t size = 0;
    bool too_large = false; ///< Local file exceeds the FAT32 4 GB limit.
    std::string problem;    ///< Non-empty: local entry that must never be synced (reason).
    std::time_t mtime = -1; ///< Modification time (local time zone), -1 if unknown.
  };

  /// Map keyed by lowercase relative path: FAT names are case-insensitive.
  using SdSyncMap = std::map<std::string, SdSyncEntry>;

  static std::string SyncKey(const std::string &rel_path)
  {
    std::string key = rel_path;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
  }

  static std::string SyncParentKey(const std::string &key)
  {
    size_t slash = key.find_last_of('/');
    return (slash == std::string::npos) ? std::string() : key.substr(0, slash);
  }

  static std::string SyncLeafName(const std::string &rel_path)
  {
    size_t slash = rel_path.find_last_of('/');
    return (slash == std::string::npos) ? rel_path : rel_path.substr(slash + 1);
  }

  /// Parse a Saturn listing timestamp ("YYYY-MM-DD HH:MM[:SS]"), interpreted as local time.
  static std::time_t ParseSaturnTime(const std::string &date, const std::string &time_str)
  {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3 ||
        sscanf(time_str.c_str(), "%d:%d:%d", &hour, &minute, &second) < 2)
    {
      return -1;
    }
    std::tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
  }

  static std::time_t ToTimeT(std::filesystem::file_time_type ftime)
  {
    using namespace std::chrono;
    auto sys = time_point_cast<system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(sys);
  }

  static std::string CombineSaturnPath(const std::string &base, const std::string &rel)
  {
    if (rel.empty()) return base;
    if (base == "/") return "/" + rel;
    if (!base.empty() && base.back() == '/') return base + rel;
    return base + "/" + rel;
  }

  /// List one Saturn directory. Returns false if the listing could not be read.
  static bool ListSaturnDirForSync(const std::string &saturn_dir, std::vector<SdSyncEntry> &entries)
  {
    std::string listing;
    std::string path_arg = "-l " + saturn_dir;
    if (xfer::DoListStr(path_arg.c_str(), listing) != 1)
    {
      std::cerr << "[DoSdSync] Failed to list Saturn directory: " << saturn_dir << std::endl;
      return false;
    }

    std::istringstream iss(listing);
    std::string line;
    while (std::getline(iss, line))
    {
      size_t first = line.find_first_not_of(" \t\r\n");
      if (first == std::string::npos) continue;
      line = line.substr(first);
      if (line.size() < 4) continue;

      bool is_dir = false;
      if (line.substr(0, 3) == "[D]") is_dir = true;
      else if (line.substr(0, 3) == "[F]") is_dir = false;
      else continue;

      std::istringstream line_ss(line.substr(3));
      uint32_t size = 0;
      std::string date, time_str, name;
      if (line_ss >> size >> date >> time_str)
      {
        std::getline(line_ss, name);
        size_t name_start = name.find_first_not_of(" ");
        if (name_start != std::string::npos) name = name.substr(name_start);
        if (!name.empty() && name.back() == '\r') name.pop_back();
        if (name.empty() || name == "." || name == "..") continue;

        SdSyncEntry entry;
        entry.rel_path = name;
        entry.is_dir = is_dir;
        entry.size = size;
        entry.mtime = ParseSaturnTime(date, time_str);
        entries.push_back(entry);
      }
    }
    return true;
  }

  /// Look up a single Saturn path by listing its parent directory.
  /// Returns false on communication failure; otherwise sets @p found / @p is_dir.
  static bool LookupSaturnPath(const std::string &saturn_path, bool &found, bool &is_dir)
  {
    found = false;
    is_dir = false;
    if (saturn_path == "/")
    {
      found = true;
      is_dir = true;
      return true;
    }

    size_t last_slash = saturn_path.find_last_of('/');
    std::string parent = (last_slash == 0) ? "/" : saturn_path.substr(0, last_slash);
    std::string leaf_key = SyncKey(saturn_path.substr(last_slash + 1));

    bool parent_found = false, parent_is_dir = false;
    if (!LookupSaturnPath(parent, parent_found, parent_is_dir)) return false;
    if (!parent_found || !parent_is_dir) return true;

    std::vector<SdSyncEntry> entries;
    if (!ListSaturnDirForSync(parent, entries)) return false;
    for (const auto &e : entries)
    {
      if (SyncKey(e.rel_path) == leaf_key)
      {
        found = true;
        is_dir = e.is_dir;
        break;
      }
    }
    return true;
  }

  /// Create a Saturn directory and any missing parents (like mkdir -p).
  static bool MakeSaturnDirs(const std::string &saturn_path)
  {
    bool parent_missing = false;
    for (size_t pos = saturn_path.find('/', 1); ; pos = saturn_path.find('/', pos + 1))
    {
      const std::string prefix = saturn_path.substr(0, pos);
      if (!parent_missing)
      {
        bool found = false, is_dir = false;
        if (!LookupSaturnPath(prefix, found, is_dir))
        {
          return false;
        }
        if (found && !is_dir)
        {
          std::cerr << "[DoSdSync] Saturn path is not a directory: " << prefix << std::endl;
          return false;
        }
        parent_missing = !found;
      }
      if (parent_missing && xfer::DoMkdir(prefix.c_str()) != 1)
      {
        std::cerr << "[DoSdSync] Cannot create Saturn directory: " << prefix << std::endl;
        return false;
      }
      if (pos == std::string::npos) return true;
    }
  }

  static bool GetSaturnTreeRecursive(const std::string &saturn_base, const std::string &current_rel, SdSyncMap &out)
  {
    std::vector<SdSyncEntry> items;
    if (!ListSaturnDirForSync(CombineSaturnPath(saturn_base, current_rel), items))
    {
      return false;
    }

    for (auto &item : items)
    {
      item.rel_path = current_rel.empty() ? item.rel_path : (current_rel + "/" + item.rel_path);
      out[SyncKey(item.rel_path)] = item;
      if (item.is_dir && !GetSaturnTreeRecursive(saturn_base, item.rel_path, out))
      {
        return false;
      }
    }
    return true;
  }

  static bool GetLocalTreeRecursive(const std::filesystem::path &local_base, const std::string &current_rel, SdSyncMap &out)
  {
    namespace fs = std::filesystem;
    fs::path current_local = current_rel.empty() ? local_base : local_base / current_rel;

    std::error_code ec;
    fs::directory_iterator it(current_local, ec);
    if (ec)
    {
      std::cerr << "[DoSdSync] Cannot read local directory " << current_local << ": " << ec.message() << std::endl;
      return false;
    }

    for (; it != fs::directory_iterator(); it.increment(ec))
    {
      if (ec) break;
      const fs::directory_entry &entry = *it;
      std::string name = entry.path().filename().string();
      std::string rel = current_rel.empty() ? name : (current_rel + "/" + name);

      SdSyncEntry node;
      node.rel_path = rel;

      // Entries that cannot be synced safely stay in the tree, flagged, so that
      // a same-named Saturn entry is never written through or over them.
      fs::file_status st = entry.status(ec);
      if (ec)
      {
        const std::string reason = ec.message();
        ec.clear();
        node.problem = entry.is_symlink(ec) ? "broken symlink" : "cannot stat (" + reason + ")";
      }
      else if (entry.is_symlink(ec) && fs::is_directory(st))
      {
        node.problem = "symlinked directory";
      }
      else if (!fs::is_directory(st) && !fs::is_regular_file(st))
      {
        node.problem = "not a regular file or directory";
      }
      ec.clear();

      node.is_dir = node.problem.empty() && fs::is_directory(st);
      if (node.problem.empty() && !node.is_dir)
      {
        const uintmax_t size = entry.file_size(ec);
        if (ec)
        {
          std::cerr << "[DoSdSync] Cannot read size of " << entry.path() << ": " << ec.message() << std::endl;
          return false;
        }
        node.too_large = size > std::numeric_limits<uint32_t>::max();
        node.size = node.too_large ? 0 : static_cast<uint32_t>(size);
        auto ftime = entry.last_write_time(ec);
        node.mtime = ec ? -1 : ToTimeT(ftime);
        ec.clear();
      }

      // FAT is case-insensitive: two local names differing only in case can't both be synced.
      const std::string key = SyncKey(rel);
      auto existing = out.find(key);
      if (existing != out.end())
      {
        existing->second.problem = "name differs only in case from " + rel;
        continue;
      }
      out[key] = node;

      if (node.is_dir && !GetLocalTreeRecursive(local_base, rel, out))
      {
        return false;
      }
    }

    if (ec)
    {
      std::cerr << "[DoSdSync] Error while reading " << current_local << ": " << ec.message() << std::endl;
      return false;
    }
    return true;
  }

  /**
   * @copydoc xfer::DoSdSync
   */
  int DoSdSync(const char *local_path, const char *saturn_sd_path, int mode)
  {
    namespace fs = std::filesystem;

    if (!local_path || !saturn_sd_path)
    {
      std::cerr << "[DoSdSync] Missing local or Saturn SD path." << std::endl;
      return 0;
    }
    if (mode < 1 || mode > 3)
    {
      std::cerr << "[DoSdSync] Invalid sync mode: " << mode << std::endl;
      return 0;
    }
    const bool push = (mode == 1 || mode == 3);
    const bool pull = (mode == 2 || mode == 3);

    std::string saturn_base = saturn_sd_path;
    if (saturn_base.empty() || saturn_base[0] != '/')
    {
      saturn_base = "/" + saturn_base;
    }
    while (saturn_base.size() > 1 && saturn_base.back() == '/')
    {
      saturn_base.pop_back();
    }

    fs::path local_base(local_path);
    std::error_code ec;

    std::cout << "[DoSdSync] Synchronizing (Mode " << mode << "): '"
              << local_base.string() << "' <-> Saturn:'" << saturn_base << "'" << std::endl;

    // Validate the local side.
    const bool local_exists = fs::exists(local_base, ec);
    if (ec)
    {
      std::cerr << "[DoSdSync] Cannot access local path " << local_base << ": " << ec.message() << std::endl;
      return 0;
    }
    if (local_exists && !fs::is_directory(local_base, ec))
    {
      std::cerr << "[DoSdSync] Local path is not a directory: " << local_base << std::endl;
      return 0;
    }
    if (!local_exists && mode == 1)
    {
      std::cerr << "[DoSdSync] Local directory does not exist: " << local_base << std::endl;
      return 0;
    }

    // Validate the Saturn side.
    bool saturn_exists = false, saturn_is_dir = false;
    if (!LookupSaturnPath(saturn_base, saturn_exists, saturn_is_dir))
    {
      std::cerr << "[DoSdSync] Failed to query Saturn path: " << saturn_base << std::endl;
      return 0;
    }
    if (saturn_exists && !saturn_is_dir)
    {
      std::cerr << "[DoSdSync] Saturn path is not a directory: " << saturn_base << std::endl;
      return 0;
    }
    if (!saturn_exists && mode == 2)
    {
      std::cerr << "[DoSdSync] Saturn directory does not exist: " << saturn_base << std::endl;
      return 0;
    }

    // Collect both trees before changing anything, so a failed listing aborts cleanly.
    SdSyncMap local_map;
    if (local_exists && !GetLocalTreeRecursive(local_base, "", local_map))
    {
      std::cerr << "[DoSdSync] Aborting: could not read local tree." << std::endl;
      return 0;
    }

    SdSyncMap saturn_map;
    if (saturn_exists && !GetSaturnTreeRecursive(saturn_base, "", saturn_map))
    {
      std::cerr << "[DoSdSync] Aborting: could not read Saturn tree." << std::endl;
      return 0;
    }

    // Create the base directories.
    if (!local_exists)
    {
      fs::create_directories(local_base, ec);
      if (ec)
      {
        std::cerr << "[DoSdSync] Cannot create local directory " << local_base << ": " << ec.message() << std::endl;
        return 0;
      }
    }
    if (!saturn_exists && !MakeSaturnDirs(saturn_base))
    {
      return 0;
    }

    int success_count = 0;
    int fail_count = 0;
    int skip_count = 0;

    // Map a Saturn relative path onto the local tree, reusing the existing local
    // spelling of the parent directory (local file systems may be case-sensitive).
    auto local_rel_for = [&](const std::string &key, const std::string &saturn_rel) {
      std::string parent_key = SyncParentKey(key);
      std::string leaf = SyncLeafName(saturn_rel);
      if (parent_key.empty()) return leaf;
      auto parent = local_map.find(parent_key);
      std::string parent_rel = (parent != local_map.end()) ? parent->second.rel_path
                                                           : saturn_rel.substr(0, saturn_rel.size() - leaf.size() - 1);
      return parent_rel + "/" + leaf;
    };

    auto upload = [&](const SdSyncEntry &loc, const std::string &saturn_rel) {
      fs::path src = local_base / loc.rel_path;
      std::string dst = CombineSaturnPath(saturn_base, saturn_rel);
      std::cout << "[DoSdSync] Uploading " << loc.rel_path << " -> " << dst << std::endl;
      if (xfer::DoSdUpload(src.string().c_str(), dst.c_str()) == 1) success_count++;
      else fail_count++;
    };

    auto download = [&](const SdSyncEntry &sat, const std::string &local_rel) {
      std::string src = CombineSaturnPath(saturn_base, sat.rel_path);
      fs::path dst = local_base / local_rel;
      std::cout << "[DoSdSync] Downloading " << src << " -> " << dst.string() << std::endl;
      if (xfer::DoSdDownload(src.c_str(), dst.string().c_str()) == 1) success_count++;
      else fail_count++;
    };

    std::set<std::string> all_keys;
    for (const auto &kv : local_map) all_keys.insert(kv.first);
    for (const auto &kv : saturn_map) all_keys.insert(kv.first);

    // Directories that failed or conflicted: their contents are skipped.
    std::set<std::string> blocked;
    auto is_blocked = [&](const std::string &key) {
      for (std::string parent = SyncParentKey(key); !parent.empty(); parent = SyncParentKey(parent))
      {
        if (blocked.count(parent)) return true;
      }
      return false;
    };

    // Sorted order guarantees a parent directory is handled before its children.
    for (const auto &key : all_keys)
    {
      if (is_blocked(key)) continue;

      auto loc_it = local_map.find(key);
      auto sat_it = saturn_map.find(key);
      const bool in_local = loc_it != local_map.end();
      const bool in_saturn = sat_it != saturn_map.end();

      // FAT32 cannot hold files of 4 GB or more: never push them, and in mode 3
      // never let a same-named Saturn file overwrite them either.
      if (in_local && !loc_it->second.problem.empty())
      {
        // Local-only entries in pull mode need no action, so aren't a failure.
        if (push || in_saturn)
        {
          std::cerr << "[DoSdSync] Skipping " << loc_it->second.rel_path << ": "
                    << loc_it->second.problem << "." << std::endl;
          fail_count++;
        }
        blocked.insert(key);
        continue;
      }
      if (in_local && loc_it->second.too_large && push)
      {
        std::cerr << "[DoSdSync] Skipping " << loc_it->second.rel_path
                  << ": larger than the 4 GB FAT32 limit." << std::endl;
        fail_count++;
        continue;
      }

      if (in_local && !in_saturn)
      {
        if (!push) continue;
        const SdSyncEntry &loc = loc_it->second;
        if (loc.is_dir)
        {
          std::string dst = CombineSaturnPath(saturn_base, loc.rel_path);
          if (xfer::DoMkdir(dst.c_str()) == 1) success_count++;
          else
          {
            std::cerr << "[DoSdSync] Failed to create Saturn directory: " << dst << std::endl;
            fail_count++;
            blocked.insert(key);
          }
        }
        else
        {
          upload(loc, loc.rel_path);
        }
      }
      else if (!in_local && in_saturn)
      {
        if (!pull) continue;
        const SdSyncEntry sat = sat_it->second;
        std::string local_rel = local_rel_for(key, sat.rel_path);
        if (sat.is_dir)
        {
          fs::create_directories(local_base / local_rel, ec);
          if (ec)
          {
            std::cerr << "[DoSdSync] Failed to create local directory " << local_rel << ": " << ec.message() << std::endl;
            fail_count++;
            blocked.insert(key);
            ec.clear();
            continue;
          }
          SdSyncEntry created;
          created.rel_path = local_rel;
          created.is_dir = true;
          local_map[key] = created;
          success_count++;
        }
        else
        {
          download(sat, local_rel);
        }
      }
      else
      {
        const SdSyncEntry &loc = loc_it->second;
        const SdSyncEntry &sat = sat_it->second;
        if (loc.is_dir != sat.is_dir)
        {
          std::cerr << "[DoSdSync] Type conflict for " << loc.rel_path << ": "
                    << (loc.is_dir ? "directory" : "file") << " locally, "
                    << (sat.is_dir ? "directory" : "file") << " on Saturn. Skipping." << std::endl;
          fail_count++;
          blocked.insert(key);
          continue;
        }
        if (loc.is_dir) continue;

        if (mode == 1)
        {
          upload(loc, sat.rel_path);
        }
        else if (mode == 2)
        {
          download(sat, loc.rel_path);
        }
        else if (loc.size != sat.size)
        {
          // Bidirectional: the most recently modified side wins.
          if (loc.mtime < 0 || sat.mtime < 0 || loc.mtime / 60 == sat.mtime / 60)
          {
            std::cerr << "[DoSdSync] Conflict for " << loc.rel_path << " (Local: " << loc.size
                      << " bytes, Saturn: " << sat.size
                      << " bytes) and modification times cannot tell which is newer. Skipping." << std::endl;
            fail_count++;
          }
          else if (loc.mtime > sat.mtime)
          {
            std::cout << "[DoSdSync] Local copy of " << loc.rel_path << " is newer." << std::endl;
            upload(loc, sat.rel_path);
          }
          else
          {
            std::cout << "[DoSdSync] Saturn copy of " << sat.rel_path << " is newer." << std::endl;
            download(sat, loc.rel_path);
          }
        }
        else
        {
          skip_count++;
        }
      }
    }

    std::cout << "[DoSdSync] Sync complete. " << success_count << " succeeded, "
              << fail_count << " failed, " << skip_count << " unchanged." << std::endl;
    return (fail_count == 0) ? 1 : 0;
  }

} // namespace xfer
