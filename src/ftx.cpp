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

#ifdef FTX_DEBUG_BUILD
#include <unistd.h>
#include <execinfo.h>
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "ftdi.hpp"
#include "xfer.hpp"



const int VID = 0x0403;
const int PID = 0x6001;

/**
 * @brief Signal handler for critical errors that should cause a core dump.
 * @param sig The signal number.
 */
void CoreDumpSignalHandler(int sig) {
    std::cerr << "\nCritical error: Caught signal " << sig << " (" << strsignal(sig) << ").\n";

#ifdef FTX_DEBUG_BUILD
    std::cerr << "Call stack:\n";
    const int MAX_STACK_FRAMES = 128;
    void *buffer[MAX_STACK_FRAMES];
    int nptrs = backtrace(buffer, MAX_STACK_FRAMES);
    backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
#else
    std::cerr << "Program was not built in Debug mode. No call stack available.\n";
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
    std::cout << "Usage: " << prog << " [-options] [-commands]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -v  <VID>                     Device VID (Default 0x0403)\n";
    std::cout << "  -p  <PID>                     Device PID (Default 0x6001)\n";
    std::cout << "  -s  <Serial>                  Device Serial (Default : Will match VID and PID with an FTDI serial)\n";
    std::cout << "  -c                            Run debug console\n";
    std::cout << "  -l                            List available FTDI devices\n";
    std::cout << "  -help                         Help\n\n";
    std::cout << "Commands:\n";
    std::cout << "  -d  <file>  <address>  <size> Download data to file\n";
    std::cout << "  -u  <file>  <address>         Upload data from file\n";
    std::cout << "  -x  <file>  <address>         Upload program and execute\n";
    std::cout << "  -r  <address>                 Execute program (Does not work !)\n";
    std::cout << "  -dump  <file>                 Dump BIOS to file\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << prog << " -d data.bin 0x200000 0x10000\n";
    std::cout << "  " << prog << " -u data.bin 0x200000\n";
    std::cout << "  " << prog << " -x prog.bin 0x200000\n";
    std::cout << "  " << prog << " -r 0x200000\n";
    std::cout << "  " << prog << " -c\n";
}

/**
 * @brief Struct to hold parsed command line arguments.
 */
struct CommandLineArgs {
    int vid = VID; ///< USB Vendor ID
    int pid = PID; ///< USB Product ID
    std::string serial = ""; ///< Device Serial
    bool console = false; ///< Run debug console
    enum CommandType { NONE, DOWNLOAD, UPLOAD, EXEC, RUN, DUMP, LIST } command = NONE; ///< Command type
    std::string filename; ///< File name for transfer
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
        ("v,v", po::value<std::string>(), "Device VID (hex) [optional]")
        ("p,p", po::value<std::string>(), "Device PID (hex) [optional]")
        ("s,s", po::value<std::string>(), "Device Serial (Default : Will match VID and PID with an FTDI serial) [optional]")
        ("l,l", "List available FTDI devices")
        ("c,c", "Run debug console")
        ("d,d", po::value<std::vector<std::string>>()->multitoken(), "Download: <file> <address> <size>")
        ("u,u", po::value<std::vector<std::string>>()->multitoken(), "Upload: <file> <address>")
        ("x,x", po::value<std::vector<std::string>>()->multitoken(), "Exec: <file> <address>")
        ("r,r", po::value<std::string>(), "Run: <address>")
        ("help,h", "Help: Show this help message")
        ("dump,D", po::value<std::string>(), "Dump BIOS: <file>");

    po::variables_map vm;

    try {
        auto parsed = po::command_line_parser(argc, argv)
                  .options(desc)
                  .style(po::command_line_style::unix_style |
                         po::command_line_style::allow_short)
                  .run();

        po::store(parsed, vm);
        po::notify(vm);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing command line: " << e.what() << std::endl;
        std::cout << desc << std::endl;
        exit(EXIT_FAILURE);
    }

    if (vm.count("v")) {
        args.vid = std::stoi(vm["v"].as<std::string>(), nullptr, 16);
    }
    if (vm.count("p")) {
        args.pid = std::stoi(vm["p"].as<std::string>(), nullptr, 16);
    }
    if (vm.count("s")) {
        args.serial = vm["s"].as<std::string>();
    }
    if (vm.count("l")) {
        args.command = CommandLineArgs::LIST;
    }
    if (vm.count("c")) {
        args.console = true;
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
    else if (vm.count("help") || vm.count("h")) {
        args.command = CommandLineArgs::NONE;
    } 
    else if (vm.count("dump")) {
        args.command = CommandLineArgs::DUMP;
        args.filename = vm["dump"].as<std::string>();
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

    if (args.command == CommandLineArgs::NONE && !args.console) {
        PrintUsage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (args.command == CommandLineArgs::LIST) {
        ftdi::ListDevices(args.vid, args.pid);
        exit(EXIT_SUCCESS);
    }

    if (ftdi::InitComms(args.vid, args.pid, args.serial)) {
        atexit(ftdi::CloseComms);
        signal(SIGINT, ftdi::Signal);
        signal(SIGTERM, ftdi::Signal);
    #ifndef _WIN32
        signal(SIGQUIT, ftdi::Signal);
    #endif
        signal(SIGSEGV, CoreDumpSignalHandler);
        signal(SIGABRT, CoreDumpSignalHandler);
        signal(SIGFPE, CoreDumpSignalHandler);
        signal(SIGILL, CoreDumpSignalHandler);
        signal(SIGBUS, CoreDumpSignalHandler);

        switch (args.command) {
            case CommandLineArgs::DOWNLOAD:
                xfer::DoDownload(args.filename.c_str(), args.address, args.length);
                break;
            case CommandLineArgs::UPLOAD:
                xfer::DoUpload(args.filename.c_str(), args.address);
                break;
            case CommandLineArgs::EXEC:
                xfer::DoExecute(args.filename.c_str(), args.address);
                break;
            case CommandLineArgs::RUN:
                xfer::DoRun(args.address);
                break;
            case CommandLineArgs::DUMP:
                xfer::DoBiosDump(args.filename.c_str());
                break;
            default:
                break;
        }
        if (args.console) {
            ftdi::DoConsole();
        }
    }
 
    return EXIT_SUCCESS;
}
