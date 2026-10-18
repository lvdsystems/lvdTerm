# Vendored FreeRDP (Apache-2.0) - the RDP protocol library behind lvdterm's
# RDP connections (see third_party/freerdp/VENDORING.md).
#
# Built through ExternalProject, as its own CMake build, instead of
# add_subdirectory(): FreeRDP's root CMakeLists.txt assumes it is the
# top-level project (project(), CTest, CPack, ${CMAKE_SOURCE_DIR}/cmake, ...)
# and a nested build would fight lvdterm's own settings. Only the two
# static libraries lvdterm needs (freerdp, winpr) are built, with every
# optional codec/channel/UI feature switched off - there is no client UI or
# virtual-channel plugin in this build, just the protocol core and its
# software GDI (the framebuffer lvdterm draws).
#
# Defines the INTERFACE target `lvd_freerdp` (include paths + libraries).

include(ExternalProject)

set(LVD_FRDP_SOURCE "${CMAKE_CURRENT_LIST_DIR}/freerdp")
set(LVD_FRDP_BINARY "${CMAKE_BINARY_DIR}/third_party/freerdp-build")

set(LVD_FRDP_ARGS
    -G "${CMAKE_GENERATOR}"
    -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_TESTING_INTERNAL=OFF
    # Protocol core only.
    -DWITH_CLIENT=OFF -DWITH_CLIENT_COMMON=OFF -DWITH_CLIENT_CHANNELS=OFF
    -DWITH_SERVER=OFF -DWITH_SERVER_CHANNELS=OFF -DWITH_CHANNELS=OFF
    -DWITH_SHADOW=OFF -DWITH_PROXY=OFF -DWITH_SAMPLE=OFF
    -DWITH_MANPAGES=OFF -DWITH_WINPR_TOOLS=OFF
    # No optional codecs / platform back ends - each of these would be one
    # more third-party library to vendor and license.
    -DWITH_FFMPEG=OFF -DWITH_SWSCALE=OFF -DWITH_DSP_FFMPEG=OFF -DWITH_CAIRO=OFF
    -DWITH_JPEG=OFF -DWITH_OPENH264=OFF -DWITH_OPUS=OFF -DWITH_FAAC=OFF
    -DWITH_FAAD2=OFF -DWITH_GSM=OFF -DWITH_LAME=OFF -DWITH_SOXR=OFF
    -DWITH_FUSE=OFF -DWITH_CUPS=OFF -DWITH_PCSC=OFF -DWITH_KRB5=OFF
    -DWITH_X11=OFF -DWAYLAND=OFF -DWITH_WAYLAND=OFF
    -DWITH_ALSA=OFF -DWITH_PULSE=OFF -DWITH_OSS=OFF
    -DWITH_WEBVIEW=OFF -DWITH_AAD=OFF
    # NTLM's MD4/MD5/RC4 are implemented in-tree rather than taken from
    # OpenSSL's legacy provider, which a static OpenSSL does not load.
    -DWITH_INTERNAL_MD4=ON -DWITH_INTERNAL_MD5=ON -DWITH_INTERNAL_RC4=ON
    -DWITH_BINARY_VERSIONING=OFF -DWITH_UNICODE_BUILTIN=ON
    -DUSE_UNWIND=OFF
)

if(WIN32)
    # zlib (required by FreeRDP) is vendored too: the llvm-mingw toolchain
    # ships none.
    set(LVD_ZLIB_DIR "${CMAKE_CURRENT_LIST_DIR}/zlib")
    add_library(lvd_zlib STATIC
        ${LVD_ZLIB_DIR}/adler32.c ${LVD_ZLIB_DIR}/compress.c ${LVD_ZLIB_DIR}/crc32.c
        ${LVD_ZLIB_DIR}/deflate.c ${LVD_ZLIB_DIR}/gzclose.c ${LVD_ZLIB_DIR}/gzlib.c
        ${LVD_ZLIB_DIR}/gzread.c ${LVD_ZLIB_DIR}/gzwrite.c ${LVD_ZLIB_DIR}/infback.c
        ${LVD_ZLIB_DIR}/inffast.c ${LVD_ZLIB_DIR}/inflate.c ${LVD_ZLIB_DIR}/inftrees.c
        ${LVD_ZLIB_DIR}/trees.c ${LVD_ZLIB_DIR}/uncompr.c ${LVD_ZLIB_DIR}/zutil.c)
    target_include_directories(lvd_zlib PUBLIC ${LVD_ZLIB_DIR})
    target_compile_definitions(lvd_zlib PRIVATE _LARGEFILE64_SOURCE=1 NO_FSEEKO)
    set_target_properties(lvd_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

    list(APPEND LVD_FRDP_ARGS
        -DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER}
        # mingw has no C11 <threads.h>.
        -DCMAKE_C_FLAGS=-D__STDC_NO_THREADS__=1
        -DOPENSSL_ROOT_DIR=${CMAKE_CURRENT_LIST_DIR}/openssl-prebuilt/win64-llvm-mingw
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DZLIB_INCLUDE_DIR=${LVD_ZLIB_DIR}
        -DZLIB_LIBRARY=$<TARGET_FILE:lvd_zlib>)
    set(LVD_FRDP_DEPENDS lvd_zlib)
else()
    # Linux: the distribution's own OpenSSL and zlib (-dev packages).
    set(LVD_FRDP_DEPENDS "")
endif()

set(LVD_FRDP_FREERDP_LIB "${LVD_FRDP_BINARY}/libfreerdp/libfreerdp3.a")
set(LVD_FRDP_WINPR_LIB "${LVD_FRDP_BINARY}/winpr/libwinpr/libwinpr3.a")

ExternalProject_Add(lvd_freerdp_build
    SOURCE_DIR "${LVD_FRDP_SOURCE}"
    BINARY_DIR "${LVD_FRDP_BINARY}"
    CMAKE_ARGS ${LVD_FRDP_ARGS}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target freerdp winpr
    INSTALL_COMMAND ""
    DEPENDS ${LVD_FRDP_DEPENDS}
    BUILD_BYPRODUCTS "${LVD_FRDP_FREERDP_LIB}" "${LVD_FRDP_WINPR_LIB}"
    USES_TERMINAL_BUILD OFF
)

add_library(lvd_freerdp INTERFACE)
add_dependencies(lvd_freerdp lvd_freerdp_build)
target_include_directories(lvd_freerdp SYSTEM INTERFACE
    "${LVD_FRDP_SOURCE}/include" "${LVD_FRDP_BINARY}/include"
    "${LVD_FRDP_SOURCE}/winpr/include" "${LVD_FRDP_BINARY}/winpr/include")

if(WIN32)
    # FreeRDP's headers mark every API dllimport on Windows unless *_EXPORTS is
    # defined; this is a static library, so there is no import table.
    target_compile_definitions(lvd_freerdp INTERFACE FREERDP_EXPORTS WINPR_EXPORTS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(lvd_freerdp INTERFACE
        "${LVD_FRDP_FREERDP_LIB}" "${LVD_FRDP_WINPR_LIB}" lvd_zlib
        OpenSSL::SSL OpenSSL::Crypto
        ws2_32 crypt32 bcrypt ncrypt secur32 rpcrt4 ole32 oleaut32 shell32 user32
        advapi32 winmm iphlpapi wtsapi32 userenv dbghelp shlwapi gdi32 ntdll)
else()
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    target_link_libraries(lvd_freerdp INTERFACE
        "${LVD_FRDP_FREERDP_LIB}" "${LVD_FRDP_WINPR_LIB}"
        OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB pthread dl m)
endif()
