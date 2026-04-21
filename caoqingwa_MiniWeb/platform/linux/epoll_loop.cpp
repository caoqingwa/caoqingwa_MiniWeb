#include "event_loop.h"
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class EpollLoop : public EventLoop {
private:
    int server_fd, epfd;
    std::unordered_map<int, int> client_ids;
    int next_client_id{ 1 };
    std::vector<int> free_client_ids;

public:
    void init(int port) override {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        bind(server_fd, (sockaddr*)&addr, sizeof(addr));
        listen(server_fd, 5);

        epfd = epoll_create(1);

        epoll_event ev{};
        ev.data.fd = server_fd;
        ev.events = EPOLLIN;

        epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev);
    }

    void loop() override {
        epoll_event events[1024];

        while (true) {
            int n = epoll_wait(epfd, events, 1024, -1);

            for (int i = 0; i < n; i++) {
                int fd = events[i].data.fd;

                if (fd == server_fd) {
                    int client = accept(server_fd, nullptr, nullptr);

                    if (client < 0) {
                        continue;
                    }

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
                    std::cout << "[client " << client_id << "] connected" << std::endl;
                }
                else {
                    char buf[1024];
                    int len = read(fd, buf, sizeof(buf));

                    if (len <= 0) {
                        auto it = client_ids.find(fd);
                        if (it != client_ids.end()) {
                            std::cout << "[client " << it->second << "] disconnected" << std::endl;
                            free_client_ids.push_back(it->second);
                            client_ids.erase(it);
                        }

                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                    }
                    else {
                        auto it = client_ids.find(fd);
                        int client_id = (it != client_ids.end()) ? it->second : -1;

                        std::string message(buf, buf + len);
                        std::cout << "[client " << client_id << "] message: " << message << std::endl;
                        std::string request_path = "/";
                        std::istringstream request_stream(message);
                        std::string method;
                        std::string version;
                        request_stream >> method >> request_path >> version;

                        size_t query_pos = request_path.find('?');
                        if (query_pos != std::string::npos) {
                            request_path = request_path.substr(0, query_pos);
                        }
                        if (request_path.empty() || request_path == "/") {
                            request_path = "/tetris.html";
                        }

                        std::string relative_path = request_path[0] == '/'
                            ? request_path.substr(1)
                            : request_path;

                        std::ifstream file;
                        const std::string candidates[] = {
                            std::string("src/") + relative_path,
                            relative_path,
                            std::string("../src/") + relative_path,
                            std::string("../../src/") + relative_path
                        };
                        for (const auto& path : candidates) {
                            file.open(path, std::ios::binary);
                            if (file.is_open()) {
                                break;
                            }
                            file.clear();
                        }

                        std::string response;
                        if (file.is_open()) {
                            std::ostringstream body_stream;
                            body_stream << file.rdbuf();
                            std::string body = body_stream.str();

                            std::string content_type = "text/plain; charset=utf-8";
                            if (relative_path.size() >= 5 && relative_path.substr(relative_path.size() - 5) == ".html") {
                                content_type = "text/html; charset=utf-8";
                            }
                            else if (relative_path.size() >= 4 && relative_path.substr(relative_path.size() - 4) == ".css") {
                                content_type = "text/css; charset=utf-8";
                            }
                            else if (relative_path.size() >= 3 && relative_path.substr(relative_path.size() - 3) == ".js") {
                                content_type = "application/javascript; charset=utf-8";
                            }

                            response =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: " + content_type + "\r\n"
                                "Connection: keep-alive\r\n"
                                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                                "\r\n" +
                                body;
                        }
                        else {
                            const std::string body = "<h1>404 Not Found</h1>";
                            response =
                                "HTTP/1.1 404 Not Found\r\n"
                                "Content-Type: text/html; charset=utf-8\r\n"
                                "Connection: keep-alive\r\n"
                                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                                "\r\n" +
                                body;
                        }

                        write(fd, response.c_str(), response.size());
                    }
                }
            }
        }
    }
};

EventLoop* create_event_loop() {
    return new EpollLoop();
}