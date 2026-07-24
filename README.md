[![Build Ftx Windows](https://github.com/willll/ftx/actions/workflows/build-windows.yml/badge.svg)](https://github.com/willll/ftx/actions/workflows/build-windows.yml) | [![Build Ftx Linux](https://github.com/willll/ftx/actions/workflows/build-linux.yml/badge.svg)](https://github.com/willll/ftx/actions/workflows/build-linux.yml) | [![Build Ftx macOS](https://github.com/willll/ftx/actions/workflows/build-macos.yml/badge.svg)](https://github.com/willll/ftx/actions/workflows/build-macos.yml)

# Sega Saturn USB Flash Cart Transfer Utility

<p align="center">
  <img src="assets/ftx_logo.png" width="256" alt="FTX Logo">
</p>

This project provides a modern C++17 command-line utility for transferring data to and from a Sega Saturn USB flash cartridge. It supports downloading, uploading, and executing binaries on the device, with robust error handling, execution tracing, and cross-platform support via CMake.

**Attribution:** This project is based on the original work by [andersm/usbcart](https://github.com/andersm/usbcart/tree/master/ftx).

## Features

- Download data from the device to a file
- Upload data from a file to the device
- Upload and execute a program on the device
- Execute a program at a specified address
- Debug console mode for device output
- WebDAV server mode to mount the cartridge's FAT filesystem as a network drive
- Recursive directory synchronization (unidirectional push, unidirectional pull, and bidirectional)
- Raw TCP-to-FTDI proxy server for GDB/debugger integration
- Command-line interface with argument parsing using Boost.Program_options
- Modern C++17 codebase with Doxygen documentation
- FTDI USB communication (libftdi)

## Requirements

- C++17 compatible compiler
- CMake 3.10+
- Boost libraries (compiled: `program_options`, `filesystem`; header-only: `beast`, `asio`)
- libftdi1 (with libusb-1.0)
- Sega Saturn USB flash cartridge (FTDI-based)

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

- Debug builds (`-DCMAKE_BUILD_TYPE=Debug`) and Release builds now both include logging support.
- Extensive progress logs, initialization traces, and debug outputs have been moved to dynamic runtime toggles.
- To view logs during execution, pass the `-v` (GDB commands) or `-vv` (full dbg traces) flag to the application. `ftx` is completely quiet by default to better support automated deployment scripts.

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
cmake --build .
```

### macOS (Native Apple Silicon / Intel)

Install dependencies via Homebrew:
```sh
brew install cmake boost libftdi pkg-config
```

Then build natively:
```sh
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Windows Driver Setup

On Microsoft Windows, the utility communicates with the Sega Saturn USB cartridge via `libusb` and `libftdi`. Since Windows installs the standard FTDI Serial Port (COM) driver by default, you must replace it with the generic **WinUSB** driver for the utility to detect and access the device.

To install the WinUSB driver:
1. Connect the USB cartridge to your Windows PC.
2. Download and run **Zadig** from [zadig.akeo.ie](https://zadig.akeo.ie/).
3. In Zadig, select **Options** -> **List All Devices** from the top menu.
4. In the main drop-down menu, select your cartridge device (typically shown as `FT245R USB FIFO` or similar).
5. Verify that the USB ID matches the target device (the default is VID `0403` and PID `6001`).
6. Select **WinUSB** as the target driver (to the right of the green arrow).
7. Click **Replace Driver** (or **Install Driver**) and wait for the process to finish.

*(Note: To revert to the standard FTDI virtual COM port drivers, you can uninstall the device from the Windows Device Manager and check the option to delete the driver software for the device, then replug it.)*

## Usage

```sh
./ftx [options] [commands]
```

### Options

- `--vid <VID>`: Device VID (default: 0x0403)
- `--pid <PID>`: Device PID (default: 0x6001)
- `-s <Serial>`: Match specific device by FTDI serial string
- `-l`      : List all connected FTDI devices
- `-t`      : Run terminal mode (bidirectional stdin/stdout)
- `-c`      : Run debug console (read-only stdout)
- `-g [port]`: Run raw TCP<->FTDI proxy (default port: 1234)
- `-wd [port]`: Run WebDAV server (default port: 8080)
- `-v`      : Print traced RSP packets as `GDB>...` and `Target>...` lines (GDB commands)
- `-vv`     : Enable detailed progress logs, initialization traces, and GDB packet tracing

### Commands

- `-d <file> <address> <size>`: Download data from device to file
- `-u <file> <address>`      : Upload data from file to device
- `-x <file> <address>`      : Upload program and execute
- `-r <address>`             : Execute program at address
- `-D, --dump <file>`        : Dump the Sega Saturn BIOS to a file
- `--ls <path>`              : List files and directories at the specified path on the target. Combine with `-l` for detailed listing with sizes and dates.
- `--rm <path>`              : Remove a file or empty directory on the target
- `--mkdir <path>`           : Create a directory on the target
- `--rmdir <path>`           : Delete a directory on the target
- `--cp <file> <target>`     : Copy a file to the target. `<target>` can be a FAT path (e.g., `/folder/file.bin`) or raw SD sectors (`sdraw:start:count`).
- `--crc <file>`             : Calculate and print the CRC-8 checksum for a file on the target
- `--lcrc <file>`            : Calculate and print the CRC-8 checksum for a local host file
- `--sync <local> <saturn> [mode]`: Synchronize a local directory with a Sega Saturn SD card directory recursively (`mode`: `1`=local->saturn [default], `2`=saturn->local, `3`=both).

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

List files and directories on the target's SD card:

```sh
./ftx --ls /
```

Detailed listing with file sizes and modification dates:

```sh
./ftx -l --ls /SD_TEST
```

Remove a file from the target's SD card:

```sh
./ftx --rm /old_save.dat
```

Copy a file to the FAT filesystem on the target:

```sh
./ftx --cp data.bin /test_folder/data.bin
```

Synchronize local folder to Saturn SD card (Mode 1: push):

```sh
./ftx --sync ./my_assets /SD_TEST/ASSETS
```

Synchronize Saturn SD card folder to local host (Mode 2: pull):

```sh
./ftx --sync ./backup /SD_TEST/ASSETS 2
```

Bidirectional recursive folder synchronization (Mode 3: both):

```sh
./ftx --sync ./my_assets /SD_TEST/ASSETS 3
```

## TCP Proxy Mode

`-g` starts a raw TCP proxy. All bytes received from the TCP client are forwarded directly to the FTDI device, and all bytes read from FTDI are forwarded back to the TCP client.

Start the proxy on the default port (1234):

```sh
./ftx -g
```

Start the proxy with GDB packet tracing:

```sh
./ftx -g 1234 -v
```

Start the proxy with full packet tracing and dbg execution logs:

```sh
./ftx -g 1234 -vv
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

When `-v` or `-vv` is enabled, traced packet lines are printed with one packet per line:

- `GDB>$...` for packets received from the TCP client
- `Target>$...` for packets received from the FTDI target

## WebDAV Server Mode

`-wd` starts a WebDAV server that exposes the target's FAT filesystem over the HTTP protocol. This allows you to mount the Sega Saturn USB cartridge as a network drive in Windows Explorer, macOS Finder, or Linux Nautilus to browse directories and transfer files.

Start the WebDAV server on the default port (8080):

```sh
./ftx -wd
```

Or on a custom port:

```sh
./ftx -wd 8081
```

### Mounting the Filesystem

#### Windows
1. Open **File Explorer** (Win+E).
2. Right-click **This PC** in the left sidebar and select **Map network drive...**
3. In the "Folder" field, enter: `http://localhost:8080/`
4. Click **Finish**. The cartridge will now appear as a network drive.

#### Linux (GNOME/Nautilus)
1. Open the **Files** (Nautilus) application.
2. Click on **Other Locations** in the left sidebar.
3. In the "Connect to Server" field at the bottom, enter: `dav://localhost:8080/`
4. Click **Connect**.

#### macOS
1. Open **Finder**.
2. From the top menu bar, select **Go** > **Connect to Server...** (or press `Cmd+K`).
3. In the "Server Address" field, enter: `http://localhost:8080/`
4. Click **Connect**.

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

In both debug and release builds, debug traces are controlled dynamically. Use the `-v` or `-vv` flags when running `ftx` to see these logs in your terminal. Building with `NDEBUG` will disable standard C++ assertions, but will no longer silence the logger.

### Error Handling

- Device initialization includes automatic recovery attempts on buffer purge failures.
- On console exit (Ctrl+C), the stdin reader thread stops within 50ms and the process exits cleanly.
- Read/write errors to the device are reported to stderr and terminate console mode.
- **Automated Deployment Friendly:** Returns standard `0` exit codes on success and `1` on failure for reliable integration into Makefiles and CI/CD pipelines. Transfers and initializations are completely quiet by default.

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
- **xfer::DoSdSync()** — Recursive directory synchronization (push, pull, bidirectional)

Internal helper functions (TCP proxy implementation):

- `write_all_socket()` — TCP socket writes with EINTR handling
- `write_all_ftdi()` — FTDI writes with adaptive chunking/retry
- `trace_rsp_stream()` — RSP packet parsing and output

## License

The software components are released under a BSD 2-Clause license. See the individual source files for details.
