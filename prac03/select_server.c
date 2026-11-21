#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9000
#define MAX_CLIENTS 64
#define BUF_SIZE 4096

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "error WSAStartup\n");
        return 1;
    }

    SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET) { 
        fprintf(stderr, "error of creating socket\n"); 
        return 1; 
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "error of bind\n"); 
        return 1;
    }
    listen(listen_fd, SOMAXCONN);

    // Делаем слушающий сокет неблокирующим, чтобы accept не зависал
    u_long nonblock = 1;
    ioctlsocket(listen_fd, FIONBIO, &nonblock);

    SOCKET clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i] = INVALID_SOCKET;

    printf("echo-server listening port %d\n", PORT);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);

        SOCKET max_fd = listen_fd;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] != INVALID_SOCKET) {
                FD_SET(clients[i], &readfds);
                if (clients[i] > max_fd) max_fd = clients[i];
            }
        }

        // В Windows первый аргумент select игнорируется, передаем 0
        int ready = select(0, &readfds, NULL, NULL, NULL);
        if (ready == SOCKET_ERROR) {
            fprintf(stderr, "error select: %d\n", WSAGetLastError());
            break;
        }

        // 1. Обрабатываем новые подключения
        if (FD_ISSET(listen_fd, &readfds)) {
            SOCKET conn = accept(listen_fd, NULL, NULL);
            if (conn != INVALID_SOCKET) {
                ioctlsocket(conn, FIONBIO, &nonblock); // Делаем клиентский сокет неблокирующим
                int placed = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i] == INVALID_SOCKET) {
                        clients[i] = conn;
                        placed = 1;
                        break;
                    }
                }
                if (!placed) {
                    closesocket(conn);
                    fprintf(stderr, "client limit reached\n");
                }
            }
        }

        // 2. Обрабатываем данные от уже подключенных клиентов (отправляем эхо)
        for (int i = 0; i < MAX_CLIENTS; i++) {
            SOCKET fd = clients[i];
            if (fd == INVALID_SOCKET || !FD_ISSET(fd, &readfds)) continue;

            char buf[BUF_SIZE];
            int n = recv(fd, buf, sizeof(buf), 0);
            if (n > 0) {
                send(fd, buf, n, 0); // Отправляем обратно (эхо)
            } else if (n == 0 || (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                // Клиент отключился или произошла реальная ошибка
                closesocket(fd);
                clients[i] = INVALID_SOCKET;
            }
        }
    }

    closesocket(listen_fd);
    WSACleanup();
    return 0;
}