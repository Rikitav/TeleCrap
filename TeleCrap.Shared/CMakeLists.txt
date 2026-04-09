add_library(telecrap_shared STATIC
    Console.cpp
    Log.cpp
    Protocol.cpp
    Request.cpp
    Responce.cpp
    SocketHelper.cpp
    Transport.cpp
)

target_include_directories(telecrap_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(telecrap_shared
    PRIVATE
        WIN32_LEAN_AND_MEAN
        _WINSOCK_DEPRECATED_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS
)

if(WIN32)
    target_link_libraries(telecrap_shared PUBLIC ws2_32)
endif()
