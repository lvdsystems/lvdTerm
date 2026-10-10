# Linux packaging

A proper `.deb` is the preferred way to get this installed - see the
README's "Building from source" Linux section:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
cd build/linux-release && cpack
sudo dpkg -i lvdterm-*-Linux.deb
```

That installs the binary, `lvdterm.desktop`, and the icon to their
proper system locations (`/usr/bin`, `/usr/share/applications`,
`/usr/share/icons/...`) with correctly auto-detected runtime
dependencies - nothing below is needed if you do this.

## AppImage

A single, self-contained, distro-agnostic executable - no matching
distro packages needed on the machine that runs it, unlike the `.deb`.
Built with `linuxdeploy` + `linuxdeploy-plugin-qt` (downloaded, not
vendored):

```sh
cmake --preset linux-release
cmake --build --preset linux-release
cmake --install build/linux-release --prefix AppDir/usr
QMAKE=/usr/bin/qmake6 APPIMAGE_EXTRACT_AND_RUN=1 linuxdeploy-x86_64.AppImage \
  --appdir AppDir --plugin qt --output appimage
```

(`APPIMAGE_EXTRACT_AND_RUN=1` rather than the `--appimage-extract-and-run`
CLI flag - linuxdeploy invokes the Qt plugin as its own subprocess
AppImage, and only the env var reaches that child process too; needed on
any system without FUSE, e.g. WSL by default.)

**The glibc/libstdc++ compatibility trap** - the one thing that matters
most about *which* machine you build this on: an AppImage bundles the
*application's* shared library dependencies (Qt, OpenSSL, libsecret,
glib, ...), but still dynamically links against whatever glibc/libstdc++
the *build* machine has - and glibc symbol versioning is forward-only
(a binary built against glibc 2.38 will refuse to run anywhere with an
older glibc; one built against 2.28 runs fine on 2.28 *or newer*). Build
on a modern rolling/bleeding-edge distro (Debian trixie, Arch, ...) and
the AppImage will fail with `version GLIBC_2.38' not found` on anything
less brand new - defeating the entire point of an AppImage. Build
instead on the *oldest* reasonable base:

- Use an old-but-still-supported distro for the build itself (Ubuntu
  20.04 LTS worked well - glibc 2.31, GCC 9).
- Get Qt from **Qt's own official prebuilt binaries**, not that distro's
  package manager (Ubuntu 20.04 predates Qt6 in its own repos entirely).
  [`aqtinstall`](https://github.com/miniupnp/aqtinstall) makes this a
  one-liner, and Qt's official Linux builds are themselves built against
  an old RHEL baseline specifically for broad compatibility (Qt 6.5.3's
  own `libQt6Core.so.6` needs glibc 2.28 at most - lower than Ubuntu
  20.04's *own* 2.31):
  ```sh
  pip install aqtinstall
  aqt install-qt linux desktop 6.5.3 gcc_64 -m qtserialport -O ~/Qt
  # then point CMake at it instead of relying on system Qt:
  cmake -B build/legacy -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=~/Qt/6.5.3/gcc_64
  ```
- Verify what you actually built, don't just assume: `objdump -T
  dist/lvdterm | grep -oE 'GLIBC(XX)?_[0-9.]+|CXXABI_[0-9.]+' | sort -V`
  shows the real ceiling (this recipe's result: `GLIBC_2.25`/
  `GLIBCXX_3.4.22`/`CXXABI_1.3.9` - comfortably below any Linux from the
  last several years).

## Manual install (without `cpack`)

If you'd rather just run a locally-built binary without packaging it -
this only gets you the desktop/launcher icon, not a system-wide install:

```sh
install -Dm644 lvdterm.desktop ~/.local/share/applications/lvdterm.desktop
install -Dm644 ../../src/resources/icons/app_icon.svg ~/.local/share/icons/hicolor/scalable/apps/lvdterm.svg
# Point Exec= at wherever you actually built the binary, e.g.:
sed -i "s|^Exec=lvdterm|Exec=$(pwd)/../../build/linux-release/dist/lvdterm|" ~/.local/share/applications/lvdterm.desktop
update-desktop-database ~/.local/share/applications 2>/dev/null || true
```
