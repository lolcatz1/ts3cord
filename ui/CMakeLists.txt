cmake_minimum_required(VERSION 3.16)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION TS3CORD_VERSION LIMIT_COUNT 1)
project(ts3cord VERSION ${TS3CORD_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Svg Network Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_executable(ts3cord
  src/main.cpp
  src/skin.cpp
  src/corelink.cpp
  src/store.cpp
  src/tree.cpp
  src/infoframe.cpp
  src/chat.cpp
  src/statusbar.cpp
  src/serverview.cpp
  src/mainwindow.cpp
  src/dialogs.cpp
  src/avatars.cpp
  src/mpvwidget.cpp
  ts3cord.qrc
)
target_link_libraries(ts3cord Qt5::Widgets Qt5::Svg Qt5::Network Qt5::Multimedia PkgConfig::QRENCODE PkgConfig::MPV)
target_compile_definitions(ts3cord PRIVATE TS3CORD_VERSION="${PROJECT_VERSION}")
