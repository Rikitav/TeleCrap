cmake_minimum_required(VERSION 4.2)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(TeleCrap LANGUAGES C CXX)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

project(TeleCrap LANGUAGES C CXX)

add_subdirectory(third-party/sqlite3)
add_subdirectory(third-party/lualib-5.4)
add_subdirectory(third-party/terminality)

add_subdirectory(TeleCrap.Shared)
#add_subdirectory(TeleCrap.Server)
#add_subdirectory(TeleCrap.Client)
