# WIN32: builds as a GUI subsystem executable, so no console window pops
# up alongside the app on launch (a plain add_executable() on Windows
# defaults to the console subsystem, which allocates one even though
# nothing here uses stdin and every real diagnostic tool this project
# actually relies on - lldb, standalone test binaries - doesn't need it
# either). qDebug()/qWarning() calls keep working exactly as before; they
# just have nowhere visible to print to when nothing attaches a console.
add_executable(lvdterm WIN32
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
    app/Theme.cpp
    app/Theme.h
    terminal/ColorSchemes.cpp
    terminal/ColorSchemes.h
    terminal/KeyboardProfiles.cpp
    terminal/KeyboardProfiles.h
    terminal/TerminalSession.cpp
    terminal/TerminalSession.h
    terminal/TerminalView.cpp
    terminal/TerminalView.h
    core/AppSettings.cpp
    core/AppSettings.h
    core/ConnectionProfile.cpp
    core/ConnectionProfile.h
    core/Crypto.cpp
    core/Crypto.h
    core/ProfileStore.cpp
    core/ProfileStore.h
    core/SessionStore.cpp
    core/SessionStore.h
    core/StateIcon.cpp
    core/StateIcon.h
    core/ReconnectPolicy.h
    core/Transport.h
    transports/loopback/LoopbackTransport.h
    transports/serial/SerialPortSettings.h
    transports/serial/SerialTransport.cpp
    transports/serial/SerialTransport.h
    transports/telnet/TelnetTransport.cpp
    transports/telnet/TelnetTransport.h
    transports/raw/RawTransport.cpp
    transports/raw/RawTransport.h
    transports/ssh/SshConnectHelper.cpp
    transports/ssh/SshConnectHelper.h
    transports/ssh/SshConnectionSettings.h
    transports/ssh/SshTransport.cpp
    transports/ssh/SshTransport.h
    transports/ssh/SshWorker.cpp
    transports/ssh/SshWorker.h
    sftp/SftpClient.cpp
    sftp/SftpClient.h
    sftp/SftpModel.cpp
    sftp/SftpModel.h
    sftp/SftpSession.cpp
    sftp/SftpSession.h
    ui/ConnectionEditDialog.cpp
    ui/ConnectionEditDialog.h
    ui/ConnectionTreeDock.cpp
    ui/ConnectionTreeDock.h
    ui/SerialConnectDialog.cpp
    ui/SerialConnectDialog.h
    ui/SerialSettingsWidget.cpp
    ui/SerialSettingsWidget.h
    ui/SettingsDialog.cpp
    ui/SettingsDialog.h
    ui/SftpDock.cpp
    ui/SftpDock.h
    ui/BrandingWidget.cpp
    ui/BrandingWidget.h
    ui/WelcomeWidget.cpp
    ui/WelcomeWidget.h
    ui/AboutDialog.cpp
    ui/AboutDialog.h
    ui/SplitPicker.cpp
    ui/SplitPicker.h
    resources/license.qrc
    resources/branding.qrc
    resources/icons.qrc
    resources/app.rc
    ui/SshConnectDialog.cpp
    ui/SshConnectDialog.h
    ui/SshSettingsWidget.cpp
    ui/SshSettingsWidget.h
    ui/TelnetConnectDialog.cpp
    ui/TelnetConnectDialog.h
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)

target_include_directories(lvdterm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# A dedicated output folder (rather than this dir, which is full of
# CMakeFiles/ object soup) so windeployqt's output below is immediately
# ready to zip up or hand to installer/lvdterm.iss.
set_target_properties(lvdterm PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

target_link_libraries(lvdterm PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::SerialPort
    qtermwidget_core
    libssh2::libssh2
    # libssh2/OpenSSL link their Windows system-lib dependencies PRIVATEly
    # to their own static-lib targets, which CMake does not chain through
    # more than one level of static linking - so lvdterm has to name them
    # again itself. ws2_32 for sockets (libssh2), gdi32/crypt32/bcrypt for
    # OpenSSL's Windows entropy/cert-store calls.
    ws2_32
    gdi32
    crypt32
    bcrypt
)

# Bundle the Qt runtime DLLs next to lvdterm.exe automatically, instead of
# needing a manual windeployqt run after every build.
find_program(WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt6 windeployqt
    HINTS "${Qt6_DIR}/../../../bin"
)

if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET lvdterm POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:lvdterm>"
        COMMENT "Running windeployqt to bundle Qt runtime DLLs next to lvdterm.exe"
        VERBATIM
    )
else()
    message(WARNING "windeployqt not found - Qt runtime DLLs won't be bundled automatically; run it manually after building")
endif()
