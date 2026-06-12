#pragma once
#include <string>
#include <cctype>
#include <algorithm>
#include <cstddef>

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

inline std::string url_decode(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            auto hex_to_int = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex_to_int(str[i + 1]);
            int lo = hex_to_int(str[i + 2]);
            if (hi >= 0 && lo >= 0) {
                result += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}
