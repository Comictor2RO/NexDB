#ifndef PARSER_HPP
#define PARSER_HPP

#include "../Lexer/Lexer.hpp"
#include "../../AST/SelectStatement/SelectStatement.hpp"
#include "../../AST/InsertStatement/InsertStatement.hpp"
#include "../../AST/DeleteStatement/DeleteStatement.hpp"
#include "../../AST/CreateStatement/CreateStatement.hpp"
#include "../../AST/DropStatement/DropStatement.hpp"
#include "../../AST/UpdateStatement/UpdateStatement.hpp"
#include <vector>
#include <expected>

enum class ParseError {
    UnexpectedToken, MissingKeyword, MissingPunctuation, InvalidIdentifier,
    InvalidType, EmptyColumns, ValuesMismatch, TrailingComma, ExtraTokens,
    END_OF_FILE
};

struct ParseResult {
    bool success;
    ParseError error = ParseError::UnexpectedToken;
    std::string message;
};

class Parser {
    public:
        Parser(const std::vector<Token> &tokens);
        std::expected<Statement *, ParseError>parse();
    private:
        std::vector<Token> tokens;
        int position;

        Token currentToken();
        Token consumeToken();
        std::string expectToken(TokenType type);
        bool expectToken(TokenType type, const std::string &value);

        std::expected<CreateStatement *, ParseError>parseCreate();
        std::expected<SelectStatement *, ParseError>parseSelect();
        std::expected<InsertStatement *, ParseError>parseInsert();
        std::expected<DeleteStatement *, ParseError>parseDelete();
        std::expected<DropStatement *, ParseError>parseDrop();
        std::expected<UpdateStatement *, ParseError>parseUpdate();
        std::expected<Condition *, ParseError>parseCondition();
};


#endif