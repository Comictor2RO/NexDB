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

CreateStatement *Parser::parseCreate() {
    consumeToken(); //Jumps over CREATE
    consumeToken(); //Jumps over TABLE
    std::string table = consumeToken().value; //Saves table name

    std::vector<Columns> columns;

    consumeToken(); //Jumps over (

    while (currentToken().value != ")" && currentToken().type != TokenType::END_OF_FILE)
    {
        if (currentToken().value == ",")
        {
            consumeToken(); //Jumps over ,
            continue;
        }
        Columns col;
        col.name = consumeToken().value; //Column name  ex: "id"
        col.type = consumeToken().value; //Column type  ex: "INT"
        columns.push_back(col);
    }

    consumeToken(); //Jumps over )

    return new CreateStatement(table, columns);
}

SelectStatement *Parser::parseSelect()
{
    consumeToken(); //Jumps over SELECT

    std::vector<std::string> columns;
    std::string table;

    while (currentToken().value != "FROM" && currentToken().type != TokenType::END_OF_FILE) //Checks until find FROM
    {
        if (currentToken().value == ",")
            consumeToken(); //Skips the punctuation
        else
            columns.push_back(consumeToken().value); //Saves the columns
    }

    consumeToken(); //Jumps over FROM
    table = consumeToken().value; //Name of the table

    Condition *condition = nullptr;
    if (currentToken().value == "WHERE") //Checks if there is a condition
    {
        consumeToken(); //Jumps over WHERE
        condition = parseCondition(); //Saves the condition
    }

    return new SelectStatement(columns, table, condition);
}

InsertStatement *Parser::parseInsert()
{
    consumeToken();
    consumeToken();

    std::string table = consumeToken().value; //Save table name
    std::vector<std::string> values;
    std::vector<std::string> columns;
    while (currentToken().value != "VALUES" && currentToken().type != TokenType::END_OF_FILE)
    {
        if (currentToken().value == "," || currentToken().value == ")" || currentToken().value == "(")
            consumeToken(); //Jumps over punctuation
        else
            columns.push_back(consumeToken().value); //Saves the columns
    }

    if (currentToken().value != "VALUES")
        return nullptr; // Sintaxa corecta: INSERT INTO table [ (col1,...) ] VALUES (val1,...)

    consumeToken(); //Jumps over VALUES

    while (currentToken().type != TokenType::END_OF_FILE)
    {
        if (currentToken().type == TokenType::PUNCTUATION)
            consumeToken(); //Jumps over punctuation
        else
            values.push_back(consumeToken().value); //Saves the values
    }

    return new InsertStatement(table, columns, values);
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

                return new DeleteStatement(table, condition);
            }
        }
        else
            return nullptr;
    }
    return nullptr;
    /*
    consumeToken(); //Jumps over DELETE
    consumeToken(); //Jumps over FROM

    std::string table = consumeToken().value;

    Condition *condition = nullptr;
    if (currentToken().value == "WHERE")
    {
        consumeToken(); //Jumps over WHERE
        condition = parseCondition(); //Saves the condition
    }

    return new DeleteStatement(table, condition);
    */
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
                return new DropStatement(table);
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
    /*
    std::string column = consumeToken().value;
    std::string op = consumeToken().value;
    std::string value = consumeToken().value;

    Condition *condition = new Condition();
    condition->column = column;
    condition->value = value;
    condition->op = op;
    return condition;
    */
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