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
    if(NOT WIN32)
        find_program(TELECRAP_GNU_MAKE_EXE NAMES gmake make REQUIRED)
    endif()
    set(TELECRAP_OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/third-party/openssl/install")
    set(TELECRAP_OPENSSL_INCLUDE_DIR "${TELECRAP_OPENSSL_INSTALL_DIR}/include")
    file(MAKE_DIRECTORY "${TELECRAP_OPENSSL_INCLUDE_DIR}")

    if(WIN32)
        set(TELECRAP_OPENSSL_CRYPTO_LIB "${TELECRAP_OPENSSL_INSTALL_DIR}/lib/libcrypto.lib")
    else()
        # Preferred install path (Configure --libdir=lib); staging script also checks lib64.
        set(TELECRAP_OPENSSL_CRYPTO_LIB "${TELECRAP_OPENSSL_INSTALL_DIR}/lib/libcrypto.a")
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
        # Do not use BUILD_BYPRODUCTS for the install-prefix .a: that file is created by install_sw, not the
        # build step, and Unix Makefiles then list libcrypto.a as a link input with no rule to build it.
        ExternalProject_Add(telecrap_openssl_ext
            SOURCE_DIR "${TELECRAP_BUNDLED_OPENSSL_ROOT}"
            BUILD_IN_SOURCE 1
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir "${TELECRAP_BUNDLED_OPENSSL_ROOT}"
                sh -c "rm -f makefile Makefile && perl Configure linux-x86_64 no-shared no-tests --prefix=\"${TELECRAP_OPENSSL_INSTALL_DIR}\" --openssldir=\"${TELECRAP_OPENSSL_INSTALL_DIR}/ssl\" --libdir=lib"
            BUILD_COMMAND ${TELECRAP_GNU_MAKE_EXE} -j
            INSTALL_COMMAND ${TELECRAP_GNU_MAKE_EXE} install_sw
        )
    endif()

    if(WIN32)
        set(TELECRAP_OPENSSL_CRYPTO_FOR_LINK "${TELECRAP_OPENSSL_CRYPTO_LIB}")
    else()
        ExternalProject_Get_Property(telecrap_openssl_ext STAMP_DIR)
        set(TELECRAP_OPENSSL_INSTALL_STAMP "${STAMP_DIR}/telecrap_openssl_ext-install")
        set(TELECRAP_OPENSSL_CRYPTO_FOR_LINK "${CMAKE_CURRENT_BINARY_DIR}/telecrap_openssl_import/libcrypto.a")
        add_custom_command(
            OUTPUT "${TELECRAP_OPENSSL_CRYPTO_FOR_LINK}"
            COMMAND ${CMAKE_COMMAND}
                -DTELECRAP_SSL_INST=${TELECRAP_OPENSSL_INSTALL_DIR}
                -DTELECRAP_SSL_DEST=${TELECRAP_OPENSSL_CRYPTO_FOR_LINK}
                -DTELECRAP_SSL_SRC=${TELECRAP_BUNDLED_OPENSSL_ROOT}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageBundledOpenSSLCrypto.cmake"
            DEPENDS "${TELECRAP_OPENSSL_INSTALL_STAMP}"
            COMMENT "Staging bundled libcrypto.a for link rules (Unix Makefiles)"
            VERBATIM
        )
        add_custom_target(telecrap_openssl_crypto_import
            DEPENDS "${TELECRAP_OPENSSL_CRYPTO_FOR_LINK}" telecrap_openssl_ext)
    endif()

    add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${TELECRAP_OPENSSL_CRYPTO_FOR_LINK}"
        INTERFACE_INCLUDE_DIRECTORIES "${TELECRAP_OPENSSL_INCLUDE_DIR}"
    )
    if(WIN32)
        add_dependencies(OpenSSL::Crypto telecrap_openssl_ext)
        add_dependencies(telecrap_shared telecrap_openssl_ext)
    else()
        add_dependencies(OpenSSL::Crypto telecrap_openssl_crypto_import)
        add_dependencies(telecrap_shared telecrap_openssl_crypto_import)
    endif()
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
