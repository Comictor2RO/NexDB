#ifndef TOKEN_TYPE_HPP
#define TOKEN_TYPE_HPP

//Token types for the tokens that the lexer will return
enum class TokenType{
    KEYWORD,
    IDENTIFIER,
    STRING,
    NUMBER,
    OPERATOR,
    PUNCTUATION,
    WILDCARD,
    DATABASE,
    USE,
    END_OF_FILE
};

#endif