cmake_minimum_required(VERSION 3.27)
# Mobarchy's Hyprland plugin (ADR 0045): the screen turns as the device does, animated.
project(mobarchy-hyprland VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET hyprland libdrm libinput libudev pangocairo pixman-1 wayland-server xkbcommon)

add_library(mobarchy-hyprland SHARED src/main.cpp)
target_link_libraries(mobarchy-hyprland PRIVATE PkgConfig::deps)
target_compile_definitions(mobarchy-hyprland PRIVATE WLR_USE_UNSTABLE)

install(TARGETS mobarchy-hyprland LIBRARY DESTINATION lib/mobarchy/hyprland)
