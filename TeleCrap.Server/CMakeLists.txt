add_executable(telecrap_server
    src/AddonManager.cpp
    src/Backend.cpp
    src/Database.cpp
    src/ServerCLI.cpp
    src/TeleCrap.Server.cpp
)

if(WIN32)
    target_sources(telecrap_server PRIVATE src/ServerCLI.Win.cpp)
else()
    target_sources(telecrap_server PRIVATE src/ServerCLI.Linux.cpp)
endif()

target_include_directories(telecrap_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(telecrap_server
    PRIVATE
        WIN32_LEAN_AND_MEAN
        _WINSOCK_DEPRECATED_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS
        SOL_USING_CXX_LUA=1
        SOL_ALL_SAFETIES_ON=1
)

target_link_libraries(telecrap_server
    PRIVATE
        telecrap_shared
        telecrap_libs_sqlite
        telecrap_libs_sol
)

if(UNIX AND NOT APPLE)
    target_link_libraries(telecrap_server PRIVATE pthread dl m)
endif()

set_target_properties(telecrap_server PROPERTIES OUTPUT_NAME "TeleCrap_server")

add_custom_command(TARGET telecrap_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/addons
        $<TARGET_FILE_DIR:telecrap_server>/addons
)
