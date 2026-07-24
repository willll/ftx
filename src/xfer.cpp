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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
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
    std::string mtime;
  };

  static std::vector<SdSyncEntry> ListSaturnDirForSync(const std::string &saturn_dir)
  {
    std::vector<SdSyncEntry> entries;
    std::string listing;
    std::string path_arg = "-l " + saturn_dir;
    if (xfer::DoListStr(path_arg.c_str(), listing) != 1)
    {
      return entries;
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
        if (name == "." || name == "..") continue;

        SdSyncEntry entry;
        entry.rel_path = name;
        entry.is_dir = is_dir;
        entry.size = size;
        entry.mtime = date + " " + time_str;
        entries.push_back(entry);
      }
    }
    return entries;
  }

  static void GetSaturnTreeRecursive(const std::string &saturn_base, const std::string &current_rel, std::vector<SdSyncEntry> &out_list)
  {
    std::string current_saturn = saturn_base;
    if (!current_rel.empty())
    {
      if (current_saturn.back() != '/') current_saturn += '/';
      current_saturn += current_rel;
    }

    auto items = ListSaturnDirForSync(current_saturn);
    for (const auto &item : items)
    {
      std::string rel = current_rel.empty() ? item.rel_path : (current_rel + "/" + item.rel_path);
      SdSyncEntry node;
      node.rel_path = rel;
      node.is_dir = item.is_dir;
      node.size = item.size;
      node.mtime = item.mtime;
      out_list.push_back(node);

      if (item.is_dir)
      {
        GetSaturnTreeRecursive(saturn_base, rel, out_list);
      }
    }
  }

  static void GetLocalTreeRecursive(const std::filesystem::path &local_base, const std::string &current_rel, std::vector<SdSyncEntry> &out_list)
  {
    std::filesystem::path current_local = local_base;
    if (!current_rel.empty())
    {
      current_local /= current_rel;
    }

    std::error_code ec;
    if (!std::filesystem::exists(current_local, ec)) return;

    for (const auto &entry : std::filesystem::directory_iterator(current_local, ec))
    {
      std::string name = entry.path().filename().string();
      std::string rel = current_rel.empty() ? name : (current_rel + "/" + name);
      SdSyncEntry node;
      node.rel_path = rel;
      node.is_dir = entry.is_directory(ec);
      if (!node.is_dir)
      {
        node.size = static_cast<uint32_t>(entry.file_size(ec));
      }
      out_list.push_back(node);

      if (node.is_dir)
      {
        GetLocalTreeRecursive(local_base, rel, out_list);
      }
    }
  }

  static std::string CombineSaturnPath(const std::string &base, const std::string &rel)
  {
    if (base == "/") return "/" + rel;
    if (!base.empty() && base.back() == '/') return base + rel;
    return base + "/" + rel;
  }

  int DoSdSync(const char *local_path, const char *saturn_sd_path, int mode)
  {
    if (!local_path || !saturn_sd_path)
    {
      std::cerr << "[DoSdSync] Missing local or Saturn SD path." << std::endl;
      return 0;
    }

    std::string saturn_base = saturn_sd_path;
    if (saturn_base.empty() || saturn_base[0] != '/')
    {
      saturn_base = "/" + saturn_base;
    }
    if (saturn_base.size() > 1 && saturn_base.back() == '/')
    {
      saturn_base.pop_back();
    }

    std::filesystem::path local_base(local_path);
    std::error_code ec;

    std::cout << "[DoSdSync] Synchronizing (Mode " << mode << "): '"
              << local_base.string() << "' <-> Saturn:'" << saturn_base << "'" << std::endl;

    if (mode == 1)
    {
      // Mode 1: Copy local -> Saturn SD (default)
      if (!std::filesystem::exists(local_base, ec))
      {
        std::cerr << "[DoSdSync] Local directory does not exist: " << local_base << std::endl;
        return 0;
      }

      std::vector<SdSyncEntry> local_items;
      GetLocalTreeRecursive(local_base, "", local_items);

      // Create base folder on Saturn SD
      xfer::DoMkdir(saturn_base.c_str());

      // Create all directories first
      for (const auto &item : local_items)
      {
        if (item.is_dir)
        {
          std::string target_saturn = CombineSaturnPath(saturn_base, item.rel_path);
          xfer::DoMkdir(target_saturn.c_str());
        }
      }

      // Copy all files
      int success_count = 0;
      int fail_count = 0;
      for (const auto &item : local_items)
      {
        if (!item.is_dir)
        {
          std::filesystem::path src = local_base / item.rel_path;
          std::string target_saturn = CombineSaturnPath(saturn_base, item.rel_path);
          std::cout << "[DoSdSync] Uploading " << item.rel_path << " -> " << target_saturn << std::endl;
          if (xfer::DoSdUpload(src.string().c_str(), target_saturn.c_str()) == 1)
          {
            success_count++;
          }
          else
          {
            fail_count++;
          }
        }
      }
      std::cout << "[DoSdSync] Upload sync complete. " << success_count << " succeeded, " << fail_count << " failed." << std::endl;
      return (fail_count == 0) ? 1 : 0;
    }
    else if (mode == 2)
    {
      // Mode 2: Copy Saturn SD -> local
      std::filesystem::create_directories(local_base, ec);

      std::vector<SdSyncEntry> saturn_items;
      GetSaturnTreeRecursive(saturn_base, "", saturn_items);

      // Create all local directories first
      for (const auto &item : saturn_items)
      {
        if (item.is_dir)
        {
          std::filesystem::path target_local = local_base / item.rel_path;
          std::filesystem::create_directories(target_local, ec);
        }
      }

      // Download all files
      int success_count = 0;
      int fail_count = 0;
      for (const auto &item : saturn_items)
      {
        if (!item.is_dir)
        {
          std::string src_saturn = CombineSaturnPath(saturn_base, item.rel_path);
          std::filesystem::path target_local = local_base / item.rel_path;
          std::cout << "[DoSdSync] Downloading " << src_saturn << " -> " << target_local.string() << std::endl;
          if (xfer::DoSdDownload(src_saturn.c_str(), target_local.string().c_str()) == 1)
          {
            success_count++;
          }
          else
          {
            fail_count++;
          }
        }
      }
      std::cout << "[DoSdSync] Download sync complete. " << success_count << " succeeded, " << fail_count << " failed." << std::endl;
      return (fail_count == 0) ? 1 : 0;
    }
    else if (mode == 3)
    {
      // Mode 3: Synchronize both folders recursively (bidirectional)
      std::filesystem::create_directories(local_base, ec);

      std::vector<SdSyncEntry> local_items;
      GetLocalTreeRecursive(local_base, "", local_items);

      std::vector<SdSyncEntry> saturn_items;
      GetSaturnTreeRecursive(saturn_base, "", saturn_items);

      std::map<std::string, SdSyncEntry> local_map;
      for (const auto &it : local_items) local_map[it.rel_path] = it;

      std::map<std::string, SdSyncEntry> saturn_map;
      for (const auto &it : saturn_items) saturn_map[it.rel_path] = it;

      std::set<std::string> all_paths;
      for (const auto &it : local_items) all_paths.insert(it.rel_path);
      for (const auto &it : saturn_items) all_paths.insert(it.rel_path);

      xfer::DoMkdir(saturn_base.c_str());

      int success_count = 0;
      int fail_count = 0;

      for (const auto &rel : all_paths)
      {
        bool in_local = local_map.count(rel) > 0;
        bool in_saturn = saturn_map.count(rel) > 0;

        if (in_local && in_saturn)
        {
          const auto &loc = local_map[rel];
          const auto &sat = saturn_map[rel];
          if (loc.is_dir && sat.is_dir)
          {
            continue;
          }
          else if (!loc.is_dir && !sat.is_dir)
          {
            // Both are files. If size differs, update from local to Saturn
            if (loc.size != sat.size)
            {
              std::cout << "[DoSdSync] File size mismatch for " << rel << " (Local: " << loc.size << ", Saturn: " << sat.size << "). Updating Saturn." << std::endl;
              std::filesystem::path src_local = local_base / rel;
              std::string dst_saturn = CombineSaturnPath(saturn_base, rel);
              if (xfer::DoSdUpload(src_local.string().c_str(), dst_saturn.c_str()) == 1) success_count++;
              else fail_count++;
            }
          }
        }
        else if (in_local && !in_saturn)
        {
          const auto &loc = local_map[rel];
          std::string dst_saturn = CombineSaturnPath(saturn_base, rel);
          if (loc.is_dir)
          {
            xfer::DoMkdir(dst_saturn.c_str());
          }
          else
          {
            std::filesystem::path src_local = local_base / rel;
            std::cout << "[DoSdSync] Uploading missing file to Saturn: " << rel << std::endl;
            if (xfer::DoSdUpload(src_local.string().c_str(), dst_saturn.c_str()) == 1) success_count++;
            else fail_count++;
          }
        }
        else if (!in_local && in_saturn)
        {
          const auto &sat = saturn_map[rel];
          std::filesystem::path dst_local = local_base / rel;
          std::string src_saturn = CombineSaturnPath(saturn_base, rel);
          if (sat.is_dir)
          {
            std::filesystem::create_directories(dst_local, ec);
          }
          else
          {
            std::cout << "[DoSdSync] Downloading missing file to local: " << rel << std::endl;
            if (xfer::DoSdDownload(src_saturn.c_str(), dst_local.string().c_str()) == 1) success_count++;
            else fail_count++;
          }
        }
      }

      std::cout << "[DoSdSync] Bidirectional sync complete. " << success_count << " operations succeeded, " << fail_count << " failed." << std::endl;
      return (fail_count == 0) ? 1 : 0;
    }

    std::cerr << "[DoSdSync] Invalid sync mode: " << mode << std::endl;
    return 0;
  }

} // namespace xfer
