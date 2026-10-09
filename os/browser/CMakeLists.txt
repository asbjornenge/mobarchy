# mobarchy-browser: Mobarchy's browser (ADR 0023). QtWebEngine with a phone's chrome.
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
cmake_minimum_required(VERSION 3.21)
project(mobarchy-browser VERSION 0.1 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Quick DBus WebEngineQuick)
qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(mobarchy-browser src/main.cpp src/browserbus.cpp src/browserbus.h)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(mobarchy-browser
    URI Mobarchy.Browser
    VERSION 1.0
    SOURCES src/onepassword.cpp src/onepassword.h src/themesource.cpp src/themesource.h
    QML_FILES
        qml/Main.qml
        qml/WebApp.qml
        qml/Theme.qml
        qml/BottomBar.qml
        qml/AddressSheet.qml
        qml/TabsView.qml
        qml/MenuSheet.qml
        qml/BarButton.qml
        qml/LoginSheet.qml
)
target_include_directories(mobarchy-browser PRIVATE src)
target_link_libraries(mobarchy-browser PRIVATE Qt6::Gui Qt6::Quick Qt6::DBus Qt6::WebEngineQuick)

install(TARGETS mobarchy-browser RUNTIME DESTINATION bin)
install(FILES org.mobarchy.Browser.desktop DESTINATION share/applications)
install(PROGRAMS helper/op-session DESTINATION lib/mobarchy-browser)
target_compile_definitions(mobarchy-browser PRIVATE OP_SESSION="${CMAKE_INSTALL_PREFIX}/lib/mobarchy-browser/op-session")
install(FILES org.mobarchy.Browser.svg DESTINATION share/icons/hicolor/scalable/apps)
