#include "Lexer.hpp"

//Constructor
Lexer::Lexer(std::string input)
    : input(std::move(input)), position(0)
{}

//Skip whitespace
void Lexer::skipWhitespace()
{
    while(position < input.length() && isspace(input[position]))
    {
        position++;
    }
}

//Read word and identify if it is a keyword or an identifier
Token Lexer::readWord()
{
    std::string word;

    while(position < input.length() && (isalnum(input[position]) || input[position] == '_'))
    {
        word += input[position];
        position++;
    }
    std::string uppercaseWord = word;
            
    for(char &c : uppercaseWord)
        c = std::toupper(c);

    if(uppercaseWord == "SELECT" || uppercaseWord == "INSERT" || uppercaseWord == "DELETE"
        || uppercaseWord == "FROM" || uppercaseWord == "WHERE" || uppercaseWord == "INTO"
        || uppercaseWord == "VALUES" || uppercaseWord == "CREATE" || uppercaseWord == "TABLE"
        || uppercaseWord == "INT" || uppercaseWord == "STRING" || uppercaseWord == "DROP")
    {
        return {TokenType::KEYWORD, uppercaseWord};
    }
    else
        return{TokenType::IDENTIFIER, word};
}

//Read number
Token Lexer::readNumber()
{
    std::string number;
    while(position < input.length() && isdigit(input[position]))
    {
        number += input[position];
        position++;
    }
    return {TokenType::NUMBER, number};
}

//Read string jumps over '' and gets the value inside them
Token Lexer::readString()
{
    position++;
    std::string str;
    while(position < input.length() && input[position] != '\'')
    {
        str += input[position];
        position++;
    }
    position++;
    return {TokenType::STRING, str};
}

//Read operator and if checks if after there is an = for !, < and >
Token Lexer::readOperator()
{
    std::string op;
    op += input[position];

    position++;
    if(position < input.length() && input[position] == '=')
    {
        op += '=';
        position++;
    }

    return {TokenType::OPERATOR, op};
}

//Read punctuation reads ( ) , 
Token Lexer::readPunctuation()
{
    const char punctuation = input[position];
    position++;
    return {TokenType::PUNCTUATION, std::string(1, punctuation)};
}

//Read widlcard *
Token Lexer::readWildcard()
{
    const char wildcard = input[position];
    position++;
    return {TokenType::WILDCARD, std::string(1, wildcard)};
}

//Tokenize
std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;

    while(position < input.length())
    {
        char currentCharacter = input[position];

        if(isspace(currentCharacter))
        {
            skipWhitespace();
        }
        else if(isalpha(currentCharacter))
        {
            tokens.push_back(readWord());
        }
        else if(isdigit(currentCharacter))
        {
            tokens.push_back(readNumber());
        }
        else if(currentCharacter == '\'')
        {
            tokens.push_back(readString());
        }
        else if(currentCharacter == '=' || currentCharacter == '>' || currentCharacter == '<'
            || currentCharacter == '!')
        {
            tokens.push_back(readOperator());
        }
        else if(currentCharacter == '(' || currentCharacter == ')' || currentCharacter == ',')
        {
            tokens.push_back(readPunctuation());
        }
        else if(currentCharacter == '*')
        {
            tokens.push_back(readWildcard());
        }
        else
        {
            position++;
        }
    }

    tokens.push_back({TokenType::END_OF_FILE, ""});
    return tokens;
}