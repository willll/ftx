
cmake_minimum_required(VERSION 3.10)
project(ftx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
  message(STATUS "No build type specified; defaulting to Debug.")
endif()

# Always generate compile_commands.json for static analysis tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(UNIX AND NOT APPLE AND NOT WIN32)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
endif()

option(BUILD_STATIC "Build a statically-linked executable (requires static libs)" OFF)
option(FTX_STATIC_BOOST "Link Boost libraries statically to avoid runtime Boost .so dependencies" ON)

# Allow users to request a build with NDEBUG defined via `cmake -DNDEBUG=ON ..`.
option(FTX_DEFINE_NDEBUG "Define NDEBUG for ftx" OFF)
if(DEFINED NDEBUG AND NDEBUG)
  set(FTX_DEFINE_NDEBUG ON)
endif()

if(FTX_STATIC_BOOST)
  message(STATUS "FTX_STATIC_BOOST=ON: linking Boost libraries statically")
  set(Boost_USE_STATIC_LIBS ON)
else()
  message(STATUS "FTX_STATIC_BOOST=OFF: linking Boost libraries dynamically")
endif()

if(BUILD_STATIC)
  if(UNIX AND NOT APPLE AND NOT WIN32)
    message(STATUS "BUILD_STATIC=ON: attempting to produce a fully static executable")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so" ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
  else()
    message(STATUS "BUILD_STATIC=ON: resolving static libraries for third-party dependencies")
  endif()
else()
  message(STATUS "BUILD_STATIC=OFF: producing a mostly dynamic executable")
endif()

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

find_package(Boost REQUIRED COMPONENTS program_options filesystem)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBFTDI1 REQUIRED libftdi1)

if(BUILD_STATIC)
  set(LIBFTDI1_LINK_LIBS "")
  set(ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  if(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib" ".a")
  else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  endif()

  foreach(lib ${LIBFTDI1_STATIC_LIBRARIES})
    # Try to find the static version of the library
    find_library(STATIC_LIB_${lib} NAMES ${lib} PATHS ${LIBFTDI1_STATIC_LIBRARY_DIRS})
    if(STATIC_LIB_${lib})
      list(APPEND LIBFTDI1_LINK_LIBS ${STATIC_LIB_${lib}})
    else()
      # If not found as static, search with original suffixes (allowing dynamic fallback)
      set(CMAKE_FIND_LIBRARY_SUFFIXES ${ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
      find_library(DYNAMIC_LIB_${lib} NAMES ${lib} PATHS ${LIBFTDI1_STATIC_LIBRARY_DIRS})
      if(DYNAMIC_LIB_${lib})
        list(APPEND LIBFTDI1_LINK_LIBS ${DYNAMIC_LIB_${lib}})
      else()
        list(APPEND LIBFTDI1_LINK_LIBS ${lib})
      endif()
      if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib" ".a")
      else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
      endif()
    endif()
  endforeach()

  set(CMAKE_FIND_LIBRARY_SUFFIXES ${ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
else()
  set(LIBFTDI1_LINK_LIBS ${LIBFTDI1_LIBRARIES})
endif()

# Create an interface target wrapping pkg-config results for libftdi1 so we can
# link to it in a target-oriented way instead of using raw variables everywhere.
add_library(libftdi1_pkgconfig INTERFACE)
target_include_directories(libftdi1_pkgconfig INTERFACE ${LIBFTDI1_INCLUDE_DIRS})
target_link_directories(libftdi1_pkgconfig INTERFACE ${LIBFTDI1_LIBRARY_DIRS})
target_link_libraries(libftdi1_pkgconfig INTERFACE ${LIBFTDI1_LINK_LIBS})

if(APPLE AND BUILD_STATIC)
  target_link_libraries(libftdi1_pkgconfig INTERFACE
    "-framework CoreFoundation"
    "-framework IOKit"
    "-framework Security"
  )
endif()

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
    # Ensure libudev is linked if not already resolved in LIBFTDI1_LINK_LIBS
    if(NOT LIBUDEV_LIBRARY IN_LIST LIBFTDI1_LINK_LIBS)
      target_link_libraries(libftdi1_pkgconfig INTERFACE ${LIBUDEV_LIBRARY})
    endif()
  else()
    message(WARNING "BUILD_STATIC requested but libudev not found; static link may fail")
    string(REPLACE "-static" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
  endif()
endif()

add_executable(ftx
  src/ftx.cpp
  src/ftdi.cpp
  src/ftdi_init.cpp
  src/ftdi_discovery.cpp
  src/ftdi_console.cpp
  src/ftdi_gdb.cpp
  src/xfer.cpp
  src/crc.cpp
)

# win32 config
if (WIN32)
  target_sources(ftx PRIVATE src/ftx.rc)
  # CMake already defines WIN32; add unicode defines for MSVC if needed.
  target_compile_definitions(ftx PRIVATE UNICODE _UNICODE)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
  if (MSVC)
    # Prefer target_compile_options for MSVC flags instead of the old
    # CXX_COMPILER_FLAGS property.
    target_compile_options(ftx PRIVATE /std:c++17)
  endif()
  # Link against Winsock2 for socket and WSAPoll support
  target_link_libraries(ftx PRIVATE ws2_32)
endif()

target_include_directories(ftx
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/satcom_lib
  PRIVATE ${LIBFTDI1_INCLUDE_DIRS}
)


target_link_libraries(ftx PRIVATE Boost::program_options Boost::filesystem libftdi1_pkgconfig)

# Use target-based language requirement (modern CMake)
target_compile_features(ftx PRIVATE cxx_std_17)

# Define NDEBUG when requested via the cache variable or when building Release.
if(FTX_DEFINE_NDEBUG OR CMAKE_BUILD_TYPE STREQUAL "Release")
  message(STATUS "Defining NDEBUG for ftx")
  target_compile_definitions(ftx PRIVATE NDEBUG)
endif()

# Define FTX_DEBUG_BUILD for debug builds to conditionally enable features
target_compile_definitions(ftx PRIVATE $<$<CONFIG:Debug>:FTX_DEBUG_BUILD>)

# Get Git version for the CLI
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_VERSION)
  target_compile_definitions(ftx PRIVATE FTX_VERSION=${GIT_VERSION})
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