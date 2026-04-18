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

        int activity = select(0, &read_set, NULL, NULL, NULL);
        if (activity == SOCKET_ERROR) {
            std::cout << "select error: " << WSAGetLastError() << "\n";
            break;
        }

        // 遍历socket
        for (u_int i = 0; i < read_set.fd_count; ++i) {
            SOCKET sock = read_set.fd_array[i];

            // 新连接
            if (sock == server_fd) {
                SOCKET client = accept(server_fd, nullptr, nullptr);
                if (client == INVALID_SOCKET) {
                    std::cout << "accept error: " << WSAGetLastError() << "\n";
                    continue;
                }

                u_long mode = 1;
                ioctlsocket(client, FIONBIO, &mode);
                FD_SET(client, &master_set);

                std::cout << "New client connected\n";
                continue;
            }

            // 客户端可读
            char buffer[1024] = { 0 };
            int bytes = recv(sock, buffer, sizeof(buffer), 0);

            if (bytes <= 0) {
                // 断开或错误
                closesocket(sock);
                FD_CLR(sock, &master_set);
                std::cout << "Client disconnected\n";
                continue;
            }

            SOCKET client_sock = sock;
            pool.enqueue([client_sock]() {
                const char* body = "<h1>Hello ThreadPool</h1>";
                const char* response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Length: 25\r\n"
                    "Connection: keep-alive\r\n\r\n"
                    "<h1>Hello ThreadPool</h1>";

                send(client_sock, response, (int)strlen(response), 0);
                shutdown(client_sock, SD_BOTH);
                closesocket(client_sock);
            });
        }
    }
    WSACleanup();
    return 0;
}