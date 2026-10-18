cmake_minimum_required(VERSION 3.25)
# NOTE: installer/lvdterm.iss has its own hardcoded AppVersion - not
# auto-synced with this (the installer isn't part of the CMake build).
# Bump both together.
project(lvdterm VERSION 0.7 LANGUAGES CXX C) # C needed for vendored libssh2

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

# EXCLUDE_FROM_ALL: both of these are statically linked into lvdterm
# with nothing of their own to ship separately - without this, their own
# install() rules (meant for being used as a standalone library, not
# vendored-and-baked-in like here) run too during `cmake --install`/
# cpack, and in libssh2's case actually fails outright (it tries to
# install a NEWS doc file this vendored copy doesn't have - see
# third_party/libssh2/VENDORING.md for what was trimmed). Targets in an
# EXCLUDE_FROM_ALL subdirectory still build normally when something
# outside it (lvdterm) depends on them - this only removes them from the
# default "build/install everything" set, not from the actual build.
add_subdirectory(third_party/qtermwidget EXCLUDE_FROM_ALL)

# Vendored libssh2 (BSD-3-Clause). On Windows, built against a vendored
# static OpenSSL (Apache-2.0) - see third_party/libssh2/VENDORING.md and
# third_party/openssl-prebuilt/BUILDING.md for why OpenSSL specifically
# (ed25519/curve25519-sha256 support) and how that OpenSSL build was made.
# That prebuilt binary is Windows/mingw-ABI-only (no Linux sibling), so
# on Linux this instead points libssh2 (and, below, lvdterm's own Crypto
# backend) at the system OpenSSL via a plain find_package() - vendoring
# a static OpenSSL isn't needed there since every distro ships -dev
# packages for it.
set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
if(WIN32)
    set(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl-prebuilt/win64-llvm-mingw" CACHE PATH "" FORCE)
    set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/libssh2 EXCLUDE_FROM_ALL)

# Vendored FreeRDP (+ zlib on Windows) for RDP connections - defines
# the lvd_freerdp target; see third_party/freerdp.cmake.
include(third_party/freerdp.cmake)

add_subdirectory(src)

# Packaging: a .deb via CPack's Debian generator - the Linux equivalent
# of installer/lvdterm.iss (that file stays Windows-only/untouched; see
# its own comments). Windows packaging doesn't go through CMake's
# install()/CPack machinery at all (windeployqt + Inno Setup instead),
# so none of this is reachable or relevant there - `cpack` is simply
# never invoked on that side.
if(NOT WIN32)
    set(CPACK_PACKAGE_NAME "lvdterm")
    # Reuses project()'s own version above rather than a third
    # hand-synced copy - unlike installer/lvdterm.iss's AppVersion,
    # which predates this and is Windows-specific tooling anyway.
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VENDOR "LVD Systems S.r.l.")
    set(CPACK_PACKAGE_CONTACT "LVD Systems S.r.l.")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://www.lvdsystems.eu")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A tabbed and splittable SSH/Telnet/Serial terminal emulator")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    # Auto-detects the actual runtime package names/versions this binary
    # needs (via dpkg-shlibdeps against the built executable) instead of
    # this file hand-listing them - the right call specifically because
    # distros rename these across releases (e.g. Debian trixie's
    # libqt6core6 -> libqt6core6t64 for the 64-bit time_t transition);
    # hardcoding a list here would silently go stale.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    include(CPack)
endif()
