# Ghost Engine - the desktop app: a playlist library, editor and settings for
# the mpv setup next to it. Qt Widgets only, so it stays a small window.
cmake_minimum_required(VERSION 3.21)
project(GhostEngine VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(GhostEngine WIN32
    src/main.cpp
    src/playlist.h src/playlist.cpp
    src/library.h src/library.cpp
    src/mainwindow.h src/mainwindow.cpp
)
target_link_libraries(GhostEngine PRIVATE Qt6::Widgets)
target_compile_definitions(GhostEngine PRIVATE GHOST_VERSION="${PROJECT_VERSION}")
