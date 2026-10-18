# TODO

Concrete, unclaimed work a contributor could pick up. Not a full roadmap —
see the README for the broader feature picture and what's deliberately
out of scope for now. Open an issue before starting on any of these
(see [CONTRIBUTING.md](CONTRIBUTING.md)) so effort doesn't collide.

## Next release (0.7)

- **X11 forwarding for SSH**: a per-connection flag in the connection
  settings (default: on) that requests X11 forwarding on the SSH channel,
  so remote X clients display on the machine running lvdterm.
- **Local X server**: bundle/launch a local X server so those forwarded
  remote windows have somewhere to display (mainly for Windows, which has
  none of its own).
- **RDP and VNC connections**: new connection types, displayed in a tab
  like the existing SSH/Telnet/Serial ones.
- **Detach a tab**: move a tab out into its own separate window, and
  presumably drag it back.

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

## Linux packaging ✅ .deb and AppImage both done

lvdterm now builds and runs on Linux from source (`linux`/`linux-release`
CMake presets - see the README's "Building from source" section).

**`.deb` - done**: `CMakeLists.txt` wires up CPack's Debian generator
(gated `if(NOT WIN32)` - Windows packaging still goes entirely through
`windeployqt`/`installer/lvdterm.iss`, untouched). `cmake --build
--preset linux-release && cpack` (run from inside `build/linux-release/`)
produces `lvdterm-<version>-Linux.deb`, installing to `/usr/bin/lvdterm`
+ the `.desktop`/icon from `packaging/linux/` via proper `install()`
rules (new - there were none before this). Runtime dependencies
(`libqt6core6t64`, `libssl3t64`, `libsecret-1-0`, etc.) are auto-detected
from the actual built binary via `dpkg-shlibdeps`
(`CPACK_DEBIAN_PACKAGE_SHLIBDEPS`) rather than hand-listed - deliberately,
since distros rename these across releases (Debian trixie's own
time64-transition renames being a real, caught-in-testing example of
exactly why hardcoding would silently go stale). `libssh2`/`qtermwidget`
(statically linked, nothing of their own to ship) are excluded from the
install set via `add_subdirectory(... EXCLUDE_FROM_ALL)` - without it,
libssh2's own stale `install()` rules (referencing a `NEWS` file this
vendored copy doesn't have) broke packaging outright.

Verified end-to-end in a real environment (not just "it configures"):
built, packaged, `dpkg -i`'d, launched the actually-installed
`/usr/bin/lvdterm` (not just the build-tree binary), confirmed the
`.desktop`/icon landed in the right XDG locations, then `dpkg -r`'d and
confirmed a fully clean removal. One real snag found along the way:
building/packaging from a Windows-mounted drive under WSL (DrvFs, no
`metadata` mount option) silently reports every file as `777` - `cmake
--install`'s `PERMISSIONS` arguments are now explicit for exactly this
reason, but the actual package has to be *built* from a native Linux
filesystem to get correct permissions at all (DrvFs can't store real
Unix permission bits no matter what CMake asks for) - worth remembering
if this gets wired into CI on a Windows runner someday, though a normal
Linux CI runner/dev machine never hits this.

**AppImage - done**: bundles the binary + its Qt/OpenSSL/libsecret/glib
runtime deps into a single, distro-agnostic, no-install-needed file, the
closest Linux equivalent to the Windows portable zip
(`installer/build-portable.bat`) - unlike the `.deb`, which needs the
distro's own matching packages already present. Built with `linuxdeploy`
+ `linuxdeploy-plugin-qt` (downloaded tools, not vendored - no CMake
integration for this one, it's a separate manual step for now):

```sh
cmake --install build/linux-release --prefix AppDir/usr
QMAKE=/usr/bin/qmake6 APPIMAGE_EXTRACT_AND_RUN=1 linuxdeploy-x86_64.AppImage \
  --appdir AppDir --plugin qt --output appimage
```

(`APPIMAGE_EXTRACT_AND_RUN=1` rather than the `--appimage-extract-and-run`
CLI flag - linuxdeploy invokes the Qt plugin as its own subprocess
AppImage, and only the env var reaches that child process too; WSL has
no FUSE by default, which either form works around.)

Verified two ways, not just "it built": (1) confirmed the running
process actually loads its libraries from the AppImage's own extracted
`usr/lib/`, not the system's copies (checked `/proc/<pid>/maps`) - this
machine already has every runtime dependency installed system-wide, so a
successful launch alone wouldn't have proven real self-containment, only
this did; (2) a live SSH connection test (see below) exercised the exact
same binary inside the AppImage.

Reuses the existing `packaging/linux/lvdterm.desktop` +
`src/resources/icons/app_icon.svg` rather than duplicating them.

**A real compatibility bug found by actually testing on a second
machine, not just the build machine**: the first AppImage (built on
Debian trixie, a rolling/bleeding-edge distro) failed to even start
elsewhere - `GLIBC_2.38' not found`, `GLIBCXX_3.4.32' not found` - on a
real Ubuntu 22.04 target (glibc 2.35). glibc symbol versioning is
forward-only, so a bundled Qt/OpenSSL/glib doesn't help if the *build
machine's own* glibc/libstdc++ ceiling is higher than the target's -
and a brand-new distro is exactly the wrong choice of build machine for
something meant to run broadly. Rebuilt instead on Ubuntu 20.04 LTS
(glibc 2.31, GCC 9) using Qt's own official prebuilt binaries via
`aqtinstall` (`pip install aqtinstall && aqt install-qt linux desktop
6.5.3 gcc_64 -m qtserialport`) rather than that distro's packages, since
Ubuntu 20.04 predates Qt6 in its own repos entirely - and Qt's official
Linux builds are themselves built against an old RHEL baseline
specifically for broad compatibility (`libQt6Core.so.6` needs glibc
2.28 at most, lower than Ubuntu 20.04's *own* 2.31). Checked what was
actually produced rather than assuming
(`objdump -T dist/lvdterm | grep -oE 'GLIBC(XX)?_[0-9.]+|CXXABI_[0-9.]+'
| sort -V`): max requirement `GLIBC_2.25`/`GLIBCXX_3.4.22`/
`CXXABI_1.3.9`, comfortably below the failing target's 2.35. Full recipe
in [`packaging/linux/README.md`](packaging/linux/README.md).

Re-verified on the actual host that originally failed (not just
reasoned about it): copied the rebuilt AppImage over, ran it there - no
glibc errors at all this time, and it launched fully against that
machine's own real local X display (not a mock/headless check),
reaching the same successful-startup log output seen on every other
platform this project runs on.

**A real end-to-end functional test, not just a launch check**: built a
small standalone program linking this project's actual `SshTransport`/
libssh2 code (the same classes the real app uses, not a mock) and used
it to connect to a real SSH server with password auth - genuinely
connected, correctly showed and auto-accepted the known-hosts prompt for
a never-before-seen host (the real first-connection path, not special-
cased around), received the real shell banner, sent a command, and got
the exact expected output back. Confirms the Linux build's SSH stack
isn't just compiling - it works against a real remote host.

## Macro automation

Noted directly as a gap versus Tera Term (its own scripting language,
TTL) - lvdterm has neither
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
