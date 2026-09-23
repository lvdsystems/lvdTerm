cmake_minimum_required(VERSION 3.25)
# NOTE: installer/lvdterm.iss has its own hardcoded AppVersion - not
# auto-synced with this (the installer isn't part of the CMake build).
# Bump both together.
project(lvdterm VERSION 0.2 LANGUAGES CXX C) # C needed for vendored libssh2

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network SerialPort)

add_subdirectory(third_party/qtermwidget)

# Vendored libssh2 (BSD-3-Clause), built against a vendored static OpenSSL
# (Apache-2.0) - see third_party/libssh2/VENDORING.md and
# third_party/openssl-prebuilt/BUILDING.md for why OpenSSL specifically
# (ed25519/curve25519-sha256 support) and how that OpenSSL build was made.
set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl-prebuilt/win64-llvm-mingw" CACHE PATH "" FORCE)
set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/libssh2)

add_subdirectory(src)
