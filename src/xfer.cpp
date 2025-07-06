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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <memory>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

#include "xfer.hpp"
#include "crc.hpp"

#include "sc_common.h"

namespace xfer
{

    unsigned char SendBuf[2 * WRITE_PAYLOAD_SIZE];
    unsigned char RecvBuf[2 * READ_PAYLOAD_SIZE];
    struct ftdi_context Device = {0};

    /**
     * @brief Send a command with address and length to the device.
     * @param cmd Command code.
     * @param address Target address.
     * @param size Data size.
     * @return Number of bytes written, or negative on error.
     */
    int SendCommandWithAddressAndLength(unsigned int cmd, unsigned int address,
                                        unsigned int size)
    {
        uint8_t i = 0;

        SendBuf[i++] = cmd;
        SendBuf[i++] = static_cast<unsigned char>(address >> 24);
        SendBuf[i++] = static_cast<unsigned char>(address >> 16);
        SendBuf[i++] = static_cast<unsigned char>(address >> 8);
        SendBuf[i++] = static_cast<unsigned char>(address);
        SendBuf[i++] = static_cast<unsigned char>(size >> 24);
        SendBuf[i++] = static_cast<unsigned char>(size >> 16);
        SendBuf[i++] = static_cast<unsigned char>(size >> 8);
        SendBuf[i++] = static_cast<unsigned char>(size);

        if(USBDC_FUNC_EXEC_EXT == cmd)
        {
            // Reset_flag
            SendBuf[i++] = 0x00;
            SendBuf[i++] = 0x00;
            SendBuf[i++] = 0x00;
            SendBuf[i++] = 0x00;
        }


        return ftdi_write_data(&Device, SendBuf, i);
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
        auto delta_us = duration_cast<microseconds>(end - start).count();
        double seconds = delta_us / 1'000'000.0;
        double speed_kb_s = (size / 1024.0) / seconds;
        std::cout << "Transfer time: " << std::fixed << std::setprecision(3) << seconds << " s" << std::endl;
        std::cout << "Transfer speed: " << std::fixed << std::setprecision(2) << speed_kb_s << " KB/s" << std::endl;
    }

    // Placeholders for the actual implementations:
    /**
     * @brief Download data from device and write to file.
     * @param filename Output file name.
     * @param address Device address to read from.
     * @param size Number of bytes to download.
     * @return 1 on success, 0 on error.
     */
    int DoDownload(const char *filename, unsigned int address, unsigned int size)
    {
        std::cout << "[DoDownload] Starting download: file='" << filename << "', address=0x" << std::hex << address << ", size=" << std::dec << size << std::endl;
        std::unique_ptr<unsigned char[]> pFileBuffer(new unsigned char[size]);
        FILE *File = nullptr;
        unsigned int received = 0;
        int status = -1;
        crc8::crc_t readChecksum = 0, calcChecksum = 0;
        auto before = std::chrono::steady_clock::now();
        std::cout << "[DoDownload] Sending download command..." << std::endl;
        status = xfer::SendCommandWithAddressAndLength(USBDC_FUNC_DOWNLOAD, address, size);
        if (status < 0)
        {
            std::cerr << "[DoDownload] Send download command error: " << ftdi_get_error_string(&Device) << std::endl;
            return 0;
        }

        std::cout << "[DoDownload] Reading data from device..." << std::endl;
        while (size - received > 0)
        {
            status = ftdi_read_data(&Device, &pFileBuffer[received], size - received);
            if (status < 0)
            {
                std::cerr << "[DoDownload] Read data error: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
            received += status;
            std::cout << "[DoDownload] Received " << received << "/" << size << " bytes..." << std::endl;
        }

        std::cout << "[DoDownload] Waiting for checksum byte..." << std::endl;
        // The transfer may timeout, so loop until a byte is received or an error occurs.
        do
        {
            status = ftdi_read_data(&Device, reinterpret_cast<unsigned char *>(&readChecksum), 1);
            if (status < 0)
            {
                std::cerr << "[DoDownload] Read data error: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
        } while (status == 0);

        auto after = std::chrono::steady_clock::now();
        std::cout << "[DoDownload] Data received. Calculating performance..." << std::endl;
        xfer::ReportPerformance(before, after, size);

        std::cout << "[DoDownload] Calculating CRC..." << std::endl;
        calcChecksum = crc8::crc_update(calcChecksum, pFileBuffer.get(), size);

        if (readChecksum != calcChecksum)
        {
            std::cerr << "[DoDownload] Checksum error (" << std::hex << static_cast<int>(calcChecksum)
                      << ", should be " << static_cast<int>(readChecksum) << ")" << std::endl;
            return 0;
        }

        std::cout << "[DoDownload] Writing to file..." << std::endl;
        File = fopen(filename, "wb");
        if (File == nullptr)
        {
            std::cerr << "[DoDownload] Error creating output file" << std::endl;
            return 0;
        }
        fwrite(pFileBuffer.get(), 1, size, File);
        fclose(File);
        std::cout << "[DoDownload] Download complete." << std::endl;
        return status < 0 ? 0 : 1;
    }

    /**
     * @brief Upload data from file to device.
     * @param filename Input file name.
     * @param address Device address to write to.
     * @return 1 on success, 0 on error.
     */
    int DoUpload(const char *filename, unsigned int address, const bool execute)
    {
        std::string functnName = execute ? "DoUploadExecute" : "DoUpload";

        std::cout << "[" << functnName << "] Starting upload: file='" << filename << "', address=0x" << std::hex << address << std::dec << std::endl;
        FILE *File = fopen(filename, "rb");
        if (!File)
        {
            std::cerr << "[" << functnName << "] Can't open the file '" << filename << "'" << std::endl;
            return 0;
        }

        fseek(File, 0, SEEK_END);
        long size = ftell(File);
        fseek(File, 0, SEEK_SET);
        if (size <= 0)
        {
            std::cerr << "[" << functnName << "] File is empty or error reading size." << std::endl;
            fclose(File);
            return 0;
        }

        std::unique_ptr<unsigned char[]> pFileBuffer(new unsigned char[size]);
        if (fread(pFileBuffer.get(), 1, size, File) != static_cast<size_t>(size))
        {
            std::cerr << "[" << functnName << "] File read error" << std::endl;
            fclose(File);
            return 0;
        }
        fclose(File);

        crc8::crc_t checksum = crc8::crc_update(checksum, pFileBuffer.get(), size);

        auto before = std::chrono::steady_clock::now();

        if (execute)
        {
            std::cout << "[" << functnName << "] Uploading and executing at address 0x" << std::hex << address << std::dec << std::endl;
            SendBuf[0] = USBDC_FUNC_EXEC_EXT;
        }
        else
        {
            std::cout << "[" << functnName << "] Uploading to address 0x" << std::hex << address << std::dec << std::endl;
            SendBuf[0] = USBDC_FUNC_UPLOAD;
        }
        int status = xfer::SendCommandWithAddressAndLength(SendBuf[0], address, size);
        if (status < 0)
        {
            std::cerr << "[" << functnName << "] Send upload command error: " << ftdi_get_error_string(&Device) << std::endl;
            return 0;
        }

        unsigned int sent = 0;
        while (size - sent > 0)
        {
            status = ftdi_write_data(&Device, &pFileBuffer[sent], size - sent);
            if (status < 0)
            {
                std::cerr << "[" << functnName << "] Send data error: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
            sent += status;
            std::cout << "[" << functnName << "] Sent " << sent << "/" << size << " bytes..." << std::endl;
        }

        SendBuf[0] = static_cast<unsigned char>(checksum);
        status = ftdi_write_data(&Device, SendBuf, 1);
        if (status < 0)
        {
            std::cerr << "[" << functnName << "] Send checksum error: " << ftdi_get_error_string(&Device) << std::endl;
            return 0;
        }

        do
        {
            status = ftdi_read_data(&Device, RecvBuf, 1);
            if (status < 0)
            {
                std::cerr << "[" << functnName << "] Read upload result failed: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
        } while (status == 0);

        if (RecvBuf[0] != 0)
        {
            std::cerr << "[" << functnName << "] Device reported upload error." << std::endl;
            return 0;
        }

        auto after = std::chrono::steady_clock::now();
        xfer::ReportPerformance(before, after, size);
        std::cout << "[" << functnName << "] Upload complete." << std::endl;

        return 1;
    }

    /**
     * @brief Send execute command to device at given address.
     * @param address Address to execute.
     * @return 1 on success, 0 on error.
     */
    int DoRun(unsigned int address)
    {
        std::cout << "[DoRun] Executing at address 0x" << std::hex << address << std::dec << std::endl;
        SendBuf[0] = USBDC_FUNC_EXEC;
        SendBuf[1] = static_cast<unsigned char>(address >> 24);
        SendBuf[2] = static_cast<unsigned char>(address >> 16);
        SendBuf[3] = static_cast<unsigned char>(address >> 8);
        SendBuf[4] = static_cast<unsigned char>(address);
        int status = ftdi_write_data(&Device, SendBuf, 5);
        if (status < 0)
        {
            std::cerr << "[DoRun] Send execute error: " << ftdi_get_error_string(&Device) << std::endl;
            return 0;
        }
        std::cout << "[DoRun] Execute command sent successfully." << std::endl;
        return 1;
    }

    /**
     * @brief Upload file to device and execute at given address.
     * @param filename Input file name.
     * @param address Address to execute.
     * @return 1 on success, 0 on error.
     */
    int DoExecute(const char *filename, unsigned int address)
    {
        std::cout << "[DoExecute] Uploading and executing: file='" << filename << "', address=0x" << std::hex << address << std::dec << std::endl;
        int status = 0;
        if (DoUpload(filename, address, true))
        {
            std::cout << "[DoExecute] Upload successful. Executing..." << std::endl;
        }
        else
        {
            std::cerr << "[DoExecute] Upload failed. Execution aborted." << std::endl;
        }
        return status;
    }
    /**
     * @brief Initialize FTDI device communication.
     * @param VID USB Vendor ID.
     * @param PID USB Product ID.
     * @return 1 on success, 0 on error.
     */
    int InitComms(int VID, int PID)
    {
        std::cout << "[InitComms] Initializing FTDI device (VID=0x" << std::hex << VID << ", PID=0x" << PID << ")" << std::dec << std::endl;
        int status = ftdi_init(&Device);
        bool error = false;

        if (status < 0)
        {
            std::cerr << "[InitComms] Init error: " << ftdi_get_error_string(&Device) << std::endl;
            error = true;
        }
        else
        {
            status = ftdi_usb_open(&Device, VID, PID);
            if (status < 0 && status != -5)
            {
                std::cerr << "[InitComms] Device open error: " << ftdi_get_error_string(&Device) << std::endl;
                error = true;
            }
            else
            {
                status = ftdi_tcioflush(&Device);
                if (status < 0)
                {
                    std::cerr << "[InitComms] Purge buffers error: " << ftdi_get_error_string(&Device) << std::endl;
                    error = true;
                }

                status = ftdi_read_data_set_chunksize(&Device, USB_READPACKET_SIZE);
                if (status < 0)
                {
                    std::cerr << "[InitComms] Set read chunksize error: " << ftdi_get_error_string(&Device) << std::endl;
                    error = true;
                }

                status = ftdi_write_data_set_chunksize(&Device, USB_WRITEPACKET_SIZE);
                if (status < 0)
                {
                    std::cerr << "[InitComms] Set write chunksize error: " << ftdi_get_error_string(&Device) << std::endl;
                    error = true;
                }

                status = ftdi_set_bitmode(&Device, 0x0, BITMODE_RESET);
                if (status < 0)
                {
                    std::cerr << "[InitComms] Bitmode configuration error: " << ftdi_get_error_string(&Device) << std::endl;
                    error = true;
                }

                if (error)
                {
                    ftdi_usb_close(&Device);
                }
            }
        }

        if (!error)
        {
            std::cout << "[InitComms] FTDI device initialized successfully." << std::endl;
        }
        return !error;
    }
    /**
     * @brief Close FTDI device communication and purge buffers.
     */
    void CloseComms()
    {
        std::cout << "[CloseComms] Closing FTDI device and purging buffers..." << std::endl;
        int status = ftdi_tcioflush(&Device);
        if (status < 0)
        {
            std::cerr << "[CloseComms] Purge buffers error: " << ftdi_get_error_string(&Device) << std::endl;
        }
        ftdi_usb_close(&Device);
        std::cout << "[CloseComms] FTDI device closed." << std::endl;
    }

    /**
     * @brief Enter debug console mode, printing device output to stdout.
     */
    void DoConsole()
    {
        std::cout << "[DoConsole] Entering debug console mode. Press Ctrl+C to exit." << std::endl;
        int status = 0;
        while (status >= 0)
        {
            // Read data in smaller chunks
            status = ftdi_read_data(&Device, RecvBuf, 62);
            if (status < 0)
            {
                std::cerr << "[DoConsole] Read data error: " << ftdi_get_error_string(&Device) << std::endl;
            }
            else
            {
                for (int ii = 0; ii < status; ++ii)
                {
                    if (isprint(RecvBuf[ii]) || isblank(RecvBuf[ii]))
                    {
                        std::cout << static_cast<char>(RecvBuf[ii]);
                    }
                }
                std::cout.flush();
            }
        }
    }
    /**
     * @brief Signal handler for clean exit on interrupt.
     * @param sig Signal number.
     */
    void Signal(int sig)
    {
        std::exit(EXIT_FAILURE);
    }

} // namespace xfer
