/**
 * @file ftx.cpp
 * @brief Main entry point and CLI argument parsing.
 * @details Implements command-line interface for ftx utility with support for:
 *          - Device enumeration and selection (VID/PID/Serial)
 *          - Debug console mode
 *          - TCP proxy for debugger integration
 *          - Data transfer operations (upload, download, execute)
 *
 * @author Anders Montonen (original)
 * @copyright 2012, 2013, 2015 Anders Montonen
 * @license BSD 2-Clause
 */

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
#include <iomanip>
#include <string>
#include <csignal>
#include <cstring>

#ifdef _WIN32
// Dummy strsignal for Windows
const char* strsignal(int sig) {
    static char buf[32];
    sprintf(buf, "Signal %d (Windows)", sig);
    return buf;
}
#endif

#if defined(FTX_DEBUG_BUILD) && !defined(_WIN32)
#include <unistd.h>
#include <execinfo.h>
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "ftdi.hpp"
#include "xfer.hpp"
#include "crc.hpp"
#include <fstream>



bool g_verbose = false;
int g_verbose_level = 0;

const int VID = 0x0403;
const int PID = 0x6001;

/**
 * @brief Signal handler for critical errors that should cause a core dump.
 * @param sig The signal number.
 */
void CoreDumpSignalHandler(int sig) {
    std::cerr << "\nCritical error: Caught signal " << sig << " (" << strsignal(sig) << ").\n";

#if defined(FTX_DEBUG_BUILD) && !defined(_WIN32)
    std::cerr << "Call stack:\n";
    const int MAX_STACK_FRAMES = 128;
    void *buffer[MAX_STACK_FRAMES];
    int nptrs = backtrace(buffer, MAX_STACK_FRAMES);
    backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
#else
    std::cerr << "Program was not built in Debug mode (or running on Windows). No call stack available.\n";
#endif

    // Unregister our handler and fall back to the default action
    signal(sig, SIG_DFL);

    // Re-raise the signal to trigger the default handler (which should include core dump)
    raise(sig);
}

/**
 * @brief Print usage instructions for the CLI.
 * @param progName Name of the program (argv[0]).
 */
void PrintUsage(const char* progName) {
    // Use Boost.Filesystem to get the program name only
    std::string prog = boost::filesystem::path(progName).filename().string();

#ifdef FTX_VERSION
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
    std::cout << prog << " version " << TOSTRING(FTX_VERSION) << "\n";
#endif

    std::cout << "Usage: " << prog << " [-options] [-commands]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --vid <VID>                   Device VID (Default 0x0403)\n";
    std::cout << "  --pid <PID>                   Device PID (Default 0x6001)\n";
    std::cout << "  -s  <Serial>                  Device Serial (Default : Will match VID and PID with an FTDI serial)\n";
    std::cout << "  -t, --terminal                Run terminal mode (bidirectional)\n";
    std::cout << "  -c, --console                 Run debug console (read-only)\n";
    std::cout << "  -g  [port]                    Run raw TCP<->FTDI proxy (Default port 1234)\n";
    std::cout << "  -v                            Output GDB commands\n";
    std::cout << "  -vv                           Output GDB commands and all dbg execution traces\n";
    std::cout << "  -l                            List available FTDI devices\n";
    std::cout << "  -h, --help                    Help\n\n";
    std::cout << "Commands:\n";
    std::cout << "  -d  <file>  <address>  <size> Download data to file\n";
    std::cout << "  -u  <file>  <address>         Upload data from file\n";
    std::cout << "  -x  <file>  <address>         Upload program and execute\n";
    std::cout << "  -r  <address>                 Execute program (Does not work !)\n";
    std::cout << "  -D, --dump <file>             Dump BIOS to file\n\n";
    std::cout << "  --ls <path>                   List files and directories\n";
    std::cout << "  --rm <path>                   Remove a file or empty directory\n";
    std::cout << "  --mkdir <path>                Create a directory\n";
    std::cout << "  --rmdir <path>                Delete a directory\n";
    std::cout << "  --cp <file> <target>          Copy a file to a raw SD range (sdraw:start:count) or FAT filesystem path (/path)\n";
    std::cout << "  --crc <file>                  Print CRC-8 for a file\n";
    std::cout << "  --lcrc <file>                 Print CRC-8 for a local host file\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << prog << " -d data.bin 0x200000 0x10000\n";
    std::cout << "  " << prog << " -u data.bin 0x200000\n";
    std::cout << "  " << prog << " -x prog.bin 0x200000\n";
    std::cout << "  " << prog << " -r 0x200000\n";
    std::cout << "  " << prog << " -t\n";
    std::cout << "  " << prog << " -c\n";
    std::cout << "  " << prog << " --ls cd/data\n";
    std::cout << "  " << prog << " --rm old.bin\n";
    std::cout << "  " << prog << " --crc boot.bin\n";
}

/**
 * @brief Struct to hold parsed command line arguments.
 */
struct CommandLineArgs {
    int vid = VID; ///< USB Vendor ID
    int pid = PID; ///< USB Product ID
    std::string serial = ""; ///< Device Serial
    bool terminal = false; ///< Run terminal mode (bidirectional)
    bool console = false; ///< Run debug console (read-only)
    bool tcp_proxy = false; ///< Run raw TCP proxy
    uint16_t tcp_port = 1234; ///< TCP proxy port
    int verbose_level = 0; ///< Verbose tracing level (0=none, 1=GDB, 2=all)
    enum CommandType { NONE, DOWNLOAD, UPLOAD, EXEC, RUN, DUMP, LIST_DEVICES, LS, RM, CRC, CP, LCRC, MKDIR, RMDIR } command = NONE; ///< Command type
    std::string filename; ///< File name for transfer
    std::string target; ///< Target path for copy operations
    unsigned int address = 0; ///< Address for transfer/execute
    unsigned int length = 0; ///< Length for download
};

/**
 * @brief Parse command line arguments using Boost.Program_options.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Parsed CommandLineArgs struct.
 */
CommandLineArgs parse_args(int argc, char* argv[]) {
    namespace po = boost::program_options;
    CommandLineArgs args;
    po::options_description desc("Allowed options");
    desc.add_options()
        ("vid", po::value<std::string>(), "Device VID (hex) [optional]")
        ("pid", po::value<std::string>(), "Device PID (hex) [optional]")
        ("serial,s", po::value<std::string>(), "Device Serial (Default : Will match VID and PID with an FTDI serial) [optional]")
        ("l,l", "List available FTDI devices")
        ("terminal,t", "Run terminal mode (bidirectional)")
        ("console,c", "Run debug console (read-only)")
        ("g,g", po::value<std::string>()->implicit_value("1234"), "Run raw TCP proxy [optional port]")
        ("verbose_level", po::value<int>(), "Verbose level")
        ("v", "Output GDB commands")
        ("vv", "Output GDB commands and dbg execution traces")
        ("verbose", "Output GDB commands and dbg execution traces (alias for vv)")
        ("d,d", po::value<std::vector<std::string>>()->multitoken(), "Download: <file> <address> <size>")
        ("u,u", po::value<std::vector<std::string>>()->multitoken(), "Upload: <file> <address>")
        ("x,x", po::value<std::vector<std::string>>()->multitoken(), "Exec: <file> <address>")
        ("r,r", po::value<std::string>(), "Run: <address>")
        ("ls", po::value<std::string>()->implicit_value("/"), "List files and directories: <path>")
        ("rm", po::value<std::string>(), "Remove a file or empty directory: <path>")
        ("mkdir", po::value<std::string>(), "Create a directory: <path>")
        ("rmdir", po::value<std::string>(), "Delete a directory: <path>")
        ("cp", po::value<std::vector<std::string>>()->multitoken(), "Copy: <file> <sdraw:start:count>")
        ("crc", po::value<std::string>(), "Print CRC-8 for a file: <file>")
        ("lcrc", po::value<std::string>(), "Print CRC-8 for a local file: <file>")
        ("help,h", "Help: Show this help message")
        ("dump,D", po::value<std::string>(), "Dump BIOS: <file>");

    po::variables_map vm;

    try {
        auto parsed = po::command_line_parser(argc, argv)
                  .options(desc)
                  .style(po::command_line_style::unix_style |
                         po::command_line_style::allow_short |
                         po::command_line_style::allow_long_disguise)
                  .run();

        po::store(parsed, vm);
        po::notify(vm);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing command line: " << e.what() << std::endl;
        std::cout << desc << std::endl;
        exit(EXIT_FAILURE);
    }

    try {
        if (vm.count("help") || vm.count("h")) {
            PrintUsage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        if (vm.count("vid")) {
            args.vid = std::stoi(vm["vid"].as<std::string>(), nullptr, 16);
        }
        if (vm.count("pid")) {
            args.pid = std::stoi(vm["pid"].as<std::string>(), nullptr, 16);
        }
        if (vm.count("serial")) {
            args.serial = vm["serial"].as<std::string>();
        }
        if (vm.count("l")) {
            args.command = CommandLineArgs::LIST_DEVICES;
        }
        if (vm.count("ls")) {
            args.command = CommandLineArgs::LS;
            if (vm.count("l")) {
                args.filename = std::string("-l ") + vm["ls"].as<std::string>();
            } else {
                args.filename = vm["ls"].as<std::string>();
            }
        } else if (vm.count("rm")) {
            args.command = CommandLineArgs::RM;
            args.filename = vm["rm"].as<std::string>();
        } else if (vm.count("mkdir")) {
            args.command = CommandLineArgs::MKDIR;
            args.filename = vm["mkdir"].as<std::string>();
        } else if (vm.count("rmdir")) {
            args.command = CommandLineArgs::RMDIR;
            args.filename = vm["rmdir"].as<std::string>();
        } else if (vm.count("cp")) {
            auto vals = vm["cp"].as<std::vector<std::string>>();
            if (vals.size() == 2) {
                args.command = CommandLineArgs::CP;
                args.filename = vals[0];
                args.target = vals[1];
            }
        } else if (vm.count("crc")) {
            args.command = CommandLineArgs::CRC;
            args.filename = vm["crc"].as<std::string>();
        } else if (vm.count("lcrc")) {
            args.command = CommandLineArgs::LCRC;
            args.filename = vm["lcrc"].as<std::string>();
        }
        if (vm.count("terminal") || vm.count("t")) {
            args.terminal = true;
        }
        if (vm.count("console") || vm.count("c")) {
            args.console = true;
        }
        if (vm.count("g")) {
            args.tcp_proxy = true;
            std::string port_str = vm["g"].as<std::string>();
            try {
                args.tcp_port = static_cast<uint16_t>(std::stoul(port_str, nullptr, 0));
            } catch (const std::exception& e) {
                std::cerr << "Error parsing GDB port: " << e.what() << std::endl;
                args.tcp_port = 1234;
            }
        }
        if (vm.count("vv") || vm.count("verbose")) {
            args.verbose_level = 2;
        } else if (vm.count("v")) {
            args.verbose_level = 1;
        } else if (vm.count("verbose_level")) {
            args.verbose_level = vm["verbose_level"].as<int>();
        }
        if (vm.count("d")) {
            auto vals = vm["d"].as<std::vector<std::string>>();
            if (vals.size() == 3) {
                args.command = CommandLineArgs::DOWNLOAD;
                args.filename = vals[0];
                args.address = std::stoul(vals[1], nullptr, 0);
                args.length = std::stoul(vals[2], nullptr, 0);
            }
        } else if (vm.count("u")) {
            auto vals = vm["u"].as<std::vector<std::string>>();
            if (vals.size() == 2) {
                args.command = CommandLineArgs::UPLOAD;
                args.filename = vals[0];
                args.address = std::stoul(vals[1], nullptr, 0);
            }
        } else if (vm.count("x")) {
            auto vals = vm["x"].as<std::vector<std::string>>();
            if (vals.size() == 2) {
                args.command = CommandLineArgs::EXEC;
                args.filename = vals[0];
                args.address = std::stoul(vals[1], nullptr, 0);
            }
        } else if (vm.count("r")) {
            args.command = CommandLineArgs::RUN;
            args.address = std::stoul(vm["r"].as<std::string>(), nullptr, 0);
        }
        else if (vm.count("dump")) {
            args.command = CommandLineArgs::DUMP;
            args.filename = vm["dump"].as<std::string>();
        }
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: Invalid numeric argument provided." << std::endl;
        exit(EXIT_FAILURE);
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: Numeric argument out of range." << std::endl;
        exit(EXIT_FAILURE);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    return args;
}

/**
 * @brief Main entry point for the Sega Saturn USB flash cart transfer utility.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code.
 */
int main(int argc, char *argv[])
{
    CommandLineArgs args = parse_args(argc, argv);
    g_verbose_level = args.verbose_level;
    g_verbose = (g_verbose_level >= 2);

    if (args.command == CommandLineArgs::NONE && !args.terminal && !args.console && !args.tcp_proxy) {
        PrintUsage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (args.command == CommandLineArgs::LIST_DEVICES) {
        ftdi::ListDevices(args.vid, args.pid);
        exit(EXIT_SUCCESS);
    }

    if (args.command == CommandLineArgs::LCRC) {
        std::ifstream file(args.filename, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open local file: " << args.filename << std::endl;
            exit(EXIT_FAILURE);
        }
        crc8::crc_t crc = 0;
        char buffer[4096];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            crc = crc8::crc_update(crc, reinterpret_cast<const unsigned char*>(buffer), static_cast<std::size_t>(file.gcount()));
        }
        std::cout << "[DoCrc] " << args.filename << " CRC-8 = 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(crc) << std::dec << std::endl;
        exit(EXIT_SUCCESS);
    }

    if (ftdi::InitComms(args.vid, args.pid, args.serial)) {
        atexit(ftdi::CloseComms);
        signal(SIGINT, ftdi::Signal);
        signal(SIGTERM, ftdi::Signal);
    #ifndef _WIN32
        signal(SIGQUIT, ftdi::Signal);
        signal(SIGBUS, CoreDumpSignalHandler);
    #endif
        signal(SIGSEGV, CoreDumpSignalHandler);
        signal(SIGABRT, CoreDumpSignalHandler);
        signal(SIGFPE, CoreDumpSignalHandler);
        signal(SIGILL, CoreDumpSignalHandler);

        int status = 1;
        switch (args.command) {
            case CommandLineArgs::DOWNLOAD:
                status = xfer::DoDownload(args.filename.c_str(), args.address, args.length);
                break;
            case CommandLineArgs::UPLOAD:
                status = xfer::DoUpload(args.filename.c_str(), args.address);
                break;
            case CommandLineArgs::EXEC:
                status = xfer::DoExecute(args.filename.c_str(), args.address);
                break;
            case CommandLineArgs::RUN:
                status = xfer::DoRun(args.address);
                break;
            case CommandLineArgs::DUMP:
                status = xfer::DoBiosDump(args.filename.c_str());
                break;
            case CommandLineArgs::LS:
                status = xfer::DoList(args.filename.c_str());
                break;
            case CommandLineArgs::RM:
                status = xfer::DoRemove(args.filename.c_str());
                break;
            case CommandLineArgs::MKDIR:
                status = xfer::DoMkdir(args.filename.c_str());
                break;
            case CommandLineArgs::RMDIR:
                status = xfer::DoRmdir(args.filename.c_str());
                break;
            case CommandLineArgs::CP:
                status = xfer::DoSdUpload(args.filename.c_str(), args.target.c_str());
                break;
            case CommandLineArgs::CRC:
                status = xfer::DoCrc(args.filename.c_str());
                break;
            default:
                break;
        }

        if (status == 0) {
            return EXIT_FAILURE;
        }
        
        if (args.tcp_proxy) {
            return ftdi::DoTcpProxy(args.tcp_port, (g_verbose_level >= 1));
        }
        if (args.terminal) {
            ftdi::DoConsole(true);
        }
        if (args.console) {
            ftdi::DoConsole(false);
        }
    } else {
        return EXIT_FAILURE;
    }
 
    return EXIT_SUCCESS;
}
