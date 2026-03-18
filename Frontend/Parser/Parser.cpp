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

CreateStatement *Parser::parseCreate()
{
    std::string table;
    std::vector<Columns> columns;

    if (!expectToken(TokenType::KEYWORD, "CREATE"))
        return nullptr;

    if (!expectToken(TokenType::KEYWORD, "TABLE"))
        return nullptr;

    table = expectToken(TokenType::IDENTIFIER);
    if (table == "")
        return nullptr;

    if (!expectToken(TokenType::PUNCTUATION, "("))
        return nullptr;

    while (true)
    {
        if (currentToken().type == TokenType::END_OF_FILE)
            return nullptr;

        if (currentToken().value == ")")
            break;

        Columns col;

        col.name = expectToken(TokenType::IDENTIFIER);
        if (col.name == "")
            return nullptr;

        col.type = expectToken(TokenType::KEYWORD);
        if (col.type != "INT" && col.type != "STRING")
            return nullptr;

        columns.push_back(col);

        if (currentToken().value == ",")
        {
            consumeToken(); // consume comma
            if (currentToken().value == ")")
                return nullptr; // reject trailing comma
            continue;
        }

        if (currentToken().value == ")")
            break;

        return nullptr;
    }

    if (columns.empty())
        return nullptr;

    if (!expectToken(TokenType::PUNCTUATION, ")"))
        return nullptr;

    if (currentToken().type == TokenType::END_OF_FILE)
        return new CreateStatement(table, columns);
    else
        return nullptr;
}

SelectStatement *Parser::parseSelect()
{
    std::vector<std::string> columns;
    std::string table;

    if (!expectToken(TokenType::KEYWORD, "SELECT"))
        return nullptr;

    if (expectToken(TokenType::WILDCARD, "*"))
        columns.push_back("*");
    else
    {
        while (true)
        {
            std::string column = expectToken(TokenType::IDENTIFIER);
            if (column == "")
                return nullptr;
            columns.push_back(column);
            if (currentToken().value == ",")
            {
                consumeToken();
                if (currentToken().type != TokenType::IDENTIFIER)
                    return nullptr;
            }
            else
                break;
        }
    }
    if (!expectToken(TokenType::KEYWORD, "FROM"))
        return nullptr;

    table = expectToken(TokenType::IDENTIFIER);
    if (table == "")
        return nullptr;

    Condition *condition = nullptr;
    if (expectToken(TokenType::KEYWORD, "WHERE")) {
        condition = parseCondition();
        if (condition == nullptr)
            return nullptr;
    }

    if (currentToken().type == TokenType::END_OF_FILE)
        return new SelectStatement(columns, table, condition);
    else
        return nullptr;
}

InsertStatement *Parser::parseInsert()
{
    std::string table;
    std::vector<std::string> values;
    std::vector<std::string> columns;

    if (!expectToken(TokenType::KEYWORD, "INSERT"))
        return nullptr;

    if (!expectToken(TokenType::KEYWORD, "INTO"))
        return nullptr;

    table = expectToken(TokenType::IDENTIFIER);
    if (table == "")
        return nullptr;

    if (expectToken(TokenType::PUNCTUATION, "("))
    {
        while (true) {
            std::string col = expectToken(TokenType::IDENTIFIER);
            if (col == "")
                return nullptr;
            columns.push_back(col);
            if (currentToken().value == ",")
            {
                consumeToken();
                if (currentToken().type != TokenType::IDENTIFIER)
                    return nullptr;
            }
            else if (currentToken().value == ")")
                break;
            else
                return nullptr;
        }
        if (columns.empty())
            return nullptr;

        if (!expectToken(TokenType::PUNCTUATION, ")"))
            return nullptr;
    }

    if (expectToken(TokenType::KEYWORD, "VALUES"))
    {
        if (expectToken(TokenType::PUNCTUATION, "("))
        {
            while (true)
            {
                std::string val;
                if (currentToken().type == TokenType::NUMBER)
                {
                    val = expectToken(TokenType::NUMBER);
                    if (val == "")
                        return nullptr;

                    values.push_back(val);
                }
                else if (currentToken().type == TokenType::STRING)
                {
                    val = expectToken(TokenType::STRING);
                    if (val == "")
                        return nullptr;

                    values.push_back(val);
                }
                else if (currentToken().type == TokenType::IDENTIFIER)
                {
                    val = expectToken(TokenType::IDENTIFIER);
                    if (val == "")
                        return nullptr;

                    values.push_back(val);
                }


                if (currentToken().value == ")")
                    break;
                else if (currentToken().value == ",")
                    consumeToken();
                else
                    return nullptr;
            }
            if (values.empty())
                return nullptr;

            if (!expectToken(TokenType::PUNCTUATION, ")"))
                return nullptr;
        }
        else
            return nullptr;
    }
    else
        return nullptr;

    if (currentToken().type == TokenType::END_OF_FILE)
        return new InsertStatement(table, columns, values);
    else
        return nullptr;
}

DeleteStatement *Parser::parseDelete()
{
    if (expectToken(TokenType::KEYWORD, "DELETE"))
    {
        if (expectToken(TokenType::KEYWORD, "FROM"))
        {
            std::string table = expectToken(TokenType::IDENTIFIER);
            if (table == "")
                return nullptr;
            else
            {
                Condition *condition = nullptr;
                if (expectToken(TokenType::KEYWORD, "WHERE"))
                    condition = parseCondition();

                if (currentToken().type == TokenType::END_OF_FILE)
                    return new DeleteStatement(table, condition);
                else
                    return nullptr;
            }
        }
        else
            return nullptr;
    }
    return nullptr;
}

DropStatement *Parser::parseDrop()
{
    if (expectToken(TokenType::KEYWORD, "DROP"))
    {
        if (expectToken(TokenType::KEYWORD, "TABLE"))
        {
            std::string table = expectToken(TokenType::IDENTIFIER);
            if (table == "")
                return nullptr;
            else
            {
                if (currentToken().type == TokenType::END_OF_FILE)
                    return new DropStatement(table);
                return nullptr;
            }
        }
        else
            return nullptr;
    }
    else
        return nullptr;
}

Condition *Parser::parseCondition()
{
    std::string column = expectToken(TokenType::IDENTIFIER);
    if (column == "")
        return nullptr;
    else
    {
        std::string op = expectToken(TokenType::OPERATOR);
        if (op == "")
            return nullptr;
        else
        {
            if (currentToken().type == TokenType::STRING)
            {
                std::string value = expectToken(TokenType::STRING);
                if (value == "")
                    return nullptr;
                else {
                    Condition *condition = new Condition();
                    condition->column = column;
                    condition->op = op;
                    condition->value = value;
                    return condition;
                }
            }
            else if (currentToken().type == TokenType::NUMBER)
            {
                std::string value = expectToken(TokenType::NUMBER);
                if (value == "")
                    return nullptr;
                else {
                    Condition *condition = new Condition();
                    condition->column = column;
                    condition->op = op;
                    condition->value = value;
                    return condition;
                }
            }
            else if (currentToken().type == TokenType::IDENTIFIER)
            {
                std::string value = expectToken(TokenType::IDENTIFIER);
                if (value == "")
                    return nullptr;
                else {
                    Condition *condition = new Condition();
                    condition->column = column;
                    condition->op = op;
                    condition->value = value;
                    return condition;
                }
            }
            else
                return nullptr;
        }
    }
}

Statement *Parser::parse()
{
    Token token = currentToken();

    if (token.type != TokenType::KEYWORD)
        return nullptr;
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
    return nullptr;
}