# Run via: cmake -DTELECRAP_SSL_INST=<prefix> -DTELECRAP_SSL_DEST=<path> [-DTELECRAP_SSL_SRC=<openssl-src>] -P StageBundledOpenSSLCrypto.cmake
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED TELECRAP_SSL_INST OR NOT DEFINED TELECRAP_SSL_DEST)
    message(FATAL_ERROR "StageBundledOpenSSLCrypto: missing TELECRAP_SSL_INST or TELECRAP_SSL_DEST")
endif()

if(EXISTS "${TELECRAP_SSL_INST}/lib/libcrypto.a")
    set(_candidate "${TELECRAP_SSL_INST}/lib/libcrypto.a")
elseif(EXISTS "${TELECRAP_SSL_INST}/lib64/libcrypto.a")
    set(_candidate "${TELECRAP_SSL_INST}/lib64/libcrypto.a")
elseif(DEFINED TELECRAP_SSL_SRC AND EXISTS "${TELECRAP_SSL_SRC}/libcrypto.a")
    # BUILD_IN_SOURCE: static lib often remains in the OpenSSL tree even if prefix layout differs.
    set(_candidate "${TELECRAP_SSL_SRC}/libcrypto.a")
else()
    set(_src_line "")
    if(DEFINED TELECRAP_SSL_SRC)
        set(_src_line "        ${TELECRAP_SSL_SRC}/libcrypto.a\n")
    endif()
    message(FATAL_ERROR
        "TeleCrap: libcrypto.a not found after OpenSSL build/install.\n"
        "  Tried: ${TELECRAP_SSL_INST}/lib/libcrypto.a\n"
        "        ${TELECRAP_SSL_INST}/lib64/libcrypto.a\n"
        "${_src_line}"
        "  Inspect install prefix and OpenSSL source tree.")
endif()

cmake_path(GET TELECRAP_SSL_DEST PARENT_PATH _dest_dir)
file(MAKE_DIRECTORY "${_dest_dir}")
configure_file("${_candidate}" "${TELECRAP_SSL_DEST}" COPYONLY)
