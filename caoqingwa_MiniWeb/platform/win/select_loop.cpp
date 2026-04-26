#include "event_loop.h"
#include "threadpool.h"
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
#include <mutex>
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

    std::mutex pending_close_mutex;
    std::vector<SOCKET> pending_close_sockets;

private:
    void close_client(SOCKET sock) {
        const bool tracked =
            (client_ids.find(sock) != client_ids.end()) ||
            (recv_buffers.find(sock) != recv_buffers.end());

        if (!tracked) {
            return;
        }

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

    void request_close(SOCKET sock) {
        std::lock_guard<std::mutex> lock(pending_close_mutex);
        pending_close_sockets.push_back(sock);
    }

    void process_pending_close() {
        std::vector<SOCKET> local;
        {
            std::lock_guard<std::mutex> lock(pending_close_mutex);
            local.swap(pending_close_sockets);
        }

        for (SOCKET sock : local) {
            close_client(sock);
        }
    }

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
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval), sizeof(optval)) == SOCKET_ERROR) {
            throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
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
            process_pending_close();

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
                    recv_buffers[client] = "";
                    std::cout << "[client " << client_id << "] connected" << std::endl;
                }
                else {
                    char buf[4096];
                    int len = recv(sock, buf, sizeof(buf), 0);

                    if (len <= 0) {
                        close_client(sock);
                        continue;
                    }

                    auto id_it = client_ids.find(sock);
                    int client_id = (id_it != client_ids.end()) ? id_it->second : -1;

                    std::string& buffered = recv_buffers[sock];
                    buffered.append(buf, buf + len);
                    std::cout << "[client " << client_id << "] recv bytes: " << len << std::endl;

                    HttpConn http_conn;
                    HttpRequest request;
                    HttpParseResult parse_result = http_conn.parse_request(buffered, request);

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
                        close_client(sock);
                        continue;
                    }

                    buffered.clear();

                    thread_pool.enqueue([this, sock, request] {
                        std::string request_path = request.path;
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
                            std::string("../../src/") + relative_path,
                            std::string("../../../src/") + relative_path,
                            std::string("../../../../src/") + relative_path
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

                        int sent = send(sock, response.c_str(), static_cast<int>(response.size()), 0);
                        if (sent <= 0 || !request.keep_alive) {
                            request_close(sock);
                        }
                    });
                }
            }
        }
    }
};

EventLoop* create_event_loop() {
    return new SelectLoop();
}