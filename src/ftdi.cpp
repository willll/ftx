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
#include <libusb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <memory>
#include <iostream>
#include <iomanip>

#include "log.hpp"
#include "ftdi.hpp"
#include "xfer.hpp"

namespace ftdi {

// FTDI device context shared across the module
struct ftdi_context g_Device = {0};

/**
 * @brief Initialize FTDI device communication.
 * @param VID USB Vendor ID.
 * @param PID USB Product ID.
 * @return 1 on success, 0 on error.
 */
int InitComms(int VID, int PID, const std::string &Serial, bool recover)
{
    // Step 1: Log the initialization attempt with VID and PID
    if (Serial.empty())
    {
        std::cout << "[InitComms] Initializing FTDI device (VID=0x" << std::hex << VID
                  << ", PID=0x" << PID
                  << ")" << std::dec << std::endl;
    }
    else
    {
        std::cout << "[InitComms] Initializing FTDI device (VID=0x" << std::hex << VID
                  << ", PID=0x" << PID
                  << ", Serial=" << Serial
                  << ")" << std::dec << std::endl;
    }

    int status = ftdi_init(&g_Device);
    bool error = false;

    // Step 2: Initialize the FTDI device context
    if (status < 0)
    {
        // Step 3: Handle error if FTDI initialization fails
        std::cerr << "[InitComms] Init error: " << ftdi_get_error_string(&g_Device) << std::endl;
        error = true;
    }
    else
    {
        // Step 4: Open the FTDI device using VID and PID
        if (Serial.empty())
        {
            status = ftdi_usb_open(&g_Device, VID, PID);
        }
        else
        {
            status = ftdi_usb_open_desc(&g_Device, VID, PID, nullptr, Serial.c_str());
        }

        if (status < 0 && status != -5)
        {
            // Step 5: Handle error if device cannot be opened (except for specific error -5)
            std::cerr << "[InitComms] Device open error: " << ftdi_get_error_string(&g_Device) << std::endl;
            error = true;
        }
        else
        {
            // Step 6: Purge the FTDI device buffers
            status = ftdi_tcioflush(&g_Device);
            if (status < 0)
            {
                // Step 7: Handle error if buffer purge fails
                std::cerr << "[InitComms] Purge buffers error (" << status << "): " << ftdi_get_error_string(&g_Device) << std::endl;

                if (!recover)
                {
                    // Step 8: Attempt recovery if not already in recovery mode
                    std::cerr << "[InitComms] Attempting to recover from error..." << std::endl;
                    ftdi_usb_reset(&g_Device);
                    status = ftdi_usb_close(&g_Device);
                    if (status < 0)
                    {
                        // Step 9: Handle error if closing device during recovery fails
                        std::cerr << "[InitComms] Close error during recovery: " << ftdi_get_error_string(&g_Device) << std::endl;
                        error = true;
                    }
                    else
                    {
                        // Step 10: Retry initialization with recovery flag set
                        return InitComms(VID, PID, Serial, true);
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
            status = ftdi_read_data_set_chunksize(&g_Device, xfer::USB_READPACKET_SIZE);
            if (status < 0)
            {
                // Step 13: Handle error if setting read chunk size fails
                std::cerr << "[InitComms] Set read chunksize error: " << ftdi_get_error_string(&g_Device) << std::endl;
                error = true;
            }

            // Step 14: Set the write chunk size for the FTDI device
            status = ftdi_write_data_set_chunksize(&g_Device, xfer::USB_WRITEPACKET_SIZE);
            if (status < 0)
            {
                // Step 15: Handle error if setting write chunk size fails
                std::cerr << "[InitComms] Set write chunksize error: " << ftdi_get_error_string(&g_Device) << std::endl;
                error = true;
            }

            // Step 16: Configure the FTDI device to reset bitmode
            status = ftdi_set_bitmode(&g_Device, 0x0, BITMODE_RESET);
            if (status < 0)
            {
                // Step 17: Handle error if bitmode configuration fails
                std::cerr << "[InitComms] Bitmode configuration error: " << ftdi_get_error_string(&g_Device) << std::endl;
                error = true;
            }

            // Step 18: Close the device if any error occurred
            if (error)
            {
                ftdi_usb_close(&g_Device);
                ftdi_deinit(&g_Device);
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
    int status = ftdi_tcioflush(&g_Device);
    if (status < 0)
    {
        // Step 3: Handle error if buffer purge fails
        std::cerr << "[CloseComms] Purge buffers error: " << ftdi_get_error_string(&g_Device) << std::endl;
    }
    // Step 4: Close the FTDI device
    ftdi_usb_close(&g_Device);
    ftdi_deinit(&g_Device);
    // Step 5: Log successful closure
    std::cout << "[CloseComms] FTDI device closed." << std::endl;
}

/**
 * @brief List available FTDI devices.
 */
void ListDevices(int vid, int pid)
{
    // Step 1: Find all connected FTDI devices matching the given VID and PID
    struct ftdi_device_list *devlist, *curdev;

    int status = ftdi_init(&g_Device);

    if (status < 0)
    {
        std::cerr << "Failed to initialize FTDI context\n";
        return;
    }

    status = ftdi_usb_find_all(&g_Device, &devlist, vid, pid);
    if (status < 0)
    {
        std::cerr << "[ListDevices] USB device listing error: " << ftdi_get_error_string(&g_Device) << std::endl;
        return;
    }
    else if (status == 0)
    {
        std::cout << "[ListDevices] No devices found." << std::endl;
        return;
    }
    else
    {
        std::cout << "[ListDevices] Found " << status << " device(s):" << std::endl;
    }

    // Step 2: Iterate over the device list and print each device's information
    curdev = devlist;
    while (curdev != nullptr)
    {
        char manufacturer[128], description[128], serial[128];
        int ret = ftdi_usb_get_strings(&g_Device, curdev->dev, manufacturer, 128, description, 128, serial, 128);
        if (ret < 0)
        {
            std::cerr << "Error getting device strings: " << ftdi_get_error_string(&g_Device) << std::endl;
        }
        else
        {
            std::cout << "[ListDevices]  Device:" << std::endl;
            std::cout << "  Manufacturer: " << manufacturer << std::endl;
            std::cout << "  Description: " << description << std::endl;
            std::cout << "  Serial: " << serial << std::endl;
            std::cout << "  Bus: " << static_cast<int>(libusb_get_bus_number(curdev->dev)) << std::endl;
            std::cout << "  Address: " << static_cast<int>(libusb_get_device_address(curdev->dev)) << std::endl;
            std::cout << "  Port: " << static_cast<int>(libusb_get_port_number(curdev->dev)) << std::endl;
        }
        curdev = curdev->next;
    }

    // Step 3: Free the device list
    if (devlist != nullptr)
    {
        ftdi_list_free(&devlist);
    }
}

/**
 * @brief Enter debug console mode, printing device output to stdout and acknowledging character consumption.
 */
void DoConsole(bool acknowledge)
{
    // setting std::cout to unbuffered mode
    // std::cout.setf(std::ios::unitbuf);
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
        status = ftdi_read_data(&g_Device, pFileBuffer.get(), RecvBufSize);

        //cdbg << "[DoConsole] Read status: " << status << " bytes" << std::endl;

        if (status < 0)
        {
            // Step 7: Handle error if reading data fails
            std::cerr << "[DoConsole] Read data error: " << ftdi_get_error_string(&g_Device) << std::endl;
        }
        else if (status == 0)
        {
            // No data received; sleep briefly to avoid busy-waiting
            // std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
                    std::cout << "    "; // Print four spaces for a tab
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
            //     int writeStatus = ftdi_write_data(&g_Device, &ackByte, 1);
            //     if (writeStatus < 0)
            //     {
            //         // Step 15: Handle error if sending acknowledgment fails
            //         std::cerr << "[DoConsole] Failed to send ACK: " << ftdi_get_error_string(&g_Device) << std::endl;
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
    // Step 2: Exit the program. The `atexit` handler registered in `main`
    // will call `CloseComms` to perform a clean shutdown. This avoids a
    // double-free error from calling cleanup logic both here and via atexit.
    std::exit(EXIT_SUCCESS);
}

} // namespace ftdi
