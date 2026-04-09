add_executable(telecrap_client
    src/MemoryCache.cpp
    src/TeleCrap.Client.cpp
    src/TerminalUI.cpp
)

if(WIN32)
    target_sources(telecrap_client PRIVATE src/TerminalUI.Win.cpp)
else()
    target_sources(telecrap_client PRIVATE src/TerminalUI.Linux.cpp)
endif()

target_include_directories(telecrap_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(telecrap_client
    PRIVATE
        WIN32_LEAN_AND_MEAN
        _WINSOCK_DEPRECATED_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(telecrap_client
    PRIVATE
        telecrap_shared
)

if(UNIX AND NOT APPLE)
    target_link_libraries(telecrap_client PRIVATE pthread dl)
endif()

set_target_properties(telecrap_client PROPERTIES OUTPUT_NAME "telecrap_tui")

add_custom_command(TARGET telecrap_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/settings.cfg
        $<TARGET_FILE_DIR:telecrap_client>/settings.cfg
)
