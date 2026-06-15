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

#include <libusb.h>

#include <iostream>

#include "ftdi.hpp"

namespace ftdi {

/**
 * @brief List available FTDI devices.
 */
void ListDevices(int vid, int pid)
{
    // Step 1: Find all connected FTDI devices matching the given VID and PID
    struct ftdi_device_list *devlist = nullptr, *curdev = nullptr;

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
        ftdi_deinit(&g_Device);
        return;
    }
    else if (status == 0)
    {
        std::cout << "[ListDevices] No devices found." << std::endl;
        ftdi_deinit(&g_Device);
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
    ftdi_deinit(&g_Device);
}

} // namespace ftdi
