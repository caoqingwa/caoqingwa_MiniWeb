#include "event_loop.h"
#include "http_conn.h"
#include <winsock2.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "threadpool.h"
#pragma comment(lib, "ws2_32.lib")

class SelectLoop : public EventLoop {
private:
    SOCKET server_fd{ INVALID_SOCKET };
    fd_set master_set{};
    ThreadPool thread_pool{ 4 };
    std::unordered_map<SOCKET, int> client_ids;
    std::unordered_map<SOCKET, std::string> recv_buffers;
    int next_client_id{ 1 };
    std::vector<int> free_client_ids;

public:
    SelectLoop() {
        FD_ZERO(&master_set);
    }

    ~SelectLoop() override {
        if (server_fd != INVALID_SOCKET) {
            closesocket(server_fd);
            server_fd = INVALID_SOCKET;
        }
        WSACleanup();
    }

    void init(int port) override {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
            throw std::runtime_error("WSAStartup failed");
        }

        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == INVALID_SOCKET) {
            throw std::runtime_error("socket failed");
        }

        const BOOL optval = TRUE;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval), sizeof(optval));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            throw std::runtime_error("bind failed");
        }

        if (listen(server_fd, 32) == SOCKET_ERROR) {
            throw std::runtime_error("listen failed");
        }

        FD_ZERO(&master_set);
        FD_SET(server_fd, &master_set);
    }

    void loop() override {
        while (true) {
            fd_set read_set = master_set;
            int ready = select(0, &read_set, nullptr, nullptr, nullptr);
            if (ready == SOCKET_ERROR) {
                continue;
            }

            for (u_int i = 0; i < read_set.fd_count; i++) {
                SOCKET sock = read_set.fd_array[i];

                if (sock == server_fd) {
                    sockaddr_in client_addr{};
                    int addr_len = sizeof(client_addr);
                    SOCKET client = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
                    if (client == INVALID_SOCKET) {
                        continue;
                    }

                    if (client_ids.find(client) != client_ids.end()) {
                        closesocket(client);
                        continue;
                    }

                    FD_SET(client, &master_set);

                    int client_id;
                    if (!free_client_ids.empty()) {
                        client_id = free_client_ids.back();
                        free_client_ids.pop_back();
                    }
                    else {
                        client_id = next_client_id++;
                    }

                    client_ids[client] = client_id;
                    recv_buffers[client].clear();
                    std::cout << "[client " << client_id << "] connected" << std::endl;
                }
                else {
                    char buf[1024];
                    int len = recv(sock, buf, sizeof(buf), 0);

                    if (len <= 0) {
                        auto it = client_ids.find(sock);
                        if (it != client_ids.end()) {
                            std::cout << "[client " << it->second << "] disconnected" << std::endl;
                            free_client_ids.push_back(it->second);
                            client_ids.erase(it);
                        }

                        recv_buffers.erase(sock);
                        closesocket(sock);
                        FD_CLR(sock, &master_set);
                    }
                    else {
                        auto it = client_ids.find(sock);
                        int client_id = (it != client_ids.end()) ? it->second : -1;

                        std::string chunk(buf, buf + len);
                        std::cout << "[client " << client_id << "] recv chunk: " << chunk << std::endl;

                        std::string& pending = recv_buffers[sock];
                        pending.append(buf, static_cast<size_t>(len));

                        HttpConn http_conn;
                        HttpRequest request;
                        HttpParseResult parse_result = http_conn.parse_request(pending, request);

                        if (parse_result == HttpParseResult::NeedMoreData) {
                            continue;
                        }

                        if (parse_result == HttpParseResult::BadRequest) {
                            const std::string body = "<h1>400 Bad Request</h1>";
                            std::string response =
                                "HTTP/1.1 400 Bad Request\r\n"
                                "Content-Type: text/html; charset=utf-8\r\n"
                                "Connection: close\r\n"
                                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                                "\r\n" +
                                body;

                            send(sock, response.c_str(), static_cast<int>(response.size()), 0);

                            auto bad_it = client_ids.find(sock);
                            if (bad_it != client_ids.end()) {
                                std::cout << "[client " << bad_it->second << "] bad request, disconnected" << std::endl;
                                free_client_ids.push_back(bad_it->second);
                                client_ids.erase(bad_it);
                            }

                            recv_buffers.erase(sock);
                            closesocket(sock);
                            FD_CLR(sock, &master_set);
                            continue;
                        }

                        pending.clear();

                        std::cout << "[client " << client_id << "] request: "
                            << request.method << " " << request.path << std::endl;

                        thread_pool.enqueue([sock, request] {
                            std::string request_path = request.path;
                            if (request_path.empty() || request_path == "/") {
                                request_path = "/tetris.html";
                            }

                            std::string relative_path = request_path[0] == '/'
                                ? request_path.substr(1)
                                : request_path;

                            std::ifstream file;
                            const std::string candidates[] = {
                                std::string("http/") + relative_path,
                                relative_path,
                                std::string("../http/") + relative_path,
                                std::string("../../http/") + relative_path,
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
                                    "Connection: " + std::string(request.keep_alive ? "keep-alive" : "close") + "\r\n"
                                    "Content-Length: " + std::to_string(body.size()) + "\r\n"
                                    "\r\n" +
                                    body;
                            }
                            else {
                                const std::string body = "<h1>404 Not Found</h1>";
                                response =
                                    "HTTP/1.1 404 Not Found\r\n"
                                    "Content-Type: text/html; charset=utf-8\r\n"
                                    "Connection: " + std::string(request.keep_alive ? "keep-alive" : "close") + "\r\n"
                                    "Content-Length: " + std::to_string(body.size()) + "\r\n"
                                    "\r\n" +
                                    body;
                            }

                            send(sock, response.c_str(), static_cast<int>(response.size()), 0);
                        });
                    }
                }
            }
        }
    }
};

EventLoop* create_event_loop() {
    return new SelectLoop();
}