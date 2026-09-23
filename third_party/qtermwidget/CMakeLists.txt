# Vendored, pty-stripped QTermWidget terminal-emulation core.
# See VENDORING.md for what was kept, dropped and patched, and why.

add_library(qtermwidget_core STATIC
    BlockArray.cpp
    Emulation.cpp
    Filter.cpp
    History.cpp
    HistorySearch.cpp
    Hyperlink.cpp
    KeyboardTranslator.cpp
    Screen.cpp
    ScreenWindow.cpp
    TerminalCharacterDecoder.cpp
    TerminalDisplay.cpp
    Vt102Emulation.cpp
    konsole_wcwidth.cpp
    tools.cpp
)

target_include_directories(qtermwidget_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(qtermwidget_core PUBLIC
    Qt6::Widgets
)

# Upstream installs kb-layouts/color-schemes to fixed *NIX data dirs and
# bakes their path in via these macros (tools.cpp). lvdterm has no such
# install location on Windows; these are deliberately non-existent paths
# so tools.cpp's QDir::exists() check fails and falls through to the
# app-relative "kb-layouts/"/"color-schemes/" lookup added for Q_OS_WIN
# (and KeyboardTranslator falls back to the compiled-in default keytab
# regardless). See VENDORING.md.
target_compile_definitions(qtermwidget_core PRIVATE
    "KB_LAYOUT_DIR=\"lvdterm-no-kb-layout-dir\""
    "COLORSCHEMES_DIR=\"lvdterm-no-colorschemes-dir\""
)
