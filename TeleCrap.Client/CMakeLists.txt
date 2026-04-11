add_executable(telecrap_client
    src/MemoryCache.cpp
    src/TeleCrap.Client.cpp
    src/TerminalUI.cpp
    "include/ini.h")

if(TELECRAP_TUI_USE_CURSES)
    target_sources(telecrap_client PRIVATE src/TerminalUI.Curses.cpp)
    target_compile_definitions(telecrap_client PRIVATE TELECRAP_TUI_CURSES=1)

    if(WIN32)
        if(TELECRAP_USE_BUNDLED_PDCURSES)
            set(TELECRAP_PDCURSES_CORE
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/addch.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/addchstr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/addstr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/attr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/beep.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/bkgd.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/border.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/clear.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/color.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/debug.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/delch.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/deleteln.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/getch.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/getstr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/getyx.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/inch.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/inchstr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/initscr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/inopts.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/insch.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/insstr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/instr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/kernel.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/keyname.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/mouse.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/move.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/outopts.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/overlay.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/pad.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/panel.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/printw.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/refresh.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/scanw.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/scr_dump.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/scroll.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/slk.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/termattr.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/terminfo.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/touch.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/util.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/pdcurses/window.c
            )

            set(TELECRAP_PDCURSES_WINCON
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon/pdcclip.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon/pdcdisp.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon/pdcgetsc.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon/pdckbd.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon/pdcscrn.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon/pdcsetsc.c
                ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon/pdcutil.c
            )

            add_library(telecrap_pdcurses STATIC
                ${TELECRAP_PDCURSES_CORE}
                ${TELECRAP_PDCURSES_WINCON}
             "include/ini.h")

            target_include_directories(telecrap_pdcurses
                PUBLIC
                    ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9
                    ${CMAKE_SOURCE_DIR}/third-party/PDCurses-3.9/wincon
            )

            target_compile_definitions(telecrap_pdcurses
                PUBLIC
                    PDC_WIDE
                    PDC_FORCE_UTF8
            )

            if(MSVC)
                target_compile_options(telecrap_pdcurses PRIVATE /wd4005)
            endif()

            target_link_libraries(telecrap_pdcurses PRIVATE winmm)
        endif()
    else()
        find_package(Curses REQUIRED)
    endif()
else()
    if(WIN32)
        target_sources(telecrap_client PRIVATE src/TerminalUI.Win.cpp)
    else()
        target_sources(telecrap_client PRIVATE src/TerminalUI.Linux.cpp)
    endif()
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

if(TELECRAP_TUI_USE_CURSES)
    if(WIN32)
        if(MSVC)
            target_compile_options(telecrap_client PRIVATE /wd4005)
        endif()
        if(TELECRAP_USE_BUNDLED_PDCURSES)
            target_link_libraries(telecrap_client PRIVATE telecrap_pdcurses)
        endif()
    else()
        target_include_directories(telecrap_client PRIVATE ${CURSES_INCLUDE_DIRS})
        target_link_libraries(telecrap_client PRIVATE ${CURSES_LIBRARIES})
    endif()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(telecrap_client PRIVATE pthread dl)
endif()

set_target_properties(telecrap_client PROPERTIES OUTPUT_NAME "telecrap_tui")

add_custom_command(TARGET telecrap_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/settings.cfg
        $<TARGET_FILE_DIR:telecrap_client>/settings.cfg
)
