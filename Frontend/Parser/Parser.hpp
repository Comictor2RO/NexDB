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
#include <memory>

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
        std::expected<std::unique_ptr<Statement>, ParseError> parse();
    private:
        std::vector<Token> tokens;
        int position;

        Token currentToken();
        Token consumeToken();
        std::string expectToken(TokenType type);
        bool expectToken(TokenType type, const std::string &value);

        std::expected<std::unique_ptr<CreateStatement>, ParseError> parseCreate();
        std::expected<std::unique_ptr<SelectStatement>, ParseError> parseSelect();
        std::expected<std::unique_ptr<InsertStatement>, ParseError> parseInsert();
        std::expected<std::unique_ptr<DeleteStatement>, ParseError> parseDelete();
        std::expected<std::unique_ptr<DropStatement>, ParseError> parseDrop();
        std::expected<std::unique_ptr<UpdateStatement>, ParseError> parseUpdate();
        std::expected<Condition *, ParseError> parseCondition();
};


#endif