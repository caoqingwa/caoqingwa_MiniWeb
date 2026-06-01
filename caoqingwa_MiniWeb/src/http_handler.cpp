#include "http_handler.h"


namespace {
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

std::string join_root(const std::string& root, const std::string& relative_path) {
    if (root.empty()) {
        return relative_path;
    }
    char last = root.back();
    if (last == '/' || last == '\\') {
        return root + relative_path;
    }
    return root + "/" + relative_path;
}

std::string sanitize_relative_path(const std::string& relative_path) {
    if (relative_path.empty()) {
        return {};
    }

    if (relative_path[0] == '/' || relative_path[0] == '\\') {
        return {};
    }

    std::string normalized;
    std::vector<std::string> parts;
    std::string current;

    auto flush_part = [&]() {
        if (current.empty() || current == ".") {
            current.clear();
            return true;
        }
        if (current == "..") {
            return false;
        }
        parts.push_back(current);
        current.clear();
        return true;
    };

    for (char ch : relative_path) {
        if (ch == '/' || ch == '\\') {
            if (!flush_part()) {
                return {};
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!flush_part()) {
        return {};
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            normalized.push_back('/');
        }
        normalized.append(parts[i]);
    }

    return normalized;
}

std::string get_content_type(const std::string& path) {
    const std::string lower = to_lower(path);
    if (ends_with(lower, ".html")) {
        return "text/html; charset=utf-8";
    }
    if (ends_with(lower, ".css")) {
        return "text/css; charset=utf-8";
    }
    if (ends_with(lower, ".js")) {
        return "application/javascript; charset=utf-8";
    }
    if (ends_with(lower, ".png")) {
        return "image/png";
    }
    if (ends_with(lower, ".jpg") || ends_with(lower, ".jpeg")) {
        return "image/jpeg";
    }
    if (ends_with(lower, ".gif")) {
        return "image/gif";
    }
    if (ends_with(lower, ".svg")) {
        return "image/svg+xml";
    }
    if (ends_with(lower, ".ico")) {
        return "image/x-icon";
    }
    if (ends_with(lower, ".webp")) {
        return "image/webp";
    }
    if (ends_with(lower, ".bmp")) {
        return "image/bmp";
    }
    if (ends_with(lower, ".ttf")) {
        return "font/ttf";
    }
    if (ends_with(lower, ".woff")) {
        return "font/woff";
    }
    if (ends_with(lower, ".woff2")) {
        return "font/woff2";
    }
    if (ends_with(lower, ".otf")) {
        return "font/otf";
    }
    return "text/plain; charset=utf-8";
}

std::string pick_default_html(const std::vector<std::string>& search_roots) {
    std::string index_candidate;
    for (const auto& root : search_roots) {
        std::error_code ec;
        std::filesystem::path base = root.empty() ? std::filesystem::path(".") : std::filesystem::path(root);
        if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const std::filesystem::path path = entry.path();
            const std::string filename = path.filename().string();
            if (!ends_with(to_lower(filename), ".html")) {
                continue;
            }
            if (to_lower(filename) == "index.html") {
                if (index_candidate.empty()) {
                    index_candidate = filename;
                }
                continue;
            }
            return filename;
        }
    }

    return index_candidate.empty() ? "index.html" : index_candidate;
}
}

std::string HttpHandler::build_bad_request_response() const {
    const std::string body = "<h1>400 Bad Request</h1>";
    return
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" +
        body;
}

std::string HttpHandler::build_payload_too_large_response() const {
    const std::string body = "<h1>413 Payload Too Large</h1>";
    return
        "HTTP/1.1 413 Payload Too Large\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" +
        body;
}

std::string HttpHandler::build_response(const HttpRequest& request,
                                        const std::vector<std::string>& search_roots) const {
    std::string request_path = request.path;
    if (request_path.empty() || request_path == "/") {
        request_path = "/" + pick_default_html(search_roots);
    }
    else {
        const size_t last_slash = request_path.find_last_of("/\\");
        const size_t last_dot = request_path.find_last_of('.');
        if (last_dot == std::string::npos || (last_slash != std::string::npos && last_dot < last_slash)) {
            request_path += ".html";
        }
    }

    std::string relative_path = request_path[0] == '/' ? request_path.substr(1) : request_path;
    relative_path = sanitize_relative_path(relative_path);
    if (relative_path.empty()) {
        const std::string body = "<h1>404 Not Found</h1>";
        return
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: " + std::string(request.keep_alive ? "keep-alive" : "close") + "\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" +
            body;
    }

    std::ifstream file;
    for (const auto& root : search_roots) {
        const std::string candidate = join_root(root, relative_path);
        file.open(candidate, std::ios::binary);
        if (file.is_open()) {
            break;
        }
        file.clear();
    }

    if (file.is_open()) {
        std::ostringstream body_stream;
        body_stream << file.rdbuf();
        std::string body = body_stream.str();

        std::string content_type = get_content_type(relative_path);
        return
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: " + content_type + "\r\n"
            "Connection: " + std::string(request.keep_alive ? "keep-alive" : "close") + "\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" +
            body;
    }

    const std::string body = "<h1>404 Not Found</h1>";
    return
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: " + std::string(request.keep_alive ? "keep-alive" : "close") + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" +
        body;
}
