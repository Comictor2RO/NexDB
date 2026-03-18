#ifndef PARSER_HPP
#define PARSER_HPP

#include "../Lexer/Lexer.hpp"
#include "../../AST/SelectStatement/SelectStatement.hpp"
#include "../../AST/InsertStatement/InsertStatement.hpp"
#include "../../AST/DeleteStatement/DeleteStatement.hpp"
#include "../../AST/CreateStatement/CreateStatement.hpp"
#include <vector>

#include "../../AST/DropStatement/DropStatement.hpp"

class Parser {
    public:
        Parser(const std::vector<Token> &tokens);
        Statement *parse();
    private:
        std::vector<Token> tokens;
        int position;

        Token currentToken();
        Token consumeToken();
        std::string expectToken(TokenType type);
        bool expectToken(TokenType type, const std::string &value);

        CreateStatement *parseCreate();
        SelectStatement *parseSelect();
        InsertStatement *parseInsert();
        DeleteStatement *parseDelete();
        DropStatement *parseDrop();
        Condition *parseCondition();
};


#endif