
cmake_minimum_required(VERSION 3.10)
project(ftx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# Always generate compile_commands.json for static analysis tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options filesystem)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBFTDI1 REQUIRED libftdi1)

add_executable(ftx
  src/ftx.cpp
  src/xfer.cpp
  src/crc.cpp
)

target_include_directories(ftx
  PRIVATE ${CMAKE_SOURCE_DIR}/include
  PRIVATE ${CMAKE_SOURCE_DIR}/satcom_lib
  PRIVATE ${LIBFTDI1_INCLUDE_DIRS}
)


target_link_libraries(ftx PRIVATE Boost::program_options Boost::filesystem ${LIBFTDI1_LIBRARIES})

# Code validation: cppcheck static analysis
add_custom_target(
  cppcheck
  COMMAND cppcheck --enable=all --project=${CMAKE_BINARY_DIR}/compile_commands.json --suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck-suppressions.txt
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running cppcheck static analysis..."
)