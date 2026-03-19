#include "Parser.hpp"

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

std::expected<CreateStatement*, ParseError> Parser::parseCreate() {
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

        col.type = expectToken(TokenType::KEYWORD);
        if (col.type != "INT" && col.type != "STRING")
            return std::unexpected(ParseError::InvalidType);

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

    return new CreateStatement(table, columns);
}

std::expected<SelectStatement*, ParseError> Parser::parseSelect() {
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

    return new SelectStatement(columns, table, condition);
}

std::expected<InsertStatement*, ParseError> Parser::parseInsert()
{
    std::string table;
    std::vector<std::string> values;
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
        std::string val;
        if (currentToken().type == TokenType::NUMBER)
            val = expectToken(TokenType::NUMBER);
        else if (currentToken().type == TokenType::STRING)
            val = expectToken(TokenType::STRING);
        else if (currentToken().type == TokenType::IDENTIFIER)
            val = expectToken(TokenType::IDENTIFIER);
        
        if (val.empty())
            return std::unexpected(ParseError::UnexpectedToken);

        values.push_back(val);

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

    return new InsertStatement(table, columns, values);
}

std::expected<DeleteStatement*, ParseError> Parser::parseDelete()
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

    return new DeleteStatement(table, condition);
}

std::expected<DropStatement*, ParseError> Parser::parseDrop()
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

    return new DropStatement(table);
}

std::expected<UpdateStatement*, ParseError> Parser::parseUpdate()
{
    if (!expectToken(TokenType::KEYWORD, "UPDATE"))
        return std::unexpected(ParseError::MissingKeyword);

    std::string table = expectToken(TokenType::IDENTIFIER);
    if (table.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    if (!expectToken(TokenType::KEYWORD, "SET"))
        return std::unexpected(ParseError::MissingKeyword);

    auto *stmt = new UpdateStatement(table);
    while (true)
    {
        std::string column = expectToken(TokenType::IDENTIFIER);
        if (column.empty())
        {
            delete stmt;
            return std::unexpected(ParseError::InvalidIdentifier);
        }

        if (!expectToken(TokenType::OPERATOR, "="))
        {
            delete stmt;
            return std::unexpected(ParseError::UnexpectedToken);
        }

        std::string value = expectToken(TokenType::STRING);
        if (value.empty())
            value = expectToken(TokenType::NUMBER);
        if (value.empty())
            value = expectToken(TokenType::IDENTIFIER);
        if (value.empty())
        {
            delete stmt;
            return std::unexpected(ParseError::UnexpectedToken);
        }

        stmt->addAssignements(column, value);

        if (expectToken(TokenType::PUNCTUATION, ","))
            continue;

        break;
    }

    if (expectToken(TokenType::KEYWORD, "WHERE"))
    {
        auto condRes = parseCondition();
        if (!condRes)
        {
            delete stmt;
            return std::unexpected(condRes.error());
        }
        stmt->setCondition(condRes.value());
    }

    if (currentToken().type != TokenType::END_OF_FILE)
    {
        delete stmt;
        return std::unexpected(ParseError::ExtraTokens);
    }

    return stmt;
}

std::expected<Condition*, ParseError> Parser::parseCondition()
{
    std::string column = expectToken(TokenType::IDENTIFIER);
    if (column.empty())
        return std::unexpected(ParseError::InvalidIdentifier);

    std::string op = expectToken(TokenType::OPERATOR);
    if (op.empty())
        return std::unexpected(ParseError::UnexpectedToken);

    std::string value;
    if (currentToken().type == TokenType::STRING)
        value = expectToken(TokenType::STRING);
    else if (currentToken().type == TokenType::NUMBER)
        value = expectToken(TokenType::NUMBER);
    else if (currentToken().type == TokenType::IDENTIFIER)
        value = expectToken(TokenType::IDENTIFIER);
    
    if (value.empty())
        return std::unexpected(ParseError::UnexpectedToken);

    Condition *condition = new Condition();
    condition->column = column;
    condition->op = op;
    condition->value = value;
    return condition;
}

std::expected<Statement*, ParseError> Parser::parse()
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
    if (token.value == "CREATE")
        return parseCreate();
    if (token.value == "DROP")
        return parseDrop();
    if (token.value == "UPDATE")
        return parseUpdate();
    
    return std::unexpected(ParseError::UnexpectedToken);
}