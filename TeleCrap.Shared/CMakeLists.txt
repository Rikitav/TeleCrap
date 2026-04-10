include(ExternalProject)

add_library(telecrap_shared STATIC
    Console.cpp
    Log.cpp
    Protocol.cpp
    Request.cpp
    Responce.cpp
    SecureChannel.cpp
    SocketHelper.cpp
    Transport.cpp
)

set(TELECRAP_BUNDLED_OPENSSL_ROOT "${CMAKE_SOURCE_DIR}/third-party/openssl-3.6.2")
if(EXISTS "${TELECRAP_BUNDLED_OPENSSL_ROOT}/Configure")
    find_program(TELECRAP_PERL_EXE perl REQUIRED)
    set(TELECRAP_OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/third-party/openssl/install")
    set(TELECRAP_OPENSSL_INCLUDE_DIR "${TELECRAP_OPENSSL_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${TELECRAP_OPENSSL_INCLUDE_DIR}")

    if(WIN32)
        set(TELECRAP_OPENSSL_CRYPTO_LIB "${TELECRAP_OPENSSL_INSTALL_DIR}/lib/libcrypto.lib")
    else()
        set(TELECRAP_OPENSSL_CRYPTO_LIB "${TELECRAP_OPENSSL_INSTALL_DIR}/lib64/libcrypto.a")
    endif()

    if(WIN32)
        ExternalProject_Add(telecrap_openssl_ext
            SOURCE_DIR "${TELECRAP_BUNDLED_OPENSSL_ROOT}"
            BUILD_IN_SOURCE 1
            CONFIGURE_COMMAND ${TELECRAP_PERL_EXE} Configure VC-WIN64A no-shared no-tests --prefix=${TELECRAP_OPENSSL_INSTALL_DIR} --openssldir=${TELECRAP_OPENSSL_INSTALL_DIR}/ssl
            BUILD_COMMAND nmake
            INSTALL_COMMAND nmake install_sw
            BUILD_BYPRODUCTS "${TELECRAP_OPENSSL_CRYPTO_LIB}"
        )
    else()
        # Не ./Configure: в git checkout часто нет +x → Permission denied. Configure — это Perl-скрипт.
        ExternalProject_Add(telecrap_openssl_ext
            SOURCE_DIR "${TELECRAP_BUNDLED_OPENSSL_ROOT}"
            BUILD_IN_SOURCE 1
            CONFIGURE_COMMAND ${TELECRAP_PERL_EXE} Configure linux-x86_64 no-shared no-tests --prefix=${TELECRAP_OPENSSL_INSTALL_DIR} --openssldir=${TELECRAP_OPENSSL_INSTALL_DIR}/ssl
            BUILD_COMMAND make -j
            INSTALL_COMMAND make install_sw
            BUILD_BYPRODUCTS "${TELECRAP_OPENSSL_CRYPTO_LIB}"
        )
    endif()

    add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${TELECRAP_OPENSSL_CRYPTO_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${TELECRAP_OPENSSL_INCLUDE_DIR}"
    )
    add_dependencies(OpenSSL::Crypto telecrap_openssl_ext)
else()
    find_package(OpenSSL QUIET)
    if(NOT OpenSSL_FOUND)
        message(FATAL_ERROR
            "OpenSSL not found.\n"
            "Either add OpenSSL sources to '${TELECRAP_BUNDLED_OPENSSL_ROOT}' "
            "or pass -DOPENSSL_ROOT_DIR=<path> to prebuilt OpenSSL.")
    endif()
endif()

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
    # Static libcrypto uses CryptoAPI cert store (e_capi, winstore) — needs Cert* from crypt32.
    target_link_libraries(telecrap_shared PUBLIC ws2_32 crypt32 OpenSSL::Crypto)
else()
    target_link_libraries(telecrap_shared PUBLIC OpenSSL::Crypto)
endif()
