cmake_minimum_required(VERSION 3.16)
project(Mini_Database_Engine)

set(CMAKE_CXX_STANDARD 17)

# RayLib instalat local
set(RAYLIB_DIR "C:/raylib/raylib/src")
include_directories(${RAYLIB_DIR})
link_directories(${RAYLIB_DIR})

include_directories(AST/Condition)
include_directories(AST/DeleteStatement)
include_directories(AST/InsertStatement)
include_directories(frontend/Lexer)
include_directories(AST/SelectStatement)
include_directories(AST/Statement)

add_executable(Mini_Database_Engine
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
)

target_link_libraries(Mini_Database_Engine PRIVATE raylib opengl32 gdi32 winmm)
