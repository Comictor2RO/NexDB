#include "gtest/gtest.h"
#include "../Frontend/Parser/Parser.hpp"
#include "../Frontend/Lexer/Lexer.hpp"
#include <memory>

// Test 1: Parse CREATE TABLE statement
TEST(ParserTest, ParseCreateTable) {
    Lexer lexer("CREATE TABLE users (id INT, name STRING);");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* createStmt = dynamic_cast<CreateStatement*>(stmt.get());
    EXPECT_NE(createStmt, nullptr);
    EXPECT_EQ(createStmt->getTable(), "users");
    EXPECT_EQ(createStmt->getColumns().size(), 2);
}

// Test 2: Parse SELECT * FROM table
TEST(ParserTest, ParseSelectAll) {
    Lexer lexer("SELECT * FROM users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
    EXPECT_NE(selectStmt, nullptr);
    EXPECT_EQ(selectStmt->getTable(), "users");
    EXPECT_EQ(selectStmt->getColumns().size(), 1);
    EXPECT_EQ(selectStmt->getColumns()[0], "*");
}

// Test 3: Parse SELECT with specific columns
TEST(ParserTest, ParseSelectSpecificColumns) {
    Lexer lexer("SELECT name, age FROM users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
    EXPECT_NE(selectStmt, nullptr);
    EXPECT_EQ(selectStmt->getTable(), "users");
    EXPECT_EQ(selectStmt->getColumns().size(), 2);
    EXPECT_EQ(selectStmt->getColumns()[0], "name");
    EXPECT_EQ(selectStmt->getColumns()[1], "age");
}

// Test 4: Parse SELECT with WHERE clause
TEST(ParserTest, ParseSelectWithWhere) {
    Lexer lexer("SELECT * FROM users WHERE age > 18;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* selectStmt = dynamic_cast<SelectStatement*>(stmt.get());
    EXPECT_NE(selectStmt, nullptr);
    EXPECT_EQ(selectStmt->getTable(), "users");
    EXPECT_NE(selectStmt->getCondition(), nullptr);
    EXPECT_EQ(selectStmt->getCondition()->column, "age");
    EXPECT_EQ(selectStmt->getCondition()->op, ">");
    EXPECT_EQ(selectStmt->getCondition()->value, "18");
}

// Test 5: Parse INSERT statement
TEST(ParserTest, ParseInsertStatement) {
    Lexer lexer("INSERT INTO users VALUES (1, 'John');");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* insertStmt = dynamic_cast<InsertStatement*>(stmt.get());
    EXPECT_NE(insertStmt, nullptr);
    EXPECT_EQ(insertStmt->getTable(), "users");
    EXPECT_EQ(insertStmt->getValues().size(), 2);
}

// Test 6: Parse DELETE statement
TEST(ParserTest, ParseDeleteStatement) {
    Lexer lexer("DELETE FROM users WHERE id = 5;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* deleteStmt = dynamic_cast<DeleteStatement*>(stmt.get());
    EXPECT_NE(deleteStmt, nullptr);
    EXPECT_EQ(deleteStmt->getTable(), "users");
    EXPECT_NE(deleteStmt->getCondition(), nullptr);
    EXPECT_EQ(deleteStmt->getCondition()->column, "id");
    EXPECT_EQ(deleteStmt->getCondition()->op, "=");
    EXPECT_EQ(deleteStmt->getCondition()->value, "5");
}

// Test 7: Parse DROP TABLE statement
TEST(ParserTest, ParseDropTable) {
    Lexer lexer("DROP TABLE users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* dropStmt = dynamic_cast<DropStatement*>(stmt.get());
    EXPECT_NE(dropStmt, nullptr);
    EXPECT_EQ(dropStmt->getTable(), "users");
}

// Test 8: Parse UPDATE statement
TEST(ParserTest, ParseUpdateStatement) {
    Lexer lexer("UPDATE users SET name = 'Jane' WHERE id = 1;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* updateStmt = dynamic_cast<UpdateStatement*>(stmt.get());
    EXPECT_NE(updateStmt, nullptr);
    EXPECT_EQ(updateStmt->getTable(), "users");
    EXPECT_GE(updateStmt->getAssignments().size(), 1);
    EXPECT_NE(updateStmt->getCondition(), nullptr);
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

// Test 11: Parse DROP DATABASE statement
TEST(ParserTest, ParseDropDatabase) {
    Lexer lexer("DROP DATABASE mydb;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* dropDbStmt = dynamic_cast<DropDatabaseStatement*>(stmt.get());
    EXPECT_NE(dropDbStmt, nullptr);
    EXPECT_EQ(dropDbStmt->getName(), "mydb");
}

// Test 12: DROP TABLE still resolves to DropStatement, not DropDatabaseStatement
// (the DROP peek added for DROP DATABASE is what could break this)
TEST(ParserTest, ParseDropTableNotConfusedWithDropDatabase) {
    Lexer lexer("DROP TABLE users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    ASSERT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    EXPECT_NE(dynamic_cast<DropStatement*>(stmt.get()), nullptr);
    EXPECT_EQ(dynamic_cast<DropDatabaseStatement*>(stmt.get()), nullptr);
}

// Test 13: DROP DATABASE without a name is a parse error, not a silent success
TEST(ParserTest, ParseDropDatabaseWithoutNameFails) {
    Lexer lexer("DROP DATABASE;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    EXPECT_FALSE(result.has_value());
}

// Test 14: TEXT and BOOLEAN are accepted in any case and normalized to STRING / BOOL,
// so nothing past the parser ever sees an alias
TEST(ParserTest, ParseCreateTableNormalizesTypeAliases) {
    Lexer lexer("CREATE TABLE t (a TEXT, b boolean, c Text, d STRING, e BOOL);");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    ASSERT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* createStmt = dynamic_cast<CreateStatement*>(stmt.get());
    ASSERT_NE(createStmt, nullptr);

    const auto &cols = createStmt->getColumns();
    ASSERT_EQ(cols.size(), 5u);
    EXPECT_EQ(cols[0].type, "STRING");
    EXPECT_EQ(cols[1].type, "BOOL");
    EXPECT_EQ(cols[2].type, "STRING");
    EXPECT_EQ(cols[3].type, "STRING");
    EXPECT_EQ(cols[4].type, "BOOL");
}

// Test 15: the aliases are not reserved words — "text" and "boolean" still work as
// column names, which is why they are not lexer keywords
TEST(ParserTest, TypeAliasesStayUsableAsColumnNames) {
    Lexer lexer("CREATE TABLE notes (text TEXT, boolean BOOLEAN);");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();

    ASSERT_TRUE(result.has_value());
    auto stmt = std::move(result.value());
    auto* createStmt = dynamic_cast<CreateStatement*>(stmt.get());
    ASSERT_NE(createStmt, nullptr);

    const auto &cols = createStmt->getColumns();
    ASSERT_EQ(cols.size(), 2u);
    EXPECT_EQ(cols[0].name, "text");
    EXPECT_EQ(cols[0].type, "STRING");
    EXPECT_EQ(cols[1].name, "boolean");
    EXPECT_EQ(cols[1].type, "BOOL");
}

// Test 16: an unknown type is still a parse error — the alias path must not
// turn every identifier into a valid type
TEST(ParserTest, ParseCreateTableRejectsUnknownType) {
    for (const char *sql : {"CREATE TABLE t (a VARCHAR);", "CREATE TABLE t (a 123);", "CREATE TABLE t (a 'TEXT');"}) {
        Lexer lexer(sql);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto result = parser.parse();
        ASSERT_FALSE(result.has_value()) << sql;
        EXPECT_EQ(result.error(), ParseError::InvalidType) << sql;
    }
}

// Parses a single statement and returns it, failing the test on a parse error
template <typename T>
static std::unique_ptr<Statement> parseOk(const std::string &sql, T *&out) {
    Lexer lexer(sql);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();
    EXPECT_TRUE(result.has_value()) << sql;
    if (!result.has_value()) { out = nullptr; return nullptr; }
    std::unique_ptr<Statement> stmt = std::move(result.value());
    out = dynamic_cast<T*>(stmt.get());
    return stmt;
}

// Test 17: unquoted NULL is SQL NULL (nullopt), 'NULL' in quotes is the string "NULL"
TEST(ParserTest, InsertNullIsDistinctFromQuotedNull) {
    InsertStatement *ins = nullptr;
    auto stmt = parseOk("INSERT INTO t VALUES (1, NULL, 'NULL', null)", ins);
    ASSERT_NE(ins, nullptr);

    auto values = ins->getValues();
    ASSERT_EQ(values.size(), 4u);
    EXPECT_EQ(values[0], "1");
    EXPECT_FALSE(values[1].has_value());
    ASSERT_TRUE(values[2].has_value());
    EXPECT_EQ(*values[2], "NULL");
    EXPECT_FALSE(values[3].has_value());
}

// Test 18: the empty string is a valid value. The old parser rejected it because it
// checked the token text for emptiness instead of the token type
TEST(ParserTest, InsertAcceptsEmptyString) {
    InsertStatement *ins = nullptr;
    auto stmt = parseOk("INSERT INTO t VALUES (1, '')", ins);
    ASSERT_NE(ins, nullptr);

    auto values = ins->getValues();
    ASSERT_EQ(values.size(), 2u);
    ASSERT_TRUE(values[1].has_value());
    EXPECT_EQ(*values[1], "");
}

// Test 19: WHERE col IS NULL / IS NOT NULL become their own operators with no value
TEST(ParserTest, ParseIsNullAndIsNotNull) {
    SelectStatement *sel = nullptr;
    auto s1 = parseOk("SELECT * FROM t WHERE name IS NULL", sel);
    ASSERT_NE(sel, nullptr);
    ASSERT_NE(sel->getCondition(), nullptr);
    EXPECT_EQ(sel->getCondition()->column, "name");
    EXPECT_EQ(sel->getCondition()->op, "IS NULL");
    EXPECT_FALSE(sel->getCondition()->value.has_value());

    DeleteStatement *del = nullptr;
    auto s2 = parseOk("DELETE FROM t WHERE name is not null", del);
    ASSERT_NE(del, nullptr);
    ASSERT_NE(del->getCondition(), nullptr);
    EXPECT_EQ(del->getCondition()->op, "IS NOT NULL");
}

// Test 20: IS must be followed by [NOT] NULL, anything else is a parse error
TEST(ParserTest, IsWithoutNullIsRejected) {
    for (const char *sql : {"SELECT * FROM t WHERE a IS 5", "SELECT * FROM t WHERE a IS NOT 'x'",
                            "SELECT * FROM t WHERE a IS", "SELECT * FROM t WHERE a IS NULL NULL"}) {
        Lexer lexer(sql);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        EXPECT_FALSE(parser.parse().has_value()) << sql;
    }
}

// Test 21: col = NULL parses (it just matches nothing), and UPDATE ... SET col = NULL
// carries a real NULL
TEST(ParserTest, EqualsNullAndSetNullParse) {
    SelectStatement *sel = nullptr;
    auto s1 = parseOk("SELECT * FROM t WHERE a = NULL", sel);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->getCondition()->op, "=");
    EXPECT_FALSE(sel->getCondition()->value.has_value());

    UpdateStatement *upd = nullptr;
    auto s2 = parseOk("UPDATE t SET a = NULL, b = 'NULL'", upd);
    ASSERT_NE(upd, nullptr);
    const auto &assignments = upd->getAssignments();
    ASSERT_EQ(assignments.size(), 2u);
    EXPECT_FALSE(assignments[0].second.has_value());
    EXPECT_EQ(assignments[1].second, "NULL");
}

// Test 22: a column name may start with '_'; the lexer used to drop the '_' and the
// column was stored as x
TEST(ParserTest, ColumnNameMayStartWithUnderscore) {
    CreateStatement *create = nullptr;
    auto stmt = parseOk("CREATE TABLE t (_x INT)", create);
    ASSERT_NE(create, nullptr);

    const auto &cols = create->getColumns();
    ASSERT_EQ(cols.size(), 1u);
    EXPECT_EQ(cols[0].name, "_x");
    EXPECT_EQ(cols[0].type, "INT");
}
