[![Build Ftx](https://github.com/willll/ftx/actions/workflows/build.yml/badge.svg)](https://github.com/willll/ftx/actions/workflows/build.yml)

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
