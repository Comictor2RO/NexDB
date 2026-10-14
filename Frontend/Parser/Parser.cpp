#include "Parser.hpp"
#include <memory>
#include <cctype>

// Maps a column type token to the name stored in the catalog, or "" if it is not a type.
// TEXT and BOOLEAN are aliases: normalizing them here means the catalog, and every type
// comparison downstream, only ever sees INT, STRING, FLOAT and BOOL.
// The aliases are deliberately not lexer keywords, which would reserve the words and
// break columns named "text" or "boolean"; they arrive as identifiers instead.
static std::string canonicalColumnType(const Token &token)
{
    if (token.type != TokenType::KEYWORD && token.type != TokenType::IDENTIFIER)
        return "";

    std::string upper = token.value;
    for (char &c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (token.type == TokenType::KEYWORD)
        return (upper == "INT" || upper == "STRING" || upper == "FLOAT" || upper == "BOOL") ? upper : "";

    if (upper == "TEXT")    return "STRING";
    if (upper == "BOOLEAN") return "BOOL";
    return "";
}

Parser::Parser(const std::vector<Token> &tokens)
    : tokens(tokens), position(0)
{}

Token Parser::currentToken()
{
    if (position >= (int)tokens.size())
        return {TokenType::END_OF_FILE, ""};
    return tokens[position];
}

Token Parser::consumeToken()
{
    Token t = currentToken();
    if (position < (int)tokens.size())
        position++;
    return t;
}

std::string Parser::expectToken(TokenType type)
{
    std::string value;
    if (currentToken().type == type)
    {
        value = currentToken().value;
        consumeToken();
        return value;
    }
    return "";
}

bool Parser::expectToken(TokenType type, const std::string &value)
{
    if (currentToken().type == type && currentToken().value == value)
    {
        consumeToken();
        return true;
    }
    else
        return false;
}

std::expected<std::unique_ptr<CreateStatement>, ParseError> Parser::parseCreate() {
    if (!expectToken(TokenType::KEYWORD, "CREATE"))
        return std::unexpected(ParseError::MissingKeyword);

    if (!expectToken(TokenType::KEYWORD, "TABLE"))
        return std::unexpected(ParseError::MissingKeyword);

    std::string table = expectToken(TokenType::IDENTIFIER);
    if (table.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (!expectToken(TokenType::PUNCTUATION, "("))
        return std::unexpected(ParseError::MissingPunctuation);

    std::vector<Columns> columns;
    while (true) {
        if (currentToken().type == TokenType::END_OF_FILE)
            return std::unexpected(ParseError::UnexpectedToken);

        if (currentToken().value == ")") break;

        Columns col;
        col.name = expectToken(TokenType::IDENTIFIER);
        if (col.name.empty())
            return std::unexpected(ParseError::InvalidIdentifier);

        col.type = canonicalColumnType(currentToken());
        if (col.type.empty())
            return std::unexpected(ParseError::InvalidType);
        consumeToken();

        columns.push_back(col);

        if (currentToken().value == ",") {
            consumeToken();
            if (currentToken().value == ")")
                return std::unexpected(ParseError::TrailingComma);
            continue;
        }

        if (currentToken().value != ")")
            return std::unexpected(ParseError::UnexpectedToken);
    }

    if (columns.empty())
        return std::unexpected(ParseError::EmptyColumns);

    consumeToken();  // consume ')'

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<CreateStatement>(table, columns);
}

std::expected<std::unique_ptr<SelectStatement>, ParseError> Parser::parseSelect() {
    std::vector<std::string> columns;
    std::string table;

    if (!expectToken(TokenType::KEYWORD, "SELECT"))
        return std::unexpected(ParseError::MissingKeyword);

    if (expectToken(TokenType::WILDCARD, "*"))
        columns.push_back("*");
    else
    {
        while (true)
        {
            std::string column = expectToken(TokenType::IDENTIFIER);
            if (column == "")
                return std::unexpected(ParseError::InvalidIdentifier);
            columns.push_back(column);
            if (currentToken().value == ",")
            {
                consumeToken();
                if (currentToken().type != TokenType::IDENTIFIER)
                    return std::unexpected(ParseError::UnexpectedToken);
            }
            else
                break;
        }
    }
    
    if (!expectToken(TokenType::KEYWORD, "FROM"))
        return std::unexpected(ParseError::MissingKeyword);

    table = expectToken(TokenType::IDENTIFIER);
    if (table.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    Condition *condition = nullptr;
    if (expectToken(TokenType::KEYWORD, "WHERE")) {
        auto condRes = parseCondition();
        if (!condRes)
            return std::unexpected(condRes.error());
        condition = condRes.value();
    }

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<SelectStatement>(columns, table, condition);
}

std::expected<std::unique_ptr<InsertStatement>, ParseError> Parser::parseInsert()
{
    std::string table;
    std::vector<Value> values;
    std::vector<std::string> columns;

    if (!expectToken(TokenType::KEYWORD, "INSERT"))
        return std::unexpected(ParseError::MissingKeyword);

    if (!expectToken(TokenType::KEYWORD, "INTO"))
        return std::unexpected(ParseError::MissingKeyword);

    table = expectToken(TokenType::IDENTIFIER);
    if (table.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (expectToken(TokenType::PUNCTUATION, "("))
    {
        while (true) {
            std::string col = expectToken(TokenType::IDENTIFIER);
            if (col.empty())
                return std::unexpected(ParseError::InvalidIdentifier);
            columns.push_back(col);
            if (currentToken().value == ",")
            {
                consumeToken();
                if (currentToken().type != TokenType::IDENTIFIER)
                    return std::unexpected(ParseError::UnexpectedToken);
            }
            else if (currentToken().value == ")")
                break;
            else
                return std::unexpected(ParseError::UnexpectedToken);
        }
        if (columns.empty())
            return std::unexpected(ParseError::EmptyColumns);

        if (!expectToken(TokenType::PUNCTUATION, ")"))
            return std::unexpected(ParseError::MissingPunctuation);
    }

    if (!expectToken(TokenType::KEYWORD, "VALUES"))
        return std::unexpected(ParseError::MissingKeyword);

    if (!expectToken(TokenType::PUNCTUATION, "("))
        return std::unexpected(ParseError::MissingPunctuation);

    while (true)
    {
        auto val = parseValue();
        if (!val)
            return std::unexpected(val.error());

        values.push_back(std::move(val.value()));

        if (currentToken().value == ")")
            break;
        else if (currentToken().value == ",")
            consumeToken();
        else
            return std::unexpected(ParseError::UnexpectedToken);
    }
    
    if (values.empty())
        return std::unexpected(ParseError::EmptyColumns);

    if (!expectToken(TokenType::PUNCTUATION, ")"))
        return std::unexpected(ParseError::MissingPunctuation);

    if (!columns.empty() && values.size() != columns.size())
        return std::unexpected(ParseError::ValuesMismatch);

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<InsertStatement>(table, columns, values);
}

std::expected<std::unique_ptr<DeleteStatement>, ParseError> Parser::parseDelete()
{
    if (!expectToken(TokenType::KEYWORD, "DELETE"))
        return std::unexpected(ParseError::MissingKeyword);

    if (!expectToken(TokenType::KEYWORD, "FROM"))
        return std::unexpected(ParseError::MissingKeyword);

    std::string table = expectToken(TokenType::IDENTIFIER);
    if (table.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    Condition *condition = nullptr;
    if (expectToken(TokenType::KEYWORD, "WHERE")) {
        auto condRes = parseCondition();
        if (!condRes)
            return std::unexpected(condRes.error());
        condition = condRes.value();
    }

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<DeleteStatement>(table, condition);
}

std::expected<std::unique_ptr<DropStatement>, ParseError> Parser::parseDrop()
{
    if (!expectToken(TokenType::KEYWORD, "DROP"))
        return std::unexpected(ParseError::MissingKeyword);

    if (!expectToken(TokenType::KEYWORD, "TABLE"))
        return std::unexpected(ParseError::MissingKeyword);

    std::string table = expectToken(TokenType::IDENTIFIER);
    if (table.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<DropStatement>(table);
}

std::expected<std::unique_ptr<UpdateStatement>, ParseError> Parser::parseUpdate()
{
    if (!expectToken(TokenType::KEYWORD, "UPDATE"))
        return std::unexpected(ParseError::MissingKeyword);

    std::string table = expectToken(TokenType::IDENTIFIER);
    if (table.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (!expectToken(TokenType::KEYWORD, "SET"))
        return std::unexpected(ParseError::MissingKeyword);

    auto stmt = std::make_unique<UpdateStatement>(table);
    while (true)
    {
        std::string column = expectToken(TokenType::IDENTIFIER);
        if (column.empty())
            return std::unexpected(ParseError::InvalidIdentifier);

        if (!expectToken(TokenType::OPERATOR, "="))
            return std::unexpected(ParseError::UnexpectedToken);

        auto value = parseValue();
        if (!value)
            return std::unexpected(value.error());

        stmt->addAssignements(column, value.value());

        if (expectToken(TokenType::PUNCTUATION, ","))
            continue;

        break;
    }

    if (expectToken(TokenType::KEYWORD, "WHERE"))
    {
        auto condRes = parseCondition();
        if (!condRes)
            return std::unexpected(condRes.error());
        stmt->setCondition(condRes.value());
    }

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return stmt;
}

std::expected<Condition*, ParseError> Parser::parseCondition()
{
    std::string column = expectToken(TokenType::IDENTIFIER);
    if (column.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    // col IS NULL / col IS NOT NULL
    if (expectToken(TokenType::KEYWORD, "IS"))
    {
        bool negated = expectToken(TokenType::KEYWORD, "NOT");
        if (!expectToken(TokenType::KEYWORD, "NULL"))
            return std::unexpected(ParseError::UnexpectedToken);

        Condition *condition = new Condition();
        condition->column = column;
        condition->op = negated ? "IS NOT NULL" : "IS NULL";
        condition->value = std::nullopt;
        return condition;
    }

    std::string op = expectToken(TokenType::OPERATOR);
    if (op.empty())
        return std::unexpected(ParseError::UnexpectedToken);

    // col = NULL is accepted, but by SQL rules it matches no row (see evaluateCondition)
    auto value = parseValue();
    if (!value)
        return std::unexpected(value.error());

    Condition *condition = new Condition();
    condition->column = column;
    condition->op = op;
    condition->value = value.value();
    return condition;
}

// A single literal value: NULL (the keyword, unquoted) becomes std::nullopt, while
// 'NULL' in quotes stays the string "NULL". Checks the token type rather than the
// text, so the empty string '' is a valid value.
std::expected<Value, ParseError> Parser::parseValue()
{
    Token token = currentToken();

    if (token.type == TokenType::KEYWORD && token.value == "NULL")
    {
        consumeToken();
        return Value(std::nullopt);
    }

    if (token.type == TokenType::STRING || token.type == TokenType::NUMBER
        || token.type == TokenType::IDENTIFIER)
    {
        consumeToken();
        return token.value;
    }

    return std::unexpected(ParseError::UnexpectedToken);
}

std::expected<std::unique_ptr<CreateDatabaseStatement>, ParseError> Parser::parseCreateDatabase()
{
    if (!expectToken(TokenType::KEYWORD, "CREATE"))
        return std::unexpected(ParseError::MissingKeyword);

    if (!expectToken(TokenType::KEYWORD, "DATABASE"))
        return std::unexpected(ParseError::MissingKeyword);

    std::string name = expectToken(TokenType::IDENTIFIER);
    if (name.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<CreateDatabaseStatement>(name);
}

std::expected<std::unique_ptr<DropDatabaseStatement>, ParseError> Parser::parseDropDatabase()
{
    if (!expectToken(TokenType::KEYWORD, "DROP"))
        return std::unexpected(ParseError::MissingKeyword);

    if (!expectToken(TokenType::KEYWORD, "DATABASE"))
        return std::unexpected(ParseError::MissingKeyword);

    std::string name = expectToken(TokenType::IDENTIFIER);
    if (name.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<DropDatabaseStatement>(name);
}

std::expected<std::unique_ptr<UseDatabaseStatement>, ParseError> Parser::parseUseDatabase()
{
    if (!expectToken(TokenType::KEYWORD, "USE"))
        return std::unexpected(ParseError::MissingKeyword);

    // Support both: USE mydb  and  USE DATABASE mydb
    expectToken(TokenType::KEYWORD, "DATABASE");

    std::string name = expectToken(TokenType::IDENTIFIER);
    if (name.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (currentToken().type != TokenType::END_OF_FILE)
        return std::unexpected(ParseError::ExtraTokens);

    return std::make_unique<UseDatabaseStatement>(name);
}

std::expected<std::unique_ptr<Statement>, ParseError> Parser::parse()
{
    Token token = currentToken();

    if (token.type != TokenType::KEYWORD)
        return std::unexpected(ParseError::MissingKeyword);

    if (token.value == "SELECT")
        return parseSelect();
    if (token.value == "INSERT")
        return parseInsert();
    if (token.value == "DELETE")
        return parseDelete();
    if (token.value == "CREATE") {
        // Peek at next token to distinguish CREATE TABLE vs CREATE DATABASE
        if (position + 1 < (int)tokens.size() && tokens[position + 1].value == "DATABASE")
            return parseCreateDatabase();
        return parseCreate();
    }
    if (token.value == "DROP") {
        if (position + 1 < (int)tokens.size() && tokens[position + 1].value == "DATABASE")
            return parseDropDatabase();
        return parseDrop();
    }
    if (token.value == "UPDATE")
        return parseUpdate();
    if (token.value == "USE")
        return parseUseDatabase();

    return std::unexpected(ParseError::UnexpectedToken);
}