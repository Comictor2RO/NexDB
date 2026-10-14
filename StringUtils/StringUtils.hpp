#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <string>
#include <vector>
#include <sstream>
#include "../AST/Row/Row.hpp"

inline std::vector<std::string> split(const std::string & str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter))
    {
        tokens.push_back(token);
    }

    return tokens;
}

// Like split, but keeps every field, including empty ones at the end ("1|" -> {"1", ""})
// and the single empty field of an empty string. Use this for row data, where an empty
// string value is a real column.
inline std::vector<std::string> splitFields(const std::string &str, char delimiter)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while (true)
    {
        size_t pos = str.find(delimiter, start);
        if (pos == std::string::npos)
        {
            fields.push_back(str.substr(start));
            return fields;
        }
        fields.push_back(str.substr(start, pos - start));
        start = pos + 1;
    }
}

inline std::string walEncode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '%')  out += "%25";
        else if (c == '|')  out += "%7C";
        else if (c == '~')  out += "%7E";
        else if (c == '\n') out += "%0A";
        else if (c == '\r') out += "%0D";
        else                out += c;
    }
    return out;
}

inline std::string walDecode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (int i = 0; i < (int)s.size(); i++) {
        if (s[i] == '%' && i + 2 < (int)s.size()) {
            char hi = s[i + 1], lo = s[i + 2];
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hexVal(hi), l = hexVal(lo);
            if (h >= 0 && l >= 0) {
                out += (char)((h << 4) | l);
                i += 2;
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Storage/WAL marker for SQL NULL. walEncode only ever emits '%' followed by two hex
// digits, so no encoded string can be exactly "%N" and the marker is unambiguous.
inline const std::string NULL_MARKER = "%N";

inline std::string encodeValue(const Value &v)
{
    return v ? walEncode(*v) : NULL_MARKER;
}

inline Value decodeValue(const std::string &s)
{
    if (s == NULL_MARKER)
        return std::nullopt;
    return walDecode(s);
}

// Renders a value as an SQL literal for WAL replay: NULL, or a quoted string with
// ' doubled. Quoting numbers too is safe because every value is validated as text.
inline std::string sqlLiteral(const Value &v)
{
    if (!v)
        return "NULL";
    std::string out = "'";
    for (char c : *v) {
        if (c == '\'') out += "''";
        else            out += c;
    }
    return out + "'";
}

#endif