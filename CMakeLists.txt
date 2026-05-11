cmake_minimum_required(VERSION 3.20)
project(VDT_Engine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# GTest pentru teste (auto-compile MinGW)
include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

# Raylib
FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# Asio (header-only, standalone)
FetchContent_Declare(
        asio
        URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-36-0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(asio)
add_compile_definitions(ASIO_STANDALONE)

# Teste SEPARATE
add_subdirectory(tests)
# Include directories
include_directories(${asio_SOURCE_DIR}/asio/include)
include_directories(VDT_Engine PRIVATE
        AST/Condition
        AST/DeleteStatement
        AST/InsertStatement
        frontend/Lexer
        AST/SelectStatement
        AST/Statement
        AST/CreateStatement
        AST/Columns
        Catalog
        Table
        AST/Row
        StringUtils
        Engine
        Indexing/IndexRecord
        Indexing/BPlusNode
        Indexing/BPlusTree
        Storage/LRUCache
        WALManager
        GUI
        AST/DropStatement
        AST/UpdateStatement
)

# MAIN APP - FĂRĂ teste!
add_executable(VDT_Engine
        AST/Condition/Condition.hpp
        AST/DeleteStatement/DeleteStatement.cpp
        AST/DeleteStatement/DeleteStatement.hpp
        AST/InsertStatement/InsertStatement.cpp
        AST/InsertStatement/InsertStatement.hpp
        frontend/Lexer/Lexer.cpp
        frontend/Lexer/Lexer.hpp
        AST/SelectStatement/SelectStatement.cpp
        AST/SelectStatement/SelectStatement.hpp
        AST/Statement/Statement.hpp
        main.cpp
        frontend/Parser/Parser.cpp
        frontend/Parser/Parser.hpp
        Storage/Page/Page.cpp
        Storage/Page/Page.hpp
        Storage/PageHeader/PageHeader.hpp
        Storage/PageManager/PageManager.cpp
        Storage/PageManager/PageManager.hpp
        Frontend/Lexer/Token.hpp
        Frontend/Lexer/TokenType.hpp
        AST/CreateStatement/CreateStatement.cpp
        AST/CreateStatement/CreateStatement.hpp
        AST/Columns/Columns.hpp
        Catalog/Catalog.cpp
        Catalog/Catalog.hpp
        Table/Table.cpp
        Table/Table.hpp
        AST/Row/Row.hpp
        StringUtils/StringUtils.hpp
        Engine/Engine.cpp
        Engine/Engine.hpp
        Indexing/IndexRecord/IndexRecord.hpp
        Indexing/BPlusNode/BPlusNode.hpp
        Indexing/BPlusTree/BPlusTree.cpp
        Indexing/BPlusTree/BPlusTree.hpp
        Storage/LRUCache/LRUCache.hpp
        Storage/LRUCache/LRUCache.cpp
        WALManager/WALManager.hpp
        WALManager/WALManager.cpp
        GUI/GUI.hpp
        GUI/GUI.cpp
        AST/DropStatement/DropStatement.cpp
        AST/DropStatement/DropStatement.hpp
        AST/UpdateStatement/UpdateStatement.cpp
        AST/UpdateStatement/UpdateStatement.hpp
        GUI/Colors.hpp
        Networking/NetworkServer.cpp
        Networking/NetworkServer.hpp
)

# Link main app
target_link_libraries(VDT_Engine PRIVATE raylib opengl32 gdi32 winmm ws2_32 wsock32)
target_link_options(VDT_Engine PRIVATE -static-libgcc -static-libstdc++ -Wl,--allow-multiple-definition)

# Copiază resources lângă executabil
add_custom_command(TARGET VDT_Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:VDT_Engine>/resources
)