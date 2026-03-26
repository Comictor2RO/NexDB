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

# Teste SEPARATE
add_subdirectory(tests)

# Raylib
set(RAYLIB_DIR "C:/raylib/raylib/src")
include_directories(${RAYLIB_DIR})
link_directories(${RAYLIB_DIR})

# Include directories
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
)

# Link main app
target_link_libraries(VDT_Engine PRIVATE raylib opengl32 gdi32 winmm)
target_link_options(VDT_Engine PRIVATE -static-libgcc -static-libstdc++)