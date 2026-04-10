set(TELECRAP_LUA_SOURCES
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    lcorolib.c
    lctype.c
    ldblib.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    
    lundump.c
    lutf8lib.c
    lvm.c
    lzio.c
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
