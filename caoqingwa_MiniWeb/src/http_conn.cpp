#include "http_conn.h"
#include "util.h"
#include <limits>


namespace {
std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        begin++;
    }

    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }

    return s.substr(begin, end - begin);
}
}

HttpParseResult HttpConn::parse_request(const std::string& raw, HttpRequest& request) const {
    size_t consumed = 0;
    return parse_request(raw, request, consumed);
}

HttpParseResult HttpConn::parse_request(const std::string& raw, HttpRequest& request, size_t& consumed) const {
    consumed = 0;
    const size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return HttpParseResult::NeedMoreData;
    }
    if (header_end > kMaxHeaderSize) {
        return HttpParseResult::BadRequest;
    }

    request = HttpRequest{};
    const std::string header_block = raw.substr(0, header_end);
    std::istringstream stream(header_block);

    std::string request_line;
    if (!std::getline(stream, request_line)) {
        return HttpParseResult::BadRequest;
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    {
        std::istringstream line_stream(request_line);
        if (!(line_stream >> request.method >> request.raw_target >> request.version)) {
            return HttpParseResult::BadRequest;
        }
    }

    if (request.version.rfind("HTTP/", 0) != 0) {
        return HttpParseResult::BadRequest;
    }

    const size_t query_pos = request.raw_target.find('?');
    if (query_pos == std::string::npos) {
        request.path = request.raw_target;
    }
    else {
        request.path = request.raw_target.substr(0, query_pos);
        request.query = request.raw_target.substr(query_pos + 1);
    }
    if (request.path.empty()) {
        request.path = "/";
    }

    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return HttpParseResult::BadRequest;
        }

        std::string key = to_lower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        request.headers[key] = value;
    }

    size_t content_length = 0;
    auto it = request.headers.find("content-length");
    if (it != request.headers.end()) {
        try {
            const unsigned long long parsed = std::stoull(it->second);
            if (parsed > std::numeric_limits<size_t>::max()) {
                return HttpParseResult::BadRequest;
            }
            content_length = static_cast<size_t>(parsed);
            if (content_length > kMaxBodySize) {
                return HttpParseResult::BadRequest;
            }
        }
        catch (...) {
            return HttpParseResult::BadRequest;
        }
    }

    const size_t body_begin = header_end + 4;
    if (raw.size() - body_begin < content_length) {
        return HttpParseResult::NeedMoreData;
    }

    request.body = raw.substr(body_begin, content_length);
    consumed = body_begin + content_length;

    std::string connection = "";
    auto conn_it = request.headers.find("connection");
    if (conn_it != request.headers.end()) {
        connection = to_lower(conn_it->second);
    }

    if (request.version == "HTTP/1.1") {
        request.keep_alive = (connection != "close");
    }
    else {
        request.keep_alive = (connection == "keep-alive");
    }

    return HttpParseResult::Ok;
}