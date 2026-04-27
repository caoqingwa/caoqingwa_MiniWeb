#pragma once
#include <string>
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
    HttpParseResult parse_request(const std::string& raw, HttpRequest& request) const;
};
