# TODO

Concrete, unclaimed work a contributor could pick up. Not a full roadmap —
see the README for the broader feature picture and what's deliberately
out of scope for now. Open an issue before starting on either of these
(see [CONTRIBUTING.md](CONTRIBUTING.md)) so effort doesn't collide.

## Build OpenSSL from source instead of vendoring a prebuilt binary

`third_party/openssl-prebuilt/` currently ships committed `libcrypto.a`/
`libssl.a` binaries (and their public headers) rather than being built by
this project's own CMake, unlike every other vendored dependency
(`third_party/libssh2/`, `third_party/qtermwidget/`). The full recipe
that produced them - including the exact problems hit along the way - is
documented in
[`third_party/openssl-prebuilt/BUILDING.md`](third_party/openssl-prebuilt/BUILDING.md):

- OpenSSL's build system is a Perl `Configure` script + generated
  `Makefile`, not CMake.
- Git for Windows' bundled Perl is missing several CPAN modules
  `Configure` needs, and its `cpan` tool is itself broken on this
  machine - the recipe works around this by vendoring a handful of
  pure-Perl module trees and pointing `PERL5LIB` at them by hand.
- A `PERL=` `make` command-line override is needed instead of the
  `PERL5LIB` environment variable, because Git Bash/MSYS mangles POSIX
  paths in env vars when launching a native (non-MSYS) `mingw32-make.exe`.

**Why this matters**: a prebuilt binary in the repo is exactly the kind
of thing a security-conscious build/review process flags - nobody
downstream can verify what actually went into it just by reading this
repo's own build scripts, and the multi-step Perl workaround being
undocumented-in-CMake means it only exists in one person's head (now
written down, but not automated).

**The actual task**: turn that manual recipe into a CMake step that runs
automatically as part of configuring/building lvdterm - most likely
`ExternalProject_Add` invoking `Configure`/`make` with the same flags,
gated behind whatever Perl-availability checks are needed, replacing the
committed binaries with a real build. Bonus points for finding a way to
avoid the Perl-module vendoring workaround entirely (a newer/different
Perl on the build machine, or an alternate build path OpenSSL supports)
rather than reproducing it as-is in CMake.

## Macro automation

Noted directly as a gap versus Tera Term (its own scripting language,
TTL) and MobaXTerm (macros + multi-exec broadcast) - lvdterm has neither
today. Rough shape, open for a contributor to actually design:

- Record and replay a sequence of keystrokes/commands sent to a pane
  (the simplest version), and/or
- Run a predefined command sequence automatically once a connection
  reaches `Connected` (useful for e.g. `sudo -i`, `cd` into a working
  directory, or setting up a remote tmux/screen session every time).

Whatever shape this takes, it should compose with panes the same way
everything else does - `TerminalSession`/`TerminalView` are the layer
that already owns writing bytes to a `Transport`, so a macro is really
"another source of bytes to send," not a new architectural layer.
