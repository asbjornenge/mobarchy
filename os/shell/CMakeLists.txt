# mobarchy-shell: the phone shell (ADR 0009). Qt Quick on layer-shell.
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#   ctest --test-dir build
cmake_minimum_required(VERSION 3.21)
# C: the protocol code from wayland-scanner.
project(mobarchy-shell VERSION 0.1 LANGUAGES C CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui GuiPrivate Quick DBus WaylandClient Multimedia Test)
find_package(PolkitQt6-1 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PAM REQUIRED IMPORTED_TARGET pam)
pkg_check_modules(XCRYPT REQUIRED IMPORTED_TARGET libxcrypt)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)  # qmldir files for the qml/ subdirectory

# Parsers with no GUI dependencies, so the tests can link them alone.
add_library(core STATIC src/desktopentry.cpp src/desktopentry.h src/deviceinfo.cpp src/deviceinfo.h
    src/wpaparse.cpp src/wpaparse.h src/icontheme.cpp src/icontheme.h)
target_link_libraries(core PUBLIC Qt6::Core)

qt_add_executable(mobarchy-shell src/main.cpp)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(mobarchy-shell
    URI Mobarchy.Shell
    VERSION 1.0
    # (QtMultimedia: the types of C++ properties, e.g. CameraSource.videoSink, for qmllint)
    DEPENDENCIES QtMultimedia
    SOURCES
        src/apps.cpp src/apps.h
        src/approvals.cpp src/approvals.h
        src/device.cpp src/device.h
        src/agent.cpp src/agent.h
        src/appstore.cpp src/appstore.h
        src/bluetooth.cpp src/bluetooth.h
        src/camera.cpp src/camera.h
        src/haptics.cpp src/haptics.h
        src/orientation.cpp src/orientation.h
        src/torch.cpp src/torch.h
        src/fingerprint.cpp src/fingerprint.h
        src/iconprovider.h
        src/inhibit.cpp src/inhibit.h
        src/keyboard.cpp src/keyboard.h
        src/clipboard.cpp src/clipboard.h
        src/logind.cpp src/logind.h
        src/mediaplayers.cpp src/mediaplayers.h
        src/notifications.cpp src/notifications.h
        src/pinlock.cpp src/pinlock.h
        src/polkitagent.cpp src/polkitagent.h
        src/rfkill.cpp src/rfkill.h
        src/screenpower.cpp src/screenpower.h
        src/wifi.cpp src/wifi.h
        src/systemstatus.cpp src/systemstatus.h
        src/talk.cpp src/talk.h
        src/themesource.cpp src/themesource.h
        src/toplevels.cpp src/toplevels.h
        src/touchguard.cpp src/touchguard.h
        src/voice.cpp src/voice.h
        src/volume.cpp src/volume.h
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/LayerWindow.qml
        qml/EdgeGesture.qml
        qml/Sheet.qml
        qml/StatusBar.qml
        qml/SideInset.qml
        qml/Edge.qml
        qml/Drawer.qml
        qml/QuickSettings.qml
        qml/LockScreen.qml
        qml/Banner.qml
        qml/Switch.qml
        qml/LevelSlider.qml
        qml/NowPlaying.qml
        qml/PinPad.qml
        qml/VolumeOsd.qml
        qml/WifiPassword.qml
        qml/ApprovalSheet.qml
        qml/AuthSheet.qml
        qml/ThemesApp.qml
        qml/TalkOverlay.qml
        qml/Home.qml
        qml/Store.qml
        qml/KeyboardPanel.qml
        qml/CopyBar.qml
        qml/CameraApp.qml
        qml/CameraView.qml
        qml/Rotation.qml
    RESOURCES
        data/emoji.json
)
qt6_generate_wayland_protocol_client_sources(mobarchy-shell
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-output-power-management-unstable-v1.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-idle-notify-v1.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocols/input-method-unstable-v2.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocols/virtual-keyboard-unstable-v1.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-data-control-unstable-v1.xml)
target_include_directories(mobarchy-shell PRIVATE src)
target_link_libraries(mobarchy-shell PRIVATE
    core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::DBus Qt6::WaylandClient Qt6::Multimedia
    PolkitQt6-1::Agent
    PkgConfig::PIPEWIRE PkgConfig::PULSE PkgConfig::PAM PkgConfig::XCRYPT PkgConfig::XKBCOMMON)

include(GNUInstallDirs)
install(TARGETS mobarchy-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The app ID of the shell's windows (portals look it up).
install(FILES data/org.mobarchy.Shell.desktop data/org.mobarchy.Store.desktop data/org.mobarchy.Camera.desktop
              data/org.mobarchy.Themes.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.mobarchy.Store.svg data/org.mobarchy.Camera.svg data/org.mobarchy.Themes.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# The themes that come with Mobarchy (ADR 0025).
install(DIRECTORY data/themes/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mobarchy/themes FILES_MATCHING PATTERN "*.json")

enable_testing()
qt_add_executable(tst_desktopentry tests/tst_desktopentry.cpp)
target_link_libraries(tst_desktopentry PRIVATE core Qt6::Test)
add_test(NAME desktopentry COMMAND tst_desktopentry)
qt_add_executable(tst_deviceinfo tests/tst_deviceinfo.cpp)
target_link_libraries(tst_deviceinfo PRIVATE core Qt6::Test)
add_test(NAME deviceinfo COMMAND tst_deviceinfo)
qt_add_executable(tst_wpaparse tests/tst_wpaparse.cpp)
target_link_libraries(tst_wpaparse PRIVATE core Qt6::Test)
add_test(NAME wpaparse COMMAND tst_wpaparse)
qt_add_executable(tst_icontheme tests/tst_icontheme.cpp)
target_link_libraries(tst_icontheme PRIVATE core Qt6::Test)
add_test(NAME icontheme COMMAND tst_icontheme)
# qmllint over the module: missing properties, wrong types, unqualified access.
add_test(NAME qmllint COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target mobarchy-shell_qmllint)
set_tests_properties(qmllint PROPERTIES FAIL_REGULAR_EXPRESSION "Warning:;Error:")
