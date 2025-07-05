cmake_minimum_required(VERSION 3.10)
project(ftx LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

find_package(Boost REQUIRED COMPONENTS program_options filesystem)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBFTDI1 REQUIRED libftdi1)

add_executable(ftx main.cpp xfer.cpp crc.cpp)

target_include_directories(ftx PRIVATE ${CMAKE_SOURCE_DIR}/satcom_lib)
target_include_directories(ftx PRIVATE ${LIBFTDI1_INCLUDE_DIRS})

target_link_libraries(ftx PRIVATE Boost::program_options Boost::filesystem ${LIBFTDI1_LIBRARIES})