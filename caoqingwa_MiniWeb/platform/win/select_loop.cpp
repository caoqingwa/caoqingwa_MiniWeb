#include "event_loop.h"
#include "http_handler.h"
#include <winsock2.h>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <stack>
#include <mutex>
#include "threadpool.h"
#include "buffer.h"
#pragma comment(lib, "ws2_32.lib")

namespace {
bool write_all(SOCKET sock, const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        int sent = send(sock, data.data() + total, static_cast<int>(data.size() - total), 0);
        if (sent == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            if (err == WSAEWOULDBLOCK) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        total += static_cast<size_t>(sent);
    }
    return true;
}
}

class SelectLoop : public EventLoop {
private:
    SOCKET server_fd{ INVALID_SOCKET };
    fd_set master_set{};
    ThreadPool thread_pool{ 4 };
    std::unordered_map<SOCKET, int> client_ids;
    std::unordered_map<SOCKET, Buffer> recv_buffers;
    int next_client_id{ 1 };
    std::stack<int> free_client_ids;
    std::mutex state_mutex;
    std::mutex socket_mutex;

    void close_client(SOCKET sock) {
        std::lock(state_mutex, socket_mutex);
        std::lock_guard<std::mutex> state_lock(state_mutex, std::adopt_lock);
        std::lock_guard<std::mutex> socket_lock(socket_mutex, std::adopt_lock);
        auto it = client_ids.find(sock);
        if (it != client_ids.end()) {
            std::cout << "[client " << it->second << "] disconnected" << std::endl;
            free_client_ids.push(it->second);
            client_ids.erase(it);
        }

        recv_buffers.erase(sock);
        closesocket(sock);
        FD_CLR(sock, &master_set);
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

                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        if (client_ids.find(client) != client_ids.end()) {
                            closesocket(client);
                            continue;
                        }
                    }

                    FD_SET(client, &master_set);

                    int client_id;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        if (!free_client_ids.empty()) {
                            client_id = free_client_ids.top();
                            free_client_ids.pop();
                        }
                        else {
                            client_id = next_client_id++;
                        }

                        client_ids[client] = client_id;
                        recv_buffers[client].retrieve_all();
                    }
                    std::cout << "[client " << client_id << "] connected" << std::endl;
                }
                else {
                    char buf[1024];
                    int len = recv(sock, buf, sizeof(buf), 0);

                    if (len <= 0) {
                        close_client(sock);
                    }
                    else {
                        int client_id = -1;
                        {
                            std::lock_guard<std::mutex> lock(state_mutex);
                            auto it = client_ids.find(sock);
                            if (it != client_ids.end()) {
                                client_id = it->second;
                            }
                        }

                        std::string chunk(buf, buf + len);
                        std::cout << "[client " << client_id << "] recv chunk: " << chunk << std::endl;

                        std::string pending_snapshot;
                        {
                            std::lock_guard<std::mutex> lock(state_mutex);
                            auto it = recv_buffers.find(sock);
                            if (it == recv_buffers.end()) {
                                continue;
                            }
                            it->second.append(buf, static_cast<size_t>(len));
                            pending_snapshot.assign(it->second.peek(), it->second.readable_bytes());
                        }

                        HttpConn http_conn;
                        HttpRequest request;
                        HttpParseResult parse_result = http_conn.parse_request(pending_snapshot, request);

                        if (parse_result == HttpParseResult::NeedMoreData) {
                            continue;
                        }

                        if (parse_result == HttpParseResult::BadRequest) {
                            HttpHandler handler;
                            std::string response = handler.build_bad_request_response();
                            {
                                std::lock(state_mutex, socket_mutex);
                                std::lock_guard<std::mutex> state_lock(state_mutex, std::adopt_lock);
                                std::lock_guard<std::mutex> socket_lock(socket_mutex, std::adopt_lock);
                                if (client_ids.find(sock) == client_ids.end()) {
                                    continue;
                                }
                                write_all(sock, response);
                            }
                            close_client(sock);
                            continue;
                        }

                        {
                            std::lock_guard<std::mutex> lock(state_mutex);
                            auto it = recv_buffers.find(sock);
                            if (it != recv_buffers.end()) {
                                it->second.retrieve_all();
                            }
                        }

                        std::cout << "[client " << client_id << "] request: "
                            << request.method << " " << request.path << std::endl;

                        thread_pool.enqueue([this, sock, request] {
                            HttpHandler handler;
                            const std::vector<std::string> roots = {
                                "http",
                                "",
                                "../http",
                                "../../http",
                            };
                            std::string response = handler.build_response(request, roots);

                            {
                                std::lock(state_mutex, socket_mutex);
                                std::lock_guard<std::mutex> state_lock(state_mutex, std::adopt_lock);
                                std::lock_guard<std::mutex> socket_lock(socket_mutex, std::adopt_lock);
                                if (client_ids.find(sock) == client_ids.end()) {
                                    return;
                                }
                                write_all(sock, response);
                            }
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