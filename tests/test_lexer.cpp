#include <gtest/gtest.h>
#include "../Frontend/Lexer/Lexer.hpp"

// Test 1: SELECT query basic
TEST(LexerTest, BasicSelectQuery) {
    Lexer lexer("SELECT * FROM users;");
    auto tokens = lexer.tokenize();
    EXPECT_GE(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type, TokenType::KEYWORD);
    EXPECT_EQ(tokens[0].value, "SELECT");
}

// Test 2: Keywords tokenization
TEST(LexerTest, KeywordsTokenization) {
    Lexer lexer("INSERT INTO table VALUES");
    auto tokens = lexer.tokenize();
    EXPECT_EQ(tokens[0].type, TokenType::KEYWORD);
    EXPECT_EQ(tokens[0].value, "INSERT");
    EXPECT_EQ(tokens[1].type, TokenType::KEYWORD);
    EXPECT_EQ(tokens[1].value, "INTO");
}

// Test 3: String literals
TEST(LexerTest, StringLiterals) {
    Lexer lexer("'hello world'");
    auto tokens = lexer.tokenize();
    EXPECT_EQ(tokens[0].type, TokenType::STRING);
    EXPECT_EQ(tokens[0].value, "hello world");
}

// Test 4: Number tokenization
TEST(LexerTest, NumberTokenization) {
    Lexer lexer("123 456");
    auto tokens = lexer.tokenize();
    EXPECT_EQ(tokens[0].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[0].value, "123");
    EXPECT_EQ(tokens[1].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[1].value, "456");
}

// Test 5: Operators
TEST(LexerTest, OperatorTokenization) {
    Lexer lexer("= > < >= <= !=");
    auto tokens = lexer.tokenize();
    for (const auto& token : tokens) {
        if (token.type != TokenType::END_OF_FILE) {
            EXPECT_EQ(token.type, TokenType::OPERATOR);
        }
    }
}

// Test 6: Punctuation
TEST(LexerTest, PunctuationTokenization) {
    Lexer lexer("( ) ,");
    auto tokens = lexer.tokenize();
    int punctuationCount = 0;
    for (const auto& token : tokens) {
        if (token.type == TokenType::PUNCTUATION) {
            punctuationCount++;
        }
    }
    EXPECT_EQ(punctuationCount, 3);
}

// Test 7: Wildcard
TEST(LexerTest, WildcardTokenization) {
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.tokenize();
    bool foundWildcard = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::WILDCARD && token.value == "*") {
            foundWildcard = true;
            break;
        }
    }
    EXPECT_TRUE(foundWildcard);
}

// Test 8: Identifiers
TEST(LexerTest, IdentifierTokenization) {
    Lexer lexer("user_name table123 Column_A");
    auto tokens = lexer.tokenize();
    EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[0].value, "user_name");
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);
}

// Test 9: Complex query
TEST(LexerTest, ComplexQuery) {
    Lexer lexer("SELECT name, age FROM users WHERE age > 18;");
    auto tokens = lexer.tokenize();
    EXPECT_GE(tokens.size(), 10);
    EXPECT_EQ(tokens[0].type, TokenType::KEYWORD);
    EXPECT_EQ(tokens[0].value, "SELECT");
}

// Test 10: Empty input
TEST(LexerTest, EmptyInput) {
    Lexer lexer("");
    auto tokens = lexer.tokenize();
    EXPECT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::END_OF_FILE);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}