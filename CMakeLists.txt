
cmake_minimum_required(VERSION 3.10)
project(ftx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always generate compile_commands.json for static analysis tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_STATIC "Build a statically-linked executable (requires static libs)" OFF)

if(BUILD_STATIC AND UNIX AND NOT APPLE)
  message(STATUS "BUILD_STATIC=ON: attempting to produce a statically-linked executable")
  set(Boost_USE_STATIC_LIBS ON)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so" ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
else()
  message(STATUS "BUILD_STATIC=OFF: producing a dynamically-linked executable (default)")
endif()

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options filesystem)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBFTDI1 REQUIRED libftdi1)

# Create an interface target wrapping pkg-config results for libftdi1 so we can
# link to it in a target-oriented way instead of using raw variables everywhere.
add_library(libftdi1_pkgconfig INTERFACE)
target_include_directories(libftdi1_pkgconfig INTERFACE ${LIBFTDI1_INCLUDE_DIRS})
target_link_libraries(libftdi1_pkgconfig INTERFACE ${LIBFTDI1_LIBRARIES})

# When building statically on Linux, libusb may require libudev; add it
# to the pkgconfig wrapper so the final link picks it up.
if(BUILD_STATIC AND UNIX AND NOT APPLE)
  find_library(LIBUDEV_LIBRARY NAMES udev)
  if(LIBUDEV_LIBRARY)
    if(LIBUDEV_LIBRARY MATCHES "\\.so($|\\.)")
      message(WARNING "Found only shared libudev (${LIBUDEV_LIBRARY}); cannot statically link it into the binary.")
      string(REPLACE "-static" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
      message(STATUS "Removed -static from CMAKE_EXE_LINKER_FLAGS; final binary will link libudev dynamically.")
    endif()
    target_link_libraries(libftdi1_pkgconfig INTERFACE ${LIBUDEV_LIBRARY})
  else()
    message(WARNING "BUILD_STATIC requested but libudev not found; static link may fail")
    string(REPLACE "-static" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
  endif()
endif()

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


target_link_libraries(ftx PRIVATE Boost::program_options Boost::filesystem libftdi1_pkgconfig)

# Use target-based language requirement (modern CMake)
target_compile_features(ftx PRIVATE cxx_std_17)

# If MAKE_BUILD_TYPE is set to DEBUG (either as a CMake variable or an
# environment variable), define NDEBUG for the target. This deliberately follows
# the user's request; note NDEBUG typically disables assert(), so this inverts
# the usual convention.
if((DEFINED MAKE_BUILD_TYPE AND "$<LOWER_CASE:${MAKE_BUILD_TYPE}>" STREQUAL "DEBUG") OR
   ($<LOWER_CASE:"$ENV{MAKE_BUILD_TYPE}>" STREQUAL "DEBUG"))
  message(STATUS "MAKE_BUILD_TYPE=DEBUG detected; adding NDEBUG definition to ftx")
  target_compile_definitions(ftx PRIVATE NDEBUG)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")

# Code validation: cppcheck static analysis
add_custom_target(
  cppcheck
  COMMAND cppcheck --enable=all --project=${CMAKE_BINARY_DIR}/compile_commands.json --suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck-suppressions.txt
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running cppcheck static analysis..."
)