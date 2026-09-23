# Vendored OpenSSL (win64, llvm-mingw)

Source: https://github.com/openssl/openssl.git, tag `openssl-3.5.8` (see
`OPENSSL_UPSTREAM.txt`). License: Apache-2.0 (see `LICENSE.txt`).

## Why a prebuilt static lib instead of a CMake FetchContent build

libssh2's only crypto backend with ed25519/curve25519-sha256 support is
OpenSSL (WinCNG and mbedTLS both hardcode `LIBSSH2_ED25519 0` — see
`../libssh2/VENDORING.md`). OpenSSL's own build system is a Perl
`Configure` script + a generated `Makefile`, not CMake, and this
machine's Git-for-Windows Perl is a minimal Cygwin build missing several
core CPAN modules `Configure` needs. Reproducing that inside our normal
CMake configure step every time isn't practical, so the build output
(headers + static libs) is committed here instead, once, following the
recipe below. Only `win64-llvm-mingw/` is built; re-run this recipe to
add another target triple/kit.

## Recipe (llvm-mingw, static, no-asm)

1. **Perl module gaps.** Git for Windows' bundled Perl (`Perl 5,
   cygwin-thread-multi`) is missing several modules `Configure` needs at
   compile time (`unless (caller) { use Pod::Usage; ... }` in the
   generated `configdata.pm` runs unconditionally). Its own `cpan` tool
   is itself broken in this install (missing `CPAN::Author`), so fetch
   the missing **pure-Perl** modules' `lib/` trees directly from CPAN and
   point `PERL5LIB` (or `-I`) at the combined directory instead:
   - `Locale::Maketext::Simple` (JESSE/Locale-Maketext-Simple-0.21)
   - `ExtUtils::MakeMaker` (BINGOS/ExtUtils-MakeMaker-7.78)
   - `podlators` (RRA/podlators-v6.1.1) — provides `Pod::Text`
   - `Pod::Simple` (KHW/Pod-Simple-3.48) — provides `Pod::Escapes` as a
     dependency
   - `Pod::Usage` (MAREKR/Pod-Usage-2.05) — superseded by the copy that
     ships inside podlators/Pod-Simple, kept for completeness
   Copy each distribution's `lib/*` into one directory, e.g.
   `perl-extra-lib/`.

2. **Configure**, with the llvm-mingw triple's `clang` as the
   cross-compiler and `no-asm` (no NASM on this machine — a throughput
   cost we accept for a terminal app's SSH traffic, not a high-volume
   crypto workload):
   ```
   PATH="<llvm-mingw>/bin:$PATH" \
   PERL5LIB=/c/path/to/perl-extra-lib \
   perl Configure mingw64 no-shared no-asm no-tests no-docs no-engine \
       --cross-compile-prefix=x86_64-w64-mingw32- CC=clang \
       --prefix=/tmp/openssl-out
   ```

3. **Build**, passing `PERL` as a `make` *command-line* variable rather
   than relying on the `PERL5LIB` environment variable reaching the
   `make`-spawned Perl subprocesses:
   ```
   PATH="<llvm-mingw>/bin:$PATH" \
   mingw32-make -j4 PERL="perl -I/c/path/to/perl-extra-lib" build_libs
   ```
   `mingw32-make.exe` ships with the llvm-mingw kit
   (`<llvm-mingw>/bin/mingw32-make.exe`) — there is no other GNU Make on
   this machine.

   **Why the `PERL=` override, not the environment variable:** Git
   Bash/MSYS auto-converts POSIX-style paths in environment variables to
   Windows form when launching a native (non-MSYS) executable like
   `mingw32-make.exe`. That reintroduces a `C:` drive-letter colon into
   `PERL5LIB`, which Perl (a Cygwin build, so colon-separated like Unix)
   then mis-splits into two bogus entries. Baking `-I<path>` into the
   `PERL` command line sidesteps env-var propagation entirely.

   **A gotcha that bit this build once, watch for it again:** an
   earlier failed `Configure`/`build_generated` attempt (before the
   `PERL=` fix above) left a few generated files behind as *empty*
   placeholders (`include/crypto/bn_conf.h`, `include/crypto/dso_conf.h`,
   `crypto/params_idx.c`, `include/internal/param_names.h`). A
   subsequent successful `make` run did **not** regenerate them, because
   by its timestamp they already "existed" — so the build proceeded on
   broken headers until it hit a hard error deep inside
   `providers/implementations/ciphers/ciphercommon_gcm.c` (missing
   `PIDX_CIPHER_PARAM_AEAD_*` constants). If `Configure` or
   `build_generated` ever fails partway through, treat any `*.h`/`*.c`
   the failed run touched as suspect: check for zero-byte files
   (`find . -size 0`) before trusting a later "successful" build, or
   just `find . -name '*.obj' -delete && find . -name '*.a' -delete` and
   rebuild clean, which is what was done here.

4. **Vendor the output**: `libcrypto.a` + `libssl.a` into
   `win64-llvm-mingw/lib/`, and `include/openssl/` (the public headers
   only — `include/internal/` and `include/crypto/` are OpenSSL's own
   private build-time headers, not part of the public API surface
   consumers link against) into `win64-llvm-mingw/include/`.

## Consuming this in CMake

`third_party/libssh2/CMakeLists.txt` (our wrapper around the vendored
libssh2 source) points `OPENSSL_ROOT_DIR` at
`win64-llvm-mingw/` here and calls `find_package(OpenSSL)`; CMake's
bundled `FindOpenSSL` module discovers `libcrypto.a`/`libssl.a` there via
its normal GNU-toolchain naming search.
