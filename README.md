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

#### Release

```sh
mkdir -p build
cd build
cmake ..
make
```

#### Debug

```sh
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -G "Ninja" .. 
cmake --build .
```

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
- `-g [port]`: Run GDB Remote Serial Protocol stub (default port: 1234)

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

## GDB Remote Debugging

ftx implements a **GDB Remote Serial Protocol (RSP)** stub to enable remote debugging of Saturn cartridges using standard GDB tools.

### Starting the GDB Stub

Listen on the default port (1234):

```sh
./ftx -g
```

Or specify a custom port:

```sh
./ftx -g 2345
```

### Connecting from GDB

In a separate terminal, connect to the stub:

```sh
gdb your-binary
(gdb) target remote localhost:1234
```

### Supported Commands

The stub implements core GDB Remote Serial Protocol commands:

- `g` — Read all registers
- `m` — Read memory (format: `mADDR,LENGTH`)
- `M` — Write memory (format: `MADDR,LENGTH:data`)
- `c` — Continue execution
- `s` — Single step
- `?` — Query halt reason
- `q` — General queries (e.g., `qAttached`)
- `Q` — General set operations

### Protocol Details

The GDB Remote Serial Protocol uses ASCII-based packet framing:

```
$command#checksum
```

Where:
- `$` — packet start marker
- `command` — RSP command string
- `#` — checksum marker
- `checksum` — two-digit hex XOR of command bytes

The stub validates checksums and responds with:
- `+` — ACK (packet received successfully)
- `-` — NAK (packet rejected, retry)
- `$response#checksum` — Response packet

### Implementation Details

The stub runs in a single thread, listening for GDB connections on the specified TCP port. Each connected client runs the RSP command loop synchronously. Commands are translated to Saturn debugger operations (register reads, memory access, execution control) and responses are marshaled back to GDB format.

Currently, register and memory access operations return placeholder data; real device integration requires parsing Saturn firmware debug interfaces.

### Debug Traces

The project includes extensive debug tracing. When built in Debug mode, traces are emitted to stdout with the `[DoConsole][dbg]` prefix. Key traces include:

- `stdin reader thread started/exiting` — thread lifecycle events
- `stdin got byte=0xNN` — every byte received from terminal input
- `stdin line enqueued` — complete line ready to send to device
- `ftdi_read_data status=N` — device reads (0 = no data, >0 = bytes read, <0 = error)
- `ftdi_write_data wrote=N` — successful write chunks to device

To see these traces, build with `-DCMAKE_BUILD_TYPE=Debug`:

```sh
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

In release builds (`-DCMAKE_BUILD_TYPE=Release` or unspecified), debug traces are compiled out entirely via the `NDEBUG` macro.

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

## License

The software components are released under a BSD 2-Clause license. See the individual source files for details.
