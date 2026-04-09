set(TELECRAP_LUA_SOURCES
    src/lapi.c
    src/lauxlib.c
    src/lbaselib.c
    src/lcode.c
    src/lcorolib.c
    src/lctype.c
    src/ldblib.c
    src/ldebug.c
    src/ldo.c
    src/ldump.c
    src/lfunc.c
    src/lgc.c
    src/linit.c
    src/liolib.c
    src/llex.c
    src/lmathlib.c
    src/lmem.c
    src/loadlib.c
    src/lobject.c
    src/lopcodes.c
    src/loslib.c
    src/lparser.c
    src/lstate.c
    src/lstring.c
    src/lstrlib.c
    src/ltable.c
    src/ltablib.c
    src/ltm.c
    src/lua.c
    src/lundump.c
    src/lutf8lib.c
    src/lvm.c
    src/lzio.c
)

add_library(telecrap_libs_sol STATIC ${TELECRAP_LUA_SOURCES})

set_source_files_properties(${TELECRAP_LUA_SOURCES} PROPERTIES LANGUAGE CXX)

target_include_directories(telecrap_libs_sol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Lua is built as C++; consumers that use sol2 must see this when compiling against linked Lua.
target_compile_definitions(telecrap_libs_sol
    INTERFACE
        SOL_USING_CXX_LUA=1
)
