#ifndef STRING_UTILS_HPP
#define STRING_UTILS_HPP

#include <string>
#include <vector>
#include <sstream>

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

#endif