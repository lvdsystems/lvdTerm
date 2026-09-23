# Vendored libssh2

Source: https://github.com/libssh2/libssh2.git, tag `libssh2-1.11.1` (see
`../LIBSSH2_UPSTREAM.txt`). License: BSD-3-Clause (see `COPYING`).

Vendored as-is: `CMakeLists.txt`, `cmake/`, `include/`, `src/`,
`libssh2.pc.in`, `COPYING`, `LICENSES`, `README.md`. Dropped: `docs/`,
`example/`, `tests/`, `os400/`, `vms/`, `ci/`, `appveyor.yml`, and the
autotools-only build files (`configure.ac`, `Makefile.am`, `m4/`,
`acinclude.m4`, `config.rpath`, `maketgz`, `get_ver.awk`, `git2news.pl`,
`libssh2-style.el`, `REUSE.toml`) — lvdterm builds this with its own
CMake, never autotools.

`docs/CMakeLists.txt` here is **not** upstream's (which builds man
pages) — `../CMakeLists.txt`'s `add_subdirectory(docs)` call has no
`BUILD_*` guard around it (unlike `example/`/`tests/`), so dropping the
real `docs/` needed a stub left behind to satisfy that call. See the
comment in that file.

## Crypto backend: OpenSSL, not WinCNG or mbedTLS

libssh2 supports several crypto backends (`CRYPTO_BACKEND` CMake option:
OpenSSL, wolfSSL, Libgcrypt, WinCNG, mbedTLS). WinCNG needs no extra
dependency and was the initial plan, but it and mbedTLS both hardcode
`#define LIBSSH2_ED25519 0` in their respective `src/wincng.h` /
`src/mbedtls.h` (confirmed in both the 1.11.1 tag and current upstream
`main`) — no ed25519 host/user keys, and no curve25519-sha256 KEX (its
implementation in `src/kex.c` is itself gated behind
`#if LIBSSH2_ED25519`). Since `ssh-keygen`'s default key type has been
ed25519 for years, that gap is common enough in practice to matter. Only
the OpenSSL backend (`src/openssl.h`) enables it. See
`../openssl-prebuilt/BUILDING.md` for how that dependency was built for
this toolchain.

## CMake wiring

lvdterm's top-level `CMakeLists.txt` sets `CRYPTO_BACKEND OpenSSL`,
`OPENSSL_ROOT_DIR` (pointing at `../openssl-prebuilt/win64-llvm-mingw`),
`BUILD_SHARED_LIBS OFF`, `BUILD_EXAMPLES OFF`, `BUILD_TESTING OFF`, and
`ENABLE_ZLIB_COMPRESSION OFF` as cache variables before
`add_subdirectory(third_party/libssh2)`, then links the `libssh2` target
libssh2's own `CMakeLists.txt` produces. Nothing in this directory was
otherwise modified from upstream.
