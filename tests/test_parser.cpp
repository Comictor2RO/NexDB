#include "gtest/gtest.h"
#include "../Frontend/Parser/Parser.hpp"
#include "../Frontend/Lexer/Lexer.hpp"

// Test 1: Parse CREATE TABLE statement
TEST(ParserTest, ParseCreateTable) {
    Lexer lexer("CREATE TABLE users (id INT, name STRING);");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* createStmt = dynamic_cast<CreateStatement*>(stmt);
    EXPECT_NE(createStmt, nullptr);
    EXPECT_EQ(createStmt->getTable(), "users");
    EXPECT_EQ(createStmt->getColumns().size(), 2);
    delete stmt;
}

// Test 2: Parse SELECT * FROM table
TEST(ParserTest, ParseSelectAll) {
    Lexer lexer("SELECT * FROM users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* selectStmt = dynamic_cast<SelectStatement*>(stmt);
    EXPECT_NE(selectStmt, nullptr);
    EXPECT_EQ(selectStmt->getTable(), "users");
    EXPECT_EQ(selectStmt->getColumns().size(), 1);
    EXPECT_EQ(selectStmt->getColumns()[0], "*");
    delete stmt;
}

// Test 3: Parse SELECT with specific columns
TEST(ParserTest, ParseSelectSpecificColumns) {
    Lexer lexer("SELECT name, age FROM users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* selectStmt = dynamic_cast<SelectStatement*>(stmt);
    EXPECT_NE(selectStmt, nullptr);
    EXPECT_EQ(selectStmt->getTable(), "users");
    EXPECT_EQ(selectStmt->getColumns().size(), 2);
    EXPECT_EQ(selectStmt->getColumns()[0], "name");
    EXPECT_EQ(selectStmt->getColumns()[1], "age");
    delete stmt;
}

// Test 4: Parse SELECT with WHERE clause
TEST(ParserTest, ParseSelectWithWhere) {
    Lexer lexer("SELECT * FROM users WHERE age > 18;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* selectStmt = dynamic_cast<SelectStatement*>(stmt);
    EXPECT_NE(selectStmt, nullptr);
    EXPECT_EQ(selectStmt->getTable(), "users");
    EXPECT_NE(selectStmt->getCondition(), nullptr);
    EXPECT_EQ(selectStmt->getCondition()->column, "age");
    EXPECT_EQ(selectStmt->getCondition()->op, ">");
    EXPECT_EQ(selectStmt->getCondition()->value, "18");
    delete stmt;
}

// Test 5: Parse INSERT statement
TEST(ParserTest, ParseInsertStatement) {
    Lexer lexer("INSERT INTO users VALUES (1, 'John');");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* insertStmt = dynamic_cast<InsertStatement*>(stmt);
    EXPECT_NE(insertStmt, nullptr);
    EXPECT_EQ(insertStmt->getTable(), "users");
    EXPECT_EQ(insertStmt->getValues().size(), 2);
    delete stmt;
}

// Test 6: Parse DELETE statement
TEST(ParserTest, ParseDeleteStatement) {
    Lexer lexer("DELETE FROM users WHERE id = 5;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* deleteStmt = dynamic_cast<DeleteStatement*>(stmt);
    EXPECT_NE(deleteStmt, nullptr);
    EXPECT_EQ(deleteStmt->getTable(), "users");
    EXPECT_NE(deleteStmt->getCondition(), nullptr);
    EXPECT_EQ(deleteStmt->getCondition()->column, "id");
    EXPECT_EQ(deleteStmt->getCondition()->op, "=");
    EXPECT_EQ(deleteStmt->getCondition()->value, "5");
    delete stmt;
}

// Test 7: Parse DROP TABLE statement
TEST(ParserTest, ParseDropTable) {
    Lexer lexer("DROP TABLE users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* dropStmt = dynamic_cast<DropStatement*>(stmt);
    EXPECT_NE(dropStmt, nullptr);
    EXPECT_EQ(dropStmt->getTable(), "users");
    delete stmt;
}

// Test 8: Parse UPDATE statement
TEST(ParserTest, ParseUpdateStatement) {
    Lexer lexer("UPDATE users SET name = 'Jane' WHERE id = 1;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto* stmt = result.value();
    auto* updateStmt = dynamic_cast<UpdateStatement*>(stmt);
    EXPECT_NE(updateStmt, nullptr);
    EXPECT_EQ(updateStmt->getTable(), "users");
    EXPECT_GE(updateStmt->getAssignments().size(), 1);
    EXPECT_NE(updateStmt->getCondition(), nullptr);
    delete stmt;
}

// Test 9: Parse invalid syntax
TEST(ParserTest, ParseInvalidSyntax) {
    Lexer lexer("SELECT FROM WHERE;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_FALSE(result.has_value());
}

// Test 10: Parse empty tokens
TEST(ParserTest, ParseEmptyTokens) {
    std::vector<Token> emptyTokens;
    emptyTokens.push_back({TokenType::END_OF_FILE, ""});
    Parser parser(emptyTokens);
    auto result = parser.parse();

    EXPECT_FALSE(result.has_value());
}