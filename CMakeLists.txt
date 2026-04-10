cmake_minimum_required(VERSION 3.20)

project(TeleCrap LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

option(TELECRAP_TUI_USE_CURSES "Build client TUI with curses backend" ON)
option(TELECRAP_USE_BUNDLED_PDCURSES "Build bundled PDCurses on Windows" ON)

add_subdirectory(third-party/sqlite3)
add_subdirectory(third-party/lualib-5.4)
add_subdirectory(TeleCrap.Shared)
add_subdirectory(TeleCrap.Server)
add_subdirectory(TeleCrap.Client)
