add_library(telecrap_libs_sqlite STATIC
    src/sqlite3.c
    src/Backup.cpp
    src/Column.cpp
    src/Database.cpp
    src/Exception.cpp
    src/Savepoint.cpp
    src/Statement.cpp
    src/Transaction.cpp
)

set_source_files_properties(src/sqlite3.c PROPERTIES LANGUAGE C)

target_include_directories(telecrap_libs_sqlite
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
