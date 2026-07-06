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
#include <memory>
#include <thread>

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
    constexpr uint8_t REMOTE_IO_MAGIC[4] = {'S', 'R', 'L', '1'};
    constexpr uint32_t SDC_BLOCK_SIZE = 512;

    enum class RemoteIoCommand : uint8_t
    {
      LIST = 1,
      REMOVE = 2,
      CRC = 3,
      UPLOAD = 4,
      MKDIR = 5,
      RMDIR = 6
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

    template <typename WriteFunc>
    bool StreamAndCrc(FILE* f, crc8::crc_t& checksum_out, WriteFunc write_func)
    {
      unsigned char buffer[4096];
      crc8::crc_t checksum = 0;
      size_t bytes_read;
      while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0)
      {
        if (!write_func(buffer, bytes_read))
        {
          return false;
        }
        checksum = crc8::crc_update(checksum, buffer, bytes_read);
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
   * @copydoc xfer::DoRemove
   */
  int DoRemove(const char *path)
  {
    return ExecuteRemoteIoCommand(RemoteIoCommand::REMOVE, path, "DoRemove");
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

    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(filename, "wb"), &fclose);
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
    
    unsigned char buffer[4096];
    std::size_t received = 0;
    while (size - received > 0 && !ftdi::g_interrupt_flag)
    {
      size_t to_read = std::min<size_t>(sizeof(buffer), size - received);
      status = ftdi_read_data(&ftdi::g_Device, buffer, to_read);
      if (status < 0)
      {
        std::cerr << "[DoDownload] Read data error: " << ftdi_get_error_string(&ftdi::g_Device) << std::endl;
        return 0;
      }
      if (status > 0)
      {
        if (fwrite(buffer, 1, status, file.get()) != static_cast<size_t>(status))
        {
          std::cerr << "[DoDownload] File write error" << std::endl;
          return 0;
        }
        calcChecksum = crc8::crc_update(calcChecksum, buffer, status);
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

    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(filename, "rb"), &fclose);
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
    if (ec || file_size_raw == 0)
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

    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(host_filename, "rb"), &fclose);
    if (!file)
    {
      std::cerr << "[DoSdUpload] Can't open file '" << host_filename << "'" << std::endl;
      return 0;
    }

    if (saturn_sd_path != nullptr && saturn_sd_path[0] == '/')
    {
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

} // namespace xfer
