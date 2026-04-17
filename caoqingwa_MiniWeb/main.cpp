#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <iostream>
#include "threadpool.h"
#pragma comment(lib, "ws2_32.lib")
#define MAX_CLIENTS  FD_SETSIZE

#define port 8080

int main() {
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2, 2), &wsa))
		throw std::runtime_error("WSAStartup failed");
	ThreadPool pool(4);
    
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    std::cout << "Server running at http://localhost:" << port << "\n";

    fd_set master_set, read_set;
    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);

    while (true) {
        read_set = master_set;

        // select监听
        int activity = select(0, &read_set, NULL, NULL, NULL);

        if (activity < 0) {
            std::cout << "select error\n";
            break;
        }

        // 遍历所有socket
        for (int i = 0; i < master_set.fd_count; i++) {
            SOCKET sock = master_set.fd_array[i];

            if (FD_ISSET(sock, &read_set)) {

                // 1️⃣ 新连接
                if (sock == server_fd) {
                    SOCKET client = accept(server_fd, nullptr, nullptr);

                    FD_SET(client, &master_set);

                    std::cout << "New client connected\n";
                }
                // 2️⃣ 客户端发数据
                else {
                    SOCKET client_sock = sock;

                    pool.enqueue([client_sock]() {
                        char buffer[1024] = { 0 };
                        int bytes = recv(client_sock, buffer, 1024, 0);

                        if (bytes <= 0) {
                            closesocket(client_sock);
                            return;
                        }

                        std::cout << "Request:\n" << buffer << std::endl;

                        const char* response =
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/html\r\n\r\n"
                            "<h1>Hello ThreadPool Server</h1>";

                            send(client_sock, response, strlen(response), 0);

                            closesocket(client_sock);
                        });
                }
            }
        }
    }

    WSACleanup();
    return 0;
}