#include "event_loop.h"
#include <winsock2.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "threadpool.h"
#pragma comment(lib, "ws2_32.lib")

class SelectLoop : public EventLoop {
private:
    SOCKET server_fd;
    fd_set master_set;
    ThreadPool thread_pool{ 4 };
    std::unordered_map<SOCKET, int> client_ids;
    int next_client_id{ 1 };
    std::vector<int> free_client_ids;

public:
    void init(int port) override {
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(2, 2), &wsa))
			throw std::runtime_error("WSAStartup failed");

        server_fd = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        bind(server_fd, (sockaddr*)&addr, sizeof(addr));
		listen(server_fd, 32);//至多32个连接

        FD_ZERO(&master_set);
        FD_SET(server_fd, &master_set);
    }

    void loop() override {
        while (true) {
            fd_set read_set = master_set;
            select(0, &read_set, NULL, NULL, NULL);

            for (int i = 0; i < read_set.fd_count; i++) {
                SOCKET sock = read_set.fd_array[i];

                if (sock == server_fd) {
                    sockaddr_in client_addr{};
                    int addr_len = sizeof(client_addr);
                    SOCKET client = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
                    if (client == INVALID_SOCKET) {
                        continue;
                    }

                    if (client_ids.find(client) != client_ids.end()) {
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
                        closesocket(sock);
                        FD_CLR(sock, &master_set);
                    }
                    else {
                        auto it = client_ids.find(sock);
                        int client_id = (it != client_ids.end()) ? it->second : -1;
                        std::string message(buf, buf + len);
                        std::cout << "[client " << client_id << "] message: " << message << std::endl;

                        thread_pool.enqueue([sock, message] {
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
