#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "wepoll.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 9001
#define MAX_EVENTS 64
#define BUF_SIZE 4096

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    // 1. Создаем слушающий сокет
    SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, SOMAXCONN);

    u_long nonblock = 1;
    ioctlsocket(listen_fd, FIONBIO, &nonblock);

    // 2. Создаем экземпляр epoll (wepoll возвращает HANDLE, но интерфейс как в Linux)
    HANDLE epfd = epoll_create1(0);
    if (epfd == NULL) { 
        fprintf(stderr, "error epoll_create1\n"); 
        return 1; 
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = (int)listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, (int)listen_fd, &ev);

    printf("echo server [epoll/wepoll] listening port %d\n", PORT);

    struct epoll_event events[MAX_EVENTS];

    while (1) {
        // Ждем события (готовые дескрипторы)
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) { 
            fprintf(stderr, "error epoll_wait\n"); 
            break; 
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == (int)listen_fd) {
                // Новое подключение
                SOCKET conn = accept(listen_fd, NULL, NULL);
                if (conn != INVALID_SOCKET) {
                    ioctlsocket(conn, FIONBIO, &nonblock);
                    struct epoll_event cev;
                    cev.events = EPOLLIN;
                    cev.data.fd = (int)conn;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, (int)conn, &cev);
                }
            } else {
                // Данные от клиента
                char buf[BUF_SIZE];
                int r = recv((SOCKET)fd, buf, sizeof(buf), 0);
                if (r > 0) {
                    send((SOCKET)fd, buf, r, 0); // Эхо
                } else if (r == 0 || (r == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                    // Отключаем клиента
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    closesocket((SOCKET)fd);
                }
            }
        }
    }

    epoll_close(epfd);
    closesocket(listen_fd);
    WSACleanup();
    return 0;
}