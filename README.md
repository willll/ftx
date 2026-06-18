[![Build Ftx Windows](https://github.com/willll/ftx/actions/workflows/build-windows.yml/badge.svg)](https://github.com/willll/ftx/actions/workflows/build-windows.yml) | [![Build Ftx Linux](https://github.com/willll/ftx/actions/workflows/build-linux.yml/badge.svg)](https://github.com/willll/ftx/actions/workflows/build-linux.yml)

# Sega Saturn USB Flash Cart Transfer Utility


This project provides a modern C++17 command-line utility for transferring data to and from a Sega Saturn USB flash cartridge. It supports downloading, uploading, and executing binaries on the device, with robust error handling, execution tracing, and cross-platform support via CMake.

**Attribution:** This project is based on the original work by [andersm/usbcart](https://github.com/andersm/usbcart/tree/master/ftx).

## Features

- Download data from the device to a file
- Upload data from a file to the device
- Upload and execute a program on the device
- Execute a program at a specified address
- Debug console mode for device output
- Command-line interface with argument parsing using Boost.Program_options
- Modern C++17 codebase with Doxygen documentation
- FTDI USB communication (libftdi)

## Requirements

- C++17 compatible compiler
- CMake 3.10+
- Boost (program_options, filesystem)
- libftdi1

## Building

### Linux

#### Debug (default)

```sh
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

#### Release

```sh
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

#### Debug vs Release

- Debug builds (`-DCMAKE_BUILD_TYPE=Debug`) keep debug traces enabled.
- Release builds (`-DCMAKE_BUILD_TYPE=Release`) define `NDEBUG`, so debug traces from `cdbg` and `CDBG_LOG_ON_CHANGE` are compiled out.
- You can also force `NDEBUG` explicitly with `-DNDEBUG=ON` (independent of build type).

#### Static Build (Linux only)

Build a statically-linked executable by passing `-DBUILD_STATIC=ON`:

```sh
mkdir -p build
cd build
cmake -DBUILD_STATIC=ON ..
make
```

**Note:** A fully static build requires all dependencies to have static libraries available (e.g., `libboost-program-options.a`, `libboost-filesystem.a`, `libftdi1.a`, etc.). If static versions are unavailable, the build will automatically fall back to dynamic linking for those libraries with a warning. Most Linux distributions do not provide static Boost or libudev libraries by default.

To ensure a fully static build on Debian/Ubuntu, install static development packages:

```sh
sudo apt install libboost-program-options-dev libboost-filesystem-dev libftdi1-dev libudev-dev
```

Then configure with `-DBUILD_STATIC=ON` as shown above.

### MS Windows 

```sh
mkdir -p build
cd build
cmake ..
make
```

## Usage

```sh
./ftx [options] [commands]
```

### Options

- `-v <VID>`: Device VID (default: 0x0403)
- `-p <PID>`: Device PID (default: 0x6001)
- `-c`      : Run debug console
- `-g [port]`: Run raw TCP<->FTDI proxy (default port: 1234)
- `-verbose`: Print traced RSP packets as `GDB>...` and `Target>...` lines

### Commands

- `-d <file> <address> <size>`: Download data from device to file
- `-u <file> <address>`      : Upload data from file to device
- `-x <file> <address>`      : Upload program and execute
- `-r <address>`             : Execute program at address

### Examples

Download 64KB from address 0x200000 to `data.bin`:

```sh
./ftx -d data.bin 0x200000 0x10000
```

Upload `data.bin` to address 0x200000:

```sh
./ftx -u data.bin 0x200000
```

Upload and execute `prog.bin` at address 0x200000:

```sh
./ftx -x prog.bin 0x200000
```

Execute program at address 0x200000:

```sh
./ftx -r 0x200000
```

Start debug console:

```sh
./ftx -c
```

## TCP Proxy Mode

`-g` starts a raw TCP proxy. All bytes received from the TCP client are forwarded directly to the FTDI device, and all bytes read from FTDI are forwarded back to the TCP client.

Start the proxy on the default port (1234):

```sh
./ftx -g
```

Start the proxy with verbose packet tracing:

```sh
./ftx -g 1234 -verbose
```

Or on a custom port:

```sh
./ftx -g 2345
```

Example with netcat:

```sh
nc 127.0.0.1 1234
```

Use this mode when your client already speaks the cartridge/debug protocol and you only need transport bridging over TCP.

When `-verbose` is enabled, traced packet lines are printed with one packet per line:

- `GDB>$...` for packets received from the TCP client
- `Target>$...` for packets received from the FTDI target

### Debug Traces

The project includes extensive debug tracing. When built in Debug mode, traces are emitted to stdout with the `[DoConsole][dbg]` prefix. Key traces include:

- `stdin reader thread started/exiting` — thread lifecycle events
- `stdin got byte=0xNN` — every byte received from terminal input
- `stdin line enqueued` — complete line ready to send to device
- `ftdi_read_data status=N` — device reads (0 = no data, >0 = bytes read, <0 = error)
- `ftdi_write_data wrote=N` — successful write chunks to device

TCP proxy mode also emits traces with `[TCPProxy][dbg]`, including:

- client connect/disconnect and file descriptors
- socket/FTDI byte counts per transfer
- FTDI chunk write retries and flush/retry events

To see these traces, build with `-DCMAKE_BUILD_TYPE=Debug`:

```sh
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

To build with `NDEBUG` defined, configure with `-DNDEBUG=ON`:

```sh
cmake -DNDEBUG=ON ..
make
```

In release builds (`-DCMAKE_BUILD_TYPE=Release`) or when configured with `-DNDEBUG=ON`, debug traces are compiled out via the `NDEBUG` macro.

### Error Handling

- Device initialization includes automatic recovery attempts on buffer purge failures.
- On console exit (Ctrl+C), the stdin reader thread stops within 50ms and the process exits cleanly.
- Read/write errors to the device are reported to stderr and terminate console mode.

## Project Structure

The source is organized into focused modules:

- **src/ftdi.cpp** — Shared FTDI device context and global interrupt flag
- **src/ftdi_init.cpp** — Device initialization, configuration, and cleanup (`InitComms`, `CloseComms`)
- **src/ftdi_discovery.cpp** — Device enumeration (`ListDevices`)
- **src/ftdi_console.cpp** — Interactive console mode and signal handling (`DoConsole`, `Signal`)
- **src/ftx.cpp** — Main entry point and command-line parsing
- **src/xfer.cpp** — Data transfer operations (upload, download, execute)
- **src/crc.cpp** — CRC-8 checksum computation
- **include/log.hpp** — Deduplicating debug logger with release-mode no-op

### Build System

The project uses CMake 3.10+ with the following features:

- `CMP0167` policy enabled for modern Boost discovery
- Automatic static/dynamic linking detection for Linux
- Separate compile_commands.json generation for static analysis tools (cppcheck)
- C++17 standard requirement with feature-based target setup

In console mode, communication is bidirectional on supported terminals:

- Device output is printed to stdout.
- Typed host input is buffered line-by-line (press Enter to send a complete line).
- The stdin reader runs in a dedicated thread with a 50ms poll timeout, allowing clean exit on Ctrl+C.
- Non-printable device output is displayed in hex notation (e.g., `[0xfe]`).

## Code Documentation

Comprehensive Doxygen documentation is available for developers and API users.

### Generating Documentation

**Prerequisites:** Doxygen must be installed:

```sh
# Ubuntu/Debian
sudo apt install doxygen

# macOS
brew install doxygen

# Windows
# Download from https://www.doxygen.nl/download.html
```

### Building Documentation

From the project root:

```sh
doxygen
```

This generates HTML documentation in `docs/html/`. Open `docs/html/index.html` in a browser.

### Or via CMake (optional)

To add a `docs` build target, add this to CMakeLists.txt:

```cmake
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(docs 
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Doxygen documentation"
        VERBATIM
    )
endif()
```

Then build docs with:

```sh
cmake --build . --target docs
```

### Documentation Contents

The generated documentation includes:

- **Main Page** (`include/mainpage.doxygen`): Project overview, quick start, architecture
- **API Reference**: Namespace and function documentation for all public modules
  - `ftdi`: Core FTDI communication API
  - `xfer`: Data transfer operations  
  - `crc8`: CRC-8 checksum utilities
- **Logging** (@ref Logging): Debug logging utilities documentation
- **File Documentation**: Detailed comments for implementation files
  - TCP proxy architecture and retry logic
  - Packet tracing format
  - Build mode behavior

### Key Documented Functions

Core public API:

- **ftdi::InitComms()** — Device initialization
- **ftdi::DoConsole()** — Interactive debug console
- **ftdi::DoTcpProxy()** — TCP↔FTDI proxy server
- **xfer::Download()**, **xfer::Upload()**, **xfer::Execute()** — Data transfer

Internal helper functions (TCP proxy implementation):

- `write_all_socket()` — TCP socket writes with EINTR handling
- `write_all_ftdi()` — FTDI writes with adaptive chunking/retry
- `trace_rsp_stream()` — RSP packet parsing and output

## License

The software components are released under a BSD 2-Clause license. See the individual source files for details.
