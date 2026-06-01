#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "http_conn.h"

class HttpHandler {
public:
    std::string build_response(const HttpRequest& request, const std::vector<std::string>& search_roots) const;
    std::string build_bad_request_response() const;
    std::string build_payload_too_large_response() const;
};
