# Vendored FreeRDP

Source: https://github.com/FreeRDP/FreeRDP.git, tag `3.32.2` (see
`../FREERDP_UPSTREAM.txt`). License: Apache-2.0 (see `LICENSE`).

Vendored **unmodified**, minus directories lvdterm never builds. Kept:
`CMakeLists.txt`, `CMakeCPack*.cmake*`, `cmake/`, `include/`, `libfreerdp/`,
`winpr/`, `LICENSE`, `README.md`, `ChangeLog`. Dropped: `client/`, `server/`,
`channels/`, `uwac/`, `rdtk/`, `docs/`, `ci/`, `packaging/`, `scripts/`,
`tools/`, `external/`, `resources/`, `third-party/`, and every `test/`,
`tests/`, `tools/` and `sample/` directory inside what is kept.

## How it is built

Not with `add_subdirectory()`: FreeRDP's root `CMakeLists.txt` assumes it is
the top-level project. `../freerdp.cmake` builds it as its own CMake project
through `ExternalProject_Add`, with only the two static libraries lvdterm
needs (`freerdp`, `winpr`), and exposes them as the `lvd_freerdp` target. The
options that switch everything else off (all virtual channels, the client and
server programs, every optional codec and audio/video/UI back end) are listed
there.

lvdterm's own wrapper is `src/transports/rdp/RdpClient.cpp`: connect/event
loop on a thread, FreeRDP's software GDI as the framebuffer, input
forwarding. Because this build has no channel plugins, there is no clipboard,
audio or drive redirection and no dynamic resolution yet.

### Platform notes

- **Windows (llvm-mingw)**: built against the vendored static OpenSSL
  (`../openssl-prebuilt`) and the vendored zlib (`../zlib`, there is none in
  the toolchain). `-D__STDC_NO_THREADS__=1` because mingw has no C11
  `<threads.h>`; `FREERDP_EXPORTS`/`WINPR_EXPORTS` are defined for consumers
  because the headers otherwise mark every function `dllimport`; NTLM's
  MD4/MD5/RC4 use FreeRDP's in-tree implementations (`WITH_INTERNAL_*`)
  because a static OpenSSL does not load its legacy provider.
- **Linux**: the distribution's own OpenSSL and zlib (`libssl-dev`,
  `zlib1g-dev`).

## Updating

Replace the directory with a new tag's trimmed copy (same list as above),
update `../FREERDP_UPSTREAM.txt`, and rebuild; the only API surface lvdterm
uses is in `src/transports/rdp/RdpClient.cpp`.
