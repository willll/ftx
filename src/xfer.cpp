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

#include "log.hpp"
#include "xfer.hpp"
#include "crc.hpp"
#include "saturn.hpp"

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

        // Step 4: If the command is for execution, append four zero bytes for reset flag
        if (USBDC_FUNC_EXEC_EXT == cmd)
        {
            SendBuf[i++] = 0x00;
            SendBuf[i++] = 0x00;
            SendBuf[i++] = 0x00;
            SendBuf[i++] = 0x00;
        }

        // Step 5: Write the command buffer to the FTDI device
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
        // Step 1: Calculate the duration of the transfer in microseconds
        auto delta_us = duration_cast<microseconds>(end - start).count();
        // Step 2: Convert duration to seconds
        double seconds = delta_us / 1'000'000.0;
        // Step 3: Calculate transfer speed in KB/s
        double speed_kb_s = (size / 1024.0) / seconds;
        // Step 4: Output transfer time
        std::cout << "Transfer time: " << std::fixed << std::setprecision(3) << seconds << " s" << std::endl;
        // Step 5: Output transfer speed
        std::cout << "Transfer speed: " << std::fixed << std::setprecision(2) << speed_kb_s << " KB/s" << std::endl;
    }

    int DoBiosDump(const char *filename)
    {
        // Step 1: Log the start of the BIOS dump process
        std::cout << "[DoBiosDump] Starting BIOS dump to file: " << filename << std::endl;
        // Step 2: Call DoDownload to perform the dump from the BIOS address and size
        return DoDownload(filename, saturn::bios_address, saturn::bios_size);
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
        // Step 1: Log the start of the download process with file, address, and size details
        std::cout << "[DoDownload] Starting download: file='" << filename << "', address=0x" << std::hex << address << ", size=" << std::dec << size << std::endl;
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
        status = xfer::SendCommandWithAddressAndLength(USBDC_FUNC_DOWNLOAD, address, size);
        if (status < 0)
        {
            // Step 6: Handle error if the download command fails
            std::cerr << "[DoDownload] Send download command error: " << ftdi_get_error_string(&Device) << std::endl;
            return 0;
        }

        // Step 7: Log the start of data reading from the device
        std::cout << "[DoDownload] Reading data from device..." << std::endl;
        // Step 8: Read data from the device until all requested bytes are received
        while (size - received > 0)
        {
            status = ftdi_read_data(&Device, &pFileBuffer[received], size - received);
            if (status < 0)
            {
                // Step 9: Handle error if reading data fails
                std::cerr << "[DoDownload] Read data error: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
            received += status;
            // Step 10: Log progress of received bytes
            std::cout << "[DoDownload] Received " << received << "/" << size << " bytes..." << std::endl;
        }

        // Step 11: Log waiting for the checksum byte
        std::cout << "[DoDownload] Waiting for checksum byte..." << std::endl;
        // Step 12: Read the checksum byte, looping until received or an error occurs
        do
        {
            status = ftdi_read_data(&Device, reinterpret_cast<unsigned char *>(&readChecksum), 1);
            if (status < 0)
            {
                // Step 13: Handle error if reading checksum fails
                std::cerr << "[DoDownload] Read data error: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
        } while (status == 0);

        // Step 14: Record the end time for performance measurement
        auto after = std::chrono::steady_clock::now();
        // Step 15: Log and calculate performance metrics
        std::cout << "[DoDownload] Data received. Calculating performance..." << std::endl;
        xfer::ReportPerformance(before, after, size);

        // Step 16: Log and calculate the CRC checksum of the received data
        std::cout << "[DoDownload] Calculating CRC..." << std::endl;
        calcChecksum = crc8::crc_update(calcChecksum, pFileBuffer.get(), size);

        // Step 17: Verify the calculated checksum against the received checksum
        if (readChecksum != calcChecksum)
        {
            // Step 18: Handle checksum mismatch error
            std::cerr << "[DoDownload] Checksum error (" << std::hex << static_cast<int>(calcChecksum)
                      << ", should be " << static_cast<int>(readChecksum) << ")" << std::endl;
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
        std::cout << "[" << functnName << "] Starting upload: file='" << filename << "', address=0x" << std::hex << address << std::dec << std::endl;
        // Step 3: Open the input file for reading
        FILE *File = fopen(filename, "rb");
        if (!File)
        {
            // Step 4: Handle error if file cannot be opened
            std::cerr << "[" << functnName << "] Can't open the file '" << filename << "'" << std::endl;
            return 0;
        }

        // Step 5: Get the file size
        fseek(File, 0, SEEK_END);
        long size = ftell(File);
        fseek(File, 0, SEEK_SET);
        if (size <= 0)
        {
            // Step 6: Handle error if file is empty or size cannot be determined
            std::cerr << "[" << functnName << "] File is empty or error reading size." << std::endl;
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
            std::cout << "[" << functnName << "] Uploading and executing at address 0x" << std::hex << address << std::dec << std::endl;
            SendBuf[0] = USBDC_FUNC_EXEC_EXT;
        }
        else
        {
            std::cout << "[" << functnName << "] Uploading to address 0x" << std::hex << address << std::dec << std::endl;
            SendBuf[0] = USBDC_FUNC_UPLOAD;
        }
        // Step 14: Send the upload command to the device
        int status = xfer::SendCommandWithAddressAndLength(SendBuf[0], address, size);
        if (status < 0)
        {
            // Step 15: Handle error if sending the upload command fails
            std::cerr << "[" << functnName << "] Send upload command error: " << ftdi_get_error_string(&Device) << std::endl;
            return 0;
        }

        // Step 16: Send the file data to the device in chunks
        unsigned int sent = 0;
        while (size - sent > 0)
        {
            status = ftdi_write_data(&Device, &pFileBuffer[sent], size - sent);
            if (status < 0)
            {
                // Step 17: Handle error if sending data fails
                std::cerr << "[" << functnName << "] Send data error: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
            sent += status;
            // Step 18: Log progress of sent bytes
            std::cout << "[" << functnName << "] Sent " << sent << "/" << size << " bytes..." << std::endl;
        }

        // Step 19: Send the checksum to the device
        SendBuf[0] = static_cast<unsigned char>(checksum);
        status = ftdi_write_data(&Device, SendBuf, 1);
        if (status < 0)
        {
            // Step 20: Handle error if sending checksum fails
            std::cerr << "[" << functnName << "] Send checksum error: " << ftdi_get_error_string(&Device) << std::endl;
            return 0;
        }

        // Step 21: Wait for the device to report the upload result
        do
        {
            status = ftdi_read_data(&Device, RecvBuf, 1);
            if (status < 0)
            {
                // Step 22: Handle error if reading upload result fails
                std::cerr << "[" << functnName << "] Read upload result failed: " << ftdi_get_error_string(&Device) << std::endl;
                return 0;
            }
        } while (status == 0);

        // Step 23: Check if the device reported an upload error
        if (RecvBuf[0] != 0)
        {
            std::cerr << "[" << functnName << "] Device reported upload error." << std::endl;
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
        std::cout << "[DoRun] Executing at address 0x" << std::hex << address << std::dec << std::endl;
        // Step 2: Set the execute command in the send buffer
        SendBuf[0] = USBDC_FUNC_EXEC;
        // Step 3: Extract and store the address bytes in big-endian order
        SendBuf[1] = static_cast<unsigned char>(address >> 24);
        SendBuf[2] = static_cast<unsigned char>(address >> 16);
        SendBuf[3] = static_cast<unsigned char>(address >> 8);
        SendBuf[4] = static_cast<unsigned char>(address);
        // Step 4: Send the execute command to the device
        int status = ftdi_write_data(&Device, SendBuf, 5);
        if (status < 0)
        {
            // Step 5: Handle error if sending the execute command fails
            std::cerr << "[DoRun] Send execute error: " << ftdi_get_error_string(&Device) << std::endl;
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
        std::cout << "[DoExecute] Uploading and executing: file='" << filename << "', address=0x" << std::hex << address << std::dec << std::endl;
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
    /**
     * @brief Initialize FTDI device communication.
     * @param VID USB Vendor ID.
     * @param PID USB Product ID.
     * @return 1 on success, 0 on error.
     */
    int InitComms(int VID, int PID, bool recover)
    {
        // Step 1: Log the initialization attempt with VID and PID
        std::cout << "[InitComms] Initializing FTDI device (VID=0x" << std::hex << VID << ", PID=0x" << PID << ")" << std::dec << std::endl;
        int status = ftdi_init(&Device);
        bool error = false;

        // Step 2: Initialize the FTDI device context
        if (status < 0)
        {
            // Step 3: Handle error if FTDI initialization fails
            std::cerr << "[InitComms] Init error: " << ftdi_get_error_string(&Device) << std::endl;
            error = true;
        }
        else
        {
            // Step 4: Open the FTDI device using VID and PID
            status = ftdi_usb_open(&Device, VID, PID);
            if (status < 0 && status != -5)
            {
                // Step 5: Handle error if device cannot be opened (except for specific error -5)
                std::cerr << "[InitComms] Device open error: " << ftdi_get_error_string(&Device) << std::endl;
                error = true;
            }
            else
            {
                // Step 6: Purge the FTDI device buffers
                status = ftdi_tcioflush(&Device);
                if (status < 0)
                {
                    // Step 7: Handle error if buffer purge fails
                    std::cerr << "[InitComms] Purge buffers error: " << ftdi_get_error_string(&Device) << std::endl;

                    if (!recover)
                    {
                        // Step 8: Attempt recovery if not already in recovery mode
                        std::cerr << "[InitComms] Attempting to recover from error..." << std::endl;
                        ftdi_usb_reset(&Device);
                        status = ftdi_usb_close(&Device);
                        if (status < 0)
                        {
                            // Step 9: Handle error if closing device during recovery fails
                            std::cerr << "[InitComms] Close error during recovery: " << ftdi_get_error_string(&Device) << std::endl;
                            error = true;
                        }
                        else
                        {
                            // Step 10: Retry initialization with recovery flag set
                            return InitComms(VID, PID, true);
                        }
                    }
                    else
                    {
                        // Step 11: Handle failure if recovery attempt fails
                        std::cerr << "[InitComms] Recovery attempt failed. Aborting." << std::endl;
                        error = true;
                    }
                }

                // Step 12: Set the read chunk size for the FTDI device
                status = ftdi_read_data_set_chunksize(&Device, USB_READPACKET_SIZE);
                if (status < 0)
                {
                    // Step 13: Handle error if setting read chunk size fails
                    std::cerr << "[InitComms] Set read chunksize error: " << ftdi_get_error_string(&Device) << std::endl;
                    error = true;
                }

                // Step 14: Set the write chunk size for the FTDI device
                status = ftdi_write_data_set_chunksize(&Device, USB_WRITEPACKET_SIZE);
                if (status < 0)
                {
                    // Step 15: Handle error if setting write chunk size fails
                    std::cerr << "[InitComms] Set write chunksize error: " << ftdi_get_error_string(&Device) << std::endl;
                    error = true;
                }

                // Step 16: Configure the FTDI device to reset bitmode
                status = ftdi_set_bitmode(&Device, 0x0, BITMODE_RESET);
                if (status < 0)
                {
                    // Step 17: Handle error if bitmode configuration fails
                    std::cerr << "[InitComms] Bitmode configuration error: " << ftdi_get_error_string(&Device) << std::endl;
                    error = true;
                }

                // Step 18: Close the device if any error occurred
                if (error)
                {
                    ftdi_usb_close(&Device);
                }
            }
        }

        // Step 19: Log successful initialization if no errors
        if (!error)
        {
            std::cout << "[InitComms] FTDI device initialized successfully." << std::endl;
        }
        // Step 20: Return success or failure
        return !error;
    }
    /**
     * @brief Close FTDI device communication and purge buffers.
     */
    void CloseComms()
    {
        // Step 1: Log the start of the device closing process
        std::cout << "[CloseComms] Closing FTDI device and purging buffers..." << std::endl;
        // Step 2: Purge the FTDI device buffers
        int status = ftdi_tcioflush(&Device);
        if (status < 0)
        {
            // Step 3: Handle error if buffer purge fails
            std::cerr << "[CloseComms] Purge buffers error: " << ftdi_get_error_string(&Device) << std::endl;
        }
        // Step 4: Close the FTDI device
        ftdi_usb_close(&Device);
        // Step 5: Log successful closure
        std::cout << "[CloseComms] FTDI device closed." << std::endl;
    }

    /**
     * @brief Enter debug console mode, printing device output to stdout and acknowledging character consumption.
     */
    void DoConsole(bool acknowledge)
    {
        // setting std::cout to unbuffered mode
        //std::cout.setf(std::ios::unitbuf);
        // Step 1: Define the receive buffer size
        const int RecvBufSize = 512;
        // Step 2: Allocate a buffer for receiving console data
        std::unique_ptr<unsigned char[]> pFileBuffer(new unsigned char[RecvBufSize]);
        // Step 3: Log entry into debug console mode
        std::cout << "[DoConsole] Entering debug console mode. Press Ctrl+C to exit." << std::endl;
        int status = 0;
        // Step 4: Define the acknowledgment byte (e.g., ASCII ACK = 0x06)
        const unsigned char ackByte = 0x06;

        // Step 5: Continuously read data from the device
        while (status >= 0)
        {
            // Step 6: Read data into the buffer
            status = ftdi_read_data(&Device, pFileBuffer.get(), RecvBufSize);
            
            cdbg << "[DoConsole] Read status: " << status << " bytes" << std::endl;

            if (status < 0)
            {
                // Step 7: Handle error if reading data fails
                std::cerr << "[DoConsole] Read data error: " << ftdi_get_error_string(&Device) << std::endl;
            }
            else if (status == 0)
            {
                // No data received; sleep briefly to avoid busy-waiting
                //std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            else if (status > 0)
            {
                // Log raw data in hex
                // cdbg << "[DoConsole] Raw data: ";
                // for (int ii = 0; ii < status; ++ii)
                // {
                //     cdbg << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(pFileBuffer[ii]) << " ";
                // }
                // cdbg << std::dec << std::endl;

                // Step 8: Process each received byte
                for (int ii = 0; ii < status; ii++)
                {
                    unsigned char c = pFileBuffer[ii];
                    if (isprint(c) || isblank(c))
                    {
                        // Step 9: Print printable or blank characters directly
                        std::cout << static_cast<char>(c);
                    }
                    else if (c == '\n')
                    {
                        // Step 10: Handle newline characters
                        std::cout << std::endl;
                    }
                    else if (c == '\t')
                    {
                        // Step 10: Handle tab characters
                        std::cout << "    ";  // Print four spaces for a tab
                    }
                    else if (c == '\r')
                    {
                        // Step 11: Handle carriage return (optional: could print or process differently)
                        std::cout << "[CR]";
                    }
                    else if (c == '\0' || c == '\1' || c == '\2')
                    {
                        // Step 11: Handle null characters (optional: could print or process differently)
                        cdbg << "[NULL]";
                    }
                    else
                    {
                        // Step 12: Handle non-printable characters by displaying their hex value
                        std::cout << "[0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << "]" << std::dec;
                    }
                }
                // Step 13: Flush the output to ensure immediate display
                std::cout.flush();

                // if (acknowledge)
                // {
                //     // Step 14: Send acknowledgment to the device to indicate data consumption
                //     int writeStatus = ftdi_write_data(&Device, &ackByte, 1);
                //     if (writeStatus < 0)
                //     {
                //         // Step 15: Handle error if sending acknowledgment fails
                //         std::cerr << "[DoConsole] Failed to send ACK: " << ftdi_get_error_string(&Device) << std::endl;
                //         break;
                //     }
                //     else if (writeStatus != 1)
                //     {
                //         // Step 16: Handle case where ACK was not sent correctly
                //         std::cerr << "[DoConsole] Incomplete ACK transmission: " << writeStatus << " bytes sent" << std::endl;
                //         break;
                //     }
                //     cdbg << "[DoConsole] Sent ACK (0x06)" << std::endl;
                // }
            }
        }

        // Step 17: Log exit from console mode
        std::cout << "[DoConsole] Exiting debug console mode." << std::endl;
    }

    /**
     * @brief Signal handler for clean exit on interrupt.
     * @param sig Signal number.
     */
    void Signal(int sig)
    {
        // Step 1: Log the caught signal
        std::cout << "\n[Signal] Caught signal " << sig << ", exiting..." << std::endl;
        // Step 2: Close the FTDI device communication
        xfer::CloseComms();
        // Step 3: Exit the program
        std::exit(EXIT_SUCCESS);
    }

} // namespace xfer