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

add_subdirectory(TeleCrap.Libs.SQLite)
add_subdirectory(TeleCrap.Libs.Sol)
add_subdirectory(TeleCrap.Shared)
add_subdirectory(TeleCrap.Server)
add_subdirectory(TeleCrap.Client)
