cmake_minimum_required(VERSION 3.20)
project(tests)

# GTest auto-compile pentru MinGW
include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)  # Doar GTest
FetchContent_MakeAvailable(googletest)

add_executable(tests test_lexer.cpp
        ../Frontend/Lexer/Lexer.cpp
        ../Frontend/Parser/Parser.cpp
        ../AST/DeleteStatement/DeleteStatement.cpp
        ../AST/CreateStatement/CreateStatement.cpp
        ../AST/SelectStatement/SelectStatement.cpp
        ../AST/UpdateStatement/UpdateStatement.cpp
        ../AST/InsertStatement/InsertStatement.cpp
        ../AST/DropStatement/DropStatement.cpp
        test_parser.cpp)
target_include_directories(tests PRIVATE ../Frontend/Lexer)
target_link_libraries(tests gtest gtest_main pthread)  # Simplu!

include(GoogleTest)
gtest_discover_tests(tests)