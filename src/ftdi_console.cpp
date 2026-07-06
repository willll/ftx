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

#include <cctype>
#include <chrono>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include "log.hpp"
#include "ftdi.hpp"

namespace ftdi {

/**
 * @brief Enter debug console mode, printing device output to stdout and acknowledging character consumption.
 */
void DoConsole(bool enable_stdin, bool acknowledge)
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
    CDBG_LOG_ON_CHANGE("DoConsole.acknowledge",
                       "[DoConsole][dbg] acknowledge=" << acknowledge
                       << ", ackByte=0x" << std::hex << static_cast<int>(ackByte) << std::dec << std::endl);

    std::mutex stdinLineQueueMutex;
    std::deque<std::string> stdinLineQueue;
    std::thread stdinReaderThread;

    if (enable_stdin) {
        stdinReaderThread = std::thread([&]() {
            std::string pendingLine;
        cdbg << "[DoConsole][dbg] stdin reader thread started" << std::endl;

        // Use a timed wait on stdin so the thread can exit quickly when
        // g_interrupt_flag is set. The implementation is platform-specific:
        // POSIX uses poll()+read(), Windows uses WaitForSingleObject()+ReadFile().
#ifdef _WIN32
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
#endif
        while (!g_interrupt_flag)
        {
            unsigned char input = 0;

#ifdef _WIN32
            DWORD waitResult = WaitForSingleObject(hStdin, 50);
            if (waitResult == WAIT_FAILED)
            {
                cdbg << "[DoConsole][dbg] stdin poll failed, stopping reader thread" << std::endl;
                g_interrupt_flag = true;
                break;
            }

            if (waitResult == WAIT_TIMEOUT)
            {
                continue;
            }

            DWORD bytesRead = 0;
            if (!ReadFile(hStdin, &input, 1, &bytesRead, NULL))
            {
                cdbg << "[DoConsole][dbg] stdin read failed, stopping reader thread" << std::endl;
                g_interrupt_flag = true;
                break;
            }

            if (bytesRead == 0)
            {
                cdbg << "[DoConsole][dbg] stdin EOF, stopping reader thread" << std::endl;
                g_interrupt_flag = true;
                break;
            }
#else
            struct pollfd stdinPollFd;
            stdinPollFd.fd = STDIN_FILENO;
            stdinPollFd.events = POLLIN;
            stdinPollFd.revents = 0;

            const int pollStatus = poll(&stdinPollFd, 1, 50);
            if (pollStatus < 0)
            {
                cdbg << "[DoConsole][dbg] stdin poll failed, stopping reader thread" << std::endl;
                g_interrupt_flag = true;
                break;
            }

            if (pollStatus == 0)
            {
                continue;
            }

            if ((stdinPollFd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            {
                cdbg << "[DoConsole][dbg] stdin poll signaled error/hup, stopping reader thread" << std::endl;
                g_interrupt_flag = true;
                break;
            }

            if ((stdinPollFd.revents & POLLIN) == 0)
            {
                continue;
            }

            const ssize_t readStatus = read(STDIN_FILENO, &input, 1);
            if (readStatus == 0)
            {
                cdbg << "[DoConsole][dbg] stdin EOF, stopping reader thread" << std::endl;
                g_interrupt_flag = true;
                break;
            }

            if (readStatus < 0)
            {
                cdbg << "[DoConsole][dbg] stdin read failed, stopping reader thread" << std::endl;
                g_interrupt_flag = true;
                break;
            }
#endif

            cdbg << "[DoConsole][dbg] stdin got byte=0x" << std::hex << std::setw(2)
                 << std::setfill('0') << static_cast<int>(input) << std::dec << std::endl;

            if (input == 0x03)
            {
                cdbg << "[DoConsole][dbg] Ctrl+C received from stdin" << std::endl;
                g_interrupt_flag = true;
                pendingLine.clear();
                break;
            }

            const unsigned char normalized = (input == '\r') ? static_cast<unsigned char>('\n') : input;
            pendingLine.push_back(static_cast<char>(normalized));

            if (normalized == '\n')
            {
                std::size_t queueDepth = 0;
                {
                    std::lock_guard<std::mutex> lock(stdinLineQueueMutex);
                    stdinLineQueue.push_back(pendingLine);
                    queueDepth = stdinLineQueue.size();
                }

                cdbg << "[DoConsole][dbg] stdin line enqueued, bytes=" << pendingLine.size()
                     << ", queueDepth=" << queueDepth << std::endl;
                pendingLine.clear();
            }
        }

        cdbg << "[DoConsole][dbg] stdin reader thread exiting" << std::endl;
        });
    }

    // Step 5: Continuously read data from the device
    while (status >= 0 && !g_interrupt_flag)
    {
        // Step 6: Read data into the buffer
        status = ftdi_read_data(&g_Device, pFileBuffer.get(), RecvBufSize);
        if (status != 0)
        {
            cdbg << "[DoConsole][dbg] ftdi_read_data status=" << status << std::endl;
        }

        if (status < 0 && !g_interrupt_flag)
        {
            // Step 7: Handle error if reading data fails
            std::cerr << "[DoConsole] Read data error: " << ftdi_get_error_string(&g_Device) << std::endl;
            g_interrupt_flag = true;
            break;
        }
        else if (status > 0)
        {

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

        // Send complete stdin lines gathered by the input thread.
        bool sentAnyStdinLine = false;
        std::deque<std::string> linesToSend;
        if (enable_stdin) {
            std::lock_guard<std::mutex> lock(stdinLineQueueMutex);
            if (!stdinLineQueue.empty())
            {
                linesToSend.swap(stdinLineQueue);
            }
        }

        if (!linesToSend.empty())
        {
            CDBG_LOG_ON_CHANGE("DoConsole.stdin_dequeue",
                               "[DoConsole][dbg] dequeued stdin lines=" << linesToSend.size() << std::endl);
        }

        for (const std::string &line : linesToSend)
        {
            if (line.empty())
            {
                continue;
            }

            sentAnyStdinLine = true;
            std::vector<unsigned char> outBuffer(line.begin(), line.end());
            const int outCount = static_cast<int>(outBuffer.size());
            int sent = 0;

            CDBG_LOG_ON_CHANGE("DoConsole.stdin_forward",
                               "[DoConsole][dbg] forwarding stdin line bytes=" << outCount << std::endl);

            while (sent < outCount && !g_interrupt_flag)
            {
                const int writeStatus = ftdi_write_data(&g_Device, outBuffer.data() + sent, outCount - sent);
                if (writeStatus < 0)
                {
                    std::cerr << "[DoConsole] Stdin forward error: " << ftdi_get_error_string(&g_Device) << std::endl;
                    g_interrupt_flag = true;
                    break;
                }
                if (writeStatus == 0)
                {
                    CDBG_LOG_ON_CHANGE("DoConsole.write_zero",
                                       "[DoConsole][dbg] ftdi_write_data returned 0, breaking send loop" << std::endl);
                    break;
                }
                sent += writeStatus;
                CDBG_LOG_ON_CHANGE("DoConsole.write_progress",
                                   "[DoConsole][dbg] ftdi_write_data wrote=" << writeStatus
                                   << ", totalSent=" << sent << std::endl);
            }

            CDBG_LOG_ON_CHANGE("DoConsole.stdin_line_sent",
                               "[DoConsole][dbg] stdin line send complete, requested=" << outCount
                               << ", sent=" << sent << std::endl);
        }

        if (status == 0 && !sentAnyStdinLine)
        {
            // Avoid busy-looping when neither USB nor stdin line queue has data.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (stdinReaderThread.joinable())
    {
        cdbg << "[DoConsole][dbg] waiting for stdin reader thread to join" << std::endl;
        stdinReaderThread.join();
        cdbg << "[DoConsole][dbg] stdin reader thread joined" << std::endl;
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
    // Step 2: Set the global interrupt flag to signal the main loop to exit.
    // This allows for a graceful shutdown, preventing race conditions and
    // ensuring cleanup is handled correctly by the main program flow.
    g_interrupt_flag = true;
}

} // namespace ftdi
