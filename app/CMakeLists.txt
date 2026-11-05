# Ghost Engine - the desktop app: a playlist library, editor and settings for
# the mpv setup next to it. Qt Widgets only, so it stays a small window.
cmake_minimum_required(VERSION 3.21)
project(GhostEngine VERSION 1.3.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(GhostEngine WIN32
    src/main.cpp
    src/playlist.h src/playlist.cpp
    src/library.h src/library.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/theme.h src/theme.cpp
    src/thumbnails.h src/thumbnails.cpp
    src/youtube.h src/youtube.cpp
    src/resolver.h src/resolver.cpp
    src/mpvclient.h src/mpvclient.cpp
    src/tiles.h src/tiles.cpp
    src/nowplaying.h src/nowplaying.cpp
    src/icons.h src/icons.cpp
    src/covercrop.h src/covercrop.cpp
    src/settings.h src/settings.cpp
    src/lyrics.h src/lyrics.cpp
    src/offline.h src/offline.cpp
    src/videopage.h src/videopage.cpp
    src/songinfo.h src/songinfo.cpp
    src/nowplayingpanel.h src/nowplayingpanel.cpp
    src/ytmusic.h src/ytmusic.cpp
    src/searchpage.h src/searchpage.cpp
    src/elidedlabel.h
    src/shortcuts.h src/shortcuts.cpp
    src/profile.h src/profile.cpp
    src/updates.h src/updates.cpp
    src/traymenu.h src/traymenu.cpp
    ghostengine.rc
)
qt_add_resources(GhostEngine "assets" PREFIX "/" FILES assets/chevron-down.png)
target_link_libraries(GhostEngine PRIVATE Qt6::Widgets Qt6::Network dwmapi dbghelp)
# A .pdb with the release build, so a crash log names the function and line.
if(MSVC)
    target_compile_options(GhostEngine PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(GhostEngine PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()
target_compile_definitions(GhostEngine PRIVATE GHOST_VERSION="${PROJECT_VERSION}")
