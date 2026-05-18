#pragma once
#include <string>
#include <vector>
#include "http_conn.h"

class HttpHandler {
public:
    std::string build_response(const HttpRequest& request, const std::vector<std::string>& search_roots) const;
    std::string build_bad_request_response() const;
};
