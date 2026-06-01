#pragma once
#include <string>
#include <cstddef>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <sstream>

struct HttpRequest {
    std::string method;
    std::string raw_target;
    std::string path;
    std::string query;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool keep_alive{ false };
};

enum class HttpParseResult {
    Ok,
    NeedMoreData,
    BadRequest
};

class HttpConn {
public:
    static constexpr size_t kMaxHeaderSize = 8192;
    static constexpr size_t kMaxBodySize = 1024 * 1024;

    HttpParseResult parse_request(const std::string& raw, HttpRequest& request) const;
    HttpParseResult parse_request(const std::string& raw, HttpRequest& request, size_t& consumed) const;
};
