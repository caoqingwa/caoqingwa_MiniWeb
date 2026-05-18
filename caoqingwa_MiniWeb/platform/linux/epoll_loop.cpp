#include "event_loop.h"
#include "http_handler.h"
#include "threadpool.h"
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <iostream>
#include <unistd.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <sys/socket.h>
#include <cerrno>
#include <climits>
#include <chrono>
#include <mutex>
#include <algorithm>
#include "timer.h"
#include "buffer.h"

namespace {
std::string get_executable_dir() {
    char buffer[PATH_MAX]{};
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return {};
    }
    buffer[len] = '\0';
    std::string path(buffer);
    const size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {};
    }
    return path.substr(0, pos);
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

size_t parse_content_length(const std::string& header_block, bool& ok) {
    ok = true;
    std::istringstream stream(header_block);
    std::string line;
    if (!std::getline(stream, line)) {
        ok = false;
        return 0;
    }

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const std::string key = to_lower(line.substr(0, colon));
        if (key == "content-length") {
            try {
                return static_cast<size_t>(std::stoul(line.substr(colon + 1)));
            }
            catch (...) {
                ok = false;
                return 0;
            }
        }
    }
    return 0;
}

bool try_extract_request(const std::string& buffer, std::string& request_text, size_t& consumed) {
    const size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }

    bool length_ok = true;
    const std::string header_block = buffer.substr(0, header_end);
    size_t content_length = parse_content_length(header_block, length_ok);

    size_t total = header_end + 4 + content_length;
    if (!length_ok) {
        total = header_end + 4;
    }

    if (buffer.size() < total) {
        return false;
    }

    request_text = buffer.substr(0, total);
    consumed = total;
    return true;
}

bool write_all(int fd, const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        const ssize_t n = write(fd, data.data() + total, data.size() - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}
}

class EpollLoop : public EventLoop {
private:
    int server_fd{ -1 }, epfd{ -1 };
    ThreadPool thread_pool{ 4 };
    std::unordered_map<int, int> client_ids;
    std::unordered_map<int, Buffer> recv_buffers;
    int next_client_id{ 1 };
    std::vector<int> free_client_ids;
    std::mutex state_mutex;
    TimerManager timer_manager;
    const std::chrono::seconds idle_timeout{ 30 };

    void close_client(int fd) {
        std::lock_guard<std::mutex> lock(state_mutex);
        auto it = client_ids.find(fd);
        if (it != client_ids.end()) {
            std::cout << "[client " << it->second << "] disconnected" << std::endl;
            free_client_ids.push_back(it->second);
            client_ids.erase(it);
        }
        recv_buffers.erase(fd);
        timer_manager.remove(fd);
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
    }

public:
    ~EpollLoop() override {
        if (epfd >= 0) {
            close(epfd);
            epfd = -1;
        }
        if (server_fd >= 0) {
            close(server_fd);
            server_fd = -1;
        }
    }

    void init(int port) override {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            throw std::runtime_error(std::string("setsockopt(SO_REUSEADDR) failed: ") + std::strerror(errno));
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
        }

        if (listen(server_fd, 32) < 0) {
            throw std::runtime_error(std::string("listen failed: ") + std::strerror(errno));
        }

        epfd = epoll_create1(0);
        if (epfd < 0) {
            throw std::runtime_error(std::string("epoll_create1 failed: ") + std::strerror(errno));
        }

        epoll_event ev{};
        ev.data.fd = server_fd;
        ev.events = EPOLLIN;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
            throw std::runtime_error(std::string("epoll_ctl ADD server_fd failed: ") + std::strerror(errno));
        }
    }

    void loop() override {
        epoll_event events[1024];

        const int wait_timeout_ms = 1000;

        while (true) {
            int n = epoll_wait(epfd, events, 1024, wait_timeout_ms);

            for (int i = 0; i < n; i++) {
                int fd = events[i].data.fd;

                if (fd == server_fd) {
                    int client = accept(server_fd, nullptr, nullptr);

                    if (client < 0) {
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        if (client_ids.find(client) != client_ids.end()) {
                            continue;
                        }

                        epoll_event ev{};
                        ev.data.fd = client;
                        ev.events = EPOLLIN;

                        epoll_ctl(epfd, EPOLL_CTL_ADD, client, &ev);

                        int client_id;
                        if (!free_client_ids.empty()) {
                            client_id = free_client_ids.back();
                            free_client_ids.pop_back();
                        }
                        else {
                            client_id = next_client_id++;
                        }
                        client_ids[client] = client_id;
                        recv_buffers.emplace(client, Buffer{});
                        timer_manager.touch(client);
                        std::cout << "[client " << client_id << "] connected" << std::endl;
                    }
                }
                else {
                    char buf[1024];
                    int len = read(fd, buf, sizeof(buf));

                    if (len <= 0) {
                        close_client(fd);
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        auto it = recv_buffers.find(fd);
                        if (it == recv_buffers.end()) {
                            continue;
                        }
                        it->second.append(buf, static_cast<size_t>(len));
                        timer_manager.touch(fd);
                    }

                    while (true) {
                        std::string request_text;
                        size_t consumed = 0;
                        {
                            std::lock_guard<std::mutex> lock(state_mutex);
                            auto it = recv_buffers.find(fd);
                            if (it == recv_buffers.end()) {
                                break;
                            }
                            const std::string pending_snapshot(it->second.peek(), it->second.readable_bytes());
                            if (!try_extract_request(pending_snapshot, request_text, consumed)) {
                                break;
                            }
                            it->second.retrieve(consumed);
                        }

                        HttpConn http_conn;
                        HttpRequest request;
                        HttpParseResult parse_result = http_conn.parse_request(request_text, request);
                        const bool keep_alive = (parse_result == HttpParseResult::Ok) ? request.keep_alive : false;

                        thread_pool.enqueue([this, fd, request, parse_result, keep_alive] {
                            HttpHandler handler;

                            if (parse_result != HttpParseResult::Ok) {
                                std::string response = handler.build_bad_request_response();
                                write_all(fd, response);
                                close_client(fd);
                                return;
                            }

                            std::vector<std::string> roots = {
                                "http",
                                "",
                                "../http",
                                "../../http",
                                "../../../http"
                            };

                            const std::string executable_dir = get_executable_dir();
                            if (!executable_dir.empty()) {
                                roots.insert(roots.begin(), executable_dir + "/http");
                                roots.insert(roots.begin() + 1, executable_dir);
                            }

                            std::string response = handler.build_response(request, roots);
                            write_all(fd, response);
                            if (!keep_alive) {
                                close_client(fd);
                            }
                        });

                        if (!keep_alive) {
                            break;
                        }
                    }
                }
            }

            const auto expired = timer_manager.get_expired(idle_timeout);
            for (int fd : expired) {
                close_client(fd);
            }
        }
    }
};

EventLoop* create_event_loop() {
    return new EpollLoop();
}