
cmake_minimum_required(VERSION 3.10)
project(ftx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always generate compile_commands.json for static analysis tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options filesystem)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBFTDI1 REQUIRED libftdi1)

# Create an interface target wrapping pkg-config results for libftdi1 so we can
# link to it in a target-oriented way instead of using raw variables everywhere.
add_library(libftdi1::pkgconfig INTERFACE)
target_include_directories(libftdi1::pkgconfig INTERFACE ${LIBFTDI1_INCLUDE_DIRS})
target_link_libraries(libftdi1::pkgconfig INTERFACE ${LIBFTDI1_LIBRARIES})

add_executable(ftx
  src/ftx.cpp
  src/xfer.cpp
  src/crc.cpp
)

# win32 config
if (WIN32)
  # CMake already defines WIN32; add unicode defines for MSVC if needed.
  target_compile_definitions(ftx PRIVATE UNICODE _UNICODE)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
  if (MSVC)
    # Prefer target_compile_options for MSVC flags instead of the old
    # CXX_COMPILER_FLAGS property.
    target_compile_options(ftx PRIVATE /std:c++17)
  endif()
endif()

target_include_directories(ftx
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/satcom_lib
  PRIVATE ${LIBFTDI1_INCLUDE_DIRS}
)


target_link_libraries(ftx PRIVATE Boost::program_options Boost::filesystem libftdi1::pkgconfig)

# Use target-based language requirement (modern CMake)
target_compile_features(ftx PRIVATE cxx_std_17)

# Code validation: cppcheck static analysis
add_custom_target(
  cppcheck
  COMMAND cppcheck --enable=all --project=${CMAKE_BINARY_DIR}/compile_commands.json --suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck-suppressions.txt
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running cppcheck static analysis..."
)