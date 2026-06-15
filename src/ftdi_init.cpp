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

#include <iostream>

#include "log.hpp"
#include "ftdi.hpp"
#include "xfer.hpp"

namespace ftdi {

/**
 * @brief Initialize FTDI device communication.
 * @param VID USB Vendor ID.
 * @param PID USB Product ID.
 * @return 1 on success, 0 on error.
 */
int InitComms(int VID, int PID, const std::string &Serial, bool recover)
{
    CDBG_LOG_ON_CHANGE("InitComms.begin",
                       "[InitComms][dbg] Begin init, recover=" << recover
                       << ", VID=0x" << std::hex << VID
                       << ", PID=0x" << PID
                       << ", serial='" << Serial << "'" << std::dec << std::endl);

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
    CDBG_LOG_ON_CHANGE("InitComms.ftdi_init",
                       "[InitComms][dbg] ftdi_init status=" << status << std::endl);
    bool error = false;
    bool contextInitialized = (status >= 0);
    bool deviceOpened = false;

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
        deviceOpened = (status >= 0 || status == -5);
        CDBG_LOG_ON_CHANGE("InitComms.ftdi_usb_open",
                           "[InitComms][dbg] ftdi_usb_open status=" << status << std::endl);

        if (status < 0 && status != -5)
        {
            // Step 5: Handle error if device cannot be opened (except for specific error -5)
            std::cerr << "[InitComms] Device open error: " << ftdi_get_error_string(&g_Device) << std::endl;
            error = true;
        }
        else
        {
            g_Device.usb_read_timeout = 500;
            g_Device.usb_write_timeout = 500;
            // Step 6: Purge the FTDI device buffers
            status = ftdi_tcioflush(&g_Device);
            CDBG_LOG_ON_CHANGE("InitComms.ftdi_tcioflush",
                               "[InitComms][dbg] ftdi_tcioflush status=" << status << std::endl);
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
                        deviceOpened = false;
                        ftdi_deinit(&g_Device);
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
            CDBG_LOG_ON_CHANGE("InitComms.read_chunksize",
                               "[InitComms][dbg] read chunksize=" << xfer::USB_READPACKET_SIZE
                               << ", status=" << status << std::endl);
            if (status < 0)
            {
                // Step 13: Handle error if setting read chunk size fails
                std::cerr << "[InitComms] Set read chunksize error: " << ftdi_get_error_string(&g_Device) << std::endl;
                error = true;
            }

            // Step 14: Set the write chunk size for the FTDI device
            status = ftdi_write_data_set_chunksize(&g_Device, xfer::USB_WRITEPACKET_SIZE);
            CDBG_LOG_ON_CHANGE("InitComms.write_chunksize",
                               "[InitComms][dbg] write chunksize=" << xfer::USB_WRITEPACKET_SIZE
                               << ", status=" << status << std::endl);
            if (status < 0)
            {
                // Step 15: Handle error if setting write chunk size fails
                std::cerr << "[InitComms] Set write chunksize error: " << ftdi_get_error_string(&g_Device) << std::endl;
                error = true;
            }

            // Step 16: Configure the FTDI device to reset bitmode
            status = ftdi_set_bitmode(&g_Device, 0x0, BITMODE_RESET);
            CDBG_LOG_ON_CHANGE("InitComms.set_bitmode",
                               "[InitComms][dbg] ftdi_set_bitmode(BITMODE_RESET) status=" << status << std::endl);
            if (status < 0)
            {
                // Step 17: Handle error if bitmode configuration fails
                std::cerr << "[InitComms] Bitmode configuration error: " << ftdi_get_error_string(&g_Device) << std::endl;
                error = true;
            }
        }
    }

    // Step 18: Close/deinitialize the device context if any error occurred.
    if (error)
    {
        if (deviceOpened)
        {
            ftdi_usb_close(&g_Device);
        }
        if (contextInitialized)
        {
            ftdi_deinit(&g_Device);
        }
    }

    // Step 19: Log successful initialization if no errors
    if (!error)
    {
        std::cout << "[InitComms] FTDI device initialized successfully." << std::endl;
        CDBG_LOG_ON_CHANGE("InitComms.complete",
                           "[InitComms][dbg] Init complete with timeouts r/w="
                           << g_Device.usb_read_timeout << "/" << g_Device.usb_write_timeout << std::endl);
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

} // namespace ftdi
