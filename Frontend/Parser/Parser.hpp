#ifndef PARSER_HPP
#define PARSER_HPP

#include "../Lexer/Lexer.hpp"
#include "../../AST/SelectStatement/SelectStatement.hpp"
#include "../../AST/InsertStatement/InsertStatement.hpp"
#include "../../AST/DeleteStatement/DeleteStatement.hpp"
#include "../../AST/CreateStatement/CreateStatement.hpp"
#include "../../AST/DropStatement/DropStatement.hpp"
#include "../../AST/UpdateStatement/UpdateStatement.hpp"
#include "../../AST/CreateDatabaseStatement/CreateDatabaseStatement.hpp"
#include "../../AST/UseDatabaseStatement/UseDatabaseStatement.hpp"
#include <vector>
#include <string>
#include <expected>
#include <memory>

enum class ParseError {
    UnexpectedToken, MissingKeyword, MissingPunctuation, InvalidIdentifier,
    InvalidType, EmptyColumns, ValuesMismatch, TrailingComma, ExtraTokens,
    END_OF_FILE
};

//Maps a ParseError to a clear, human-readable English message for the client/GUI
inline std::string parseErrorMessage(ParseError e)
{
    switch (e)
    {
        case ParseError::UnexpectedToken:    return "Unexpected token in query";
        case ParseError::MissingKeyword:     return "Missing required keyword";
        case ParseError::MissingPunctuation: return "Missing punctuation (e.g. parenthesis or comma)";
        case ParseError::InvalidIdentifier:  return "Invalid identifier";
        case ParseError::InvalidType:        return "Invalid column type";
        case ParseError::EmptyColumns:       return "Empty column or value list";
        case ParseError::ValuesMismatch:     return "Column count does not match value count";
        case ParseError::TrailingComma:      return "Trailing comma";
        case ParseError::ExtraTokens:        return "Unexpected tokens after end of statement";
        case ParseError::END_OF_FILE:        return "Unexpected end of query";
    }
    return "Unknown parse error";
}

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
        std::expected<std::unique_ptr<CreateDatabaseStatement>, ParseError> parseCreateDatabase();
        std::expected<std::unique_ptr<UseDatabaseStatement>, ParseError> parseUseDatabase();
        std::expected<Condition *, ParseError> parseCondition();
};


#endif