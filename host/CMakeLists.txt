cmake_minimum_required(VERSION 3.16)
project(g474_pd_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_executable(g474_pd_host
  src/analog.cpp
  src/main.cpp
  src/pd_decoder.cpp
  src/record.cpp
  src/usb_device.cpp
)

target_include_directories(g474_pd_host PRIVATE
  include
  ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(g474_pd_host PRIVATE ${LIBUSB_LIBRARIES})
target_compile_options(g474_pd_host PRIVATE -Wall -Wextra -Wpedantic)
