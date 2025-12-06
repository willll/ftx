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

## License

The software components are released under a BSD 2-Clause license. See the individual source files for details.
