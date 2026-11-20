#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pthread.h>

#define PORT 9000

struct client {
    SOCKET fd;
    struct sockaddr_in addr;
};

int active = 0;                                   /* общий счётчик подключений */
pthread_mutex_t stats_m = PTHREAD_MUTEX_INITIALIZER;

static void fail(const char *what) {
    fprintf(stderr, "%s: error Winsock %d\n", what, WSAGetLastError());
}

/* функция потока: обслуживает одного клиента */
void *worker(void *arg) {
    struct client *c = (struct client *)arg;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &c->addr.sin_addr, ip, sizeof ip);
    int port = ntohs(c->addr.sin_port);
    unsigned long tid = (unsigned long)GetCurrentThreadId();

    pthread_mutex_lock(&stats_m);
    active++;
    int now = active;
    pthread_mutex_unlock(&stats_m);
    printf("[thread %lu] connected %s:%d, active threads: %d\n", tid, ip, port, now);

    char msg[64];
    int n = snprintf(msg, sizeof msg, "hello, %s:%d\n", ip, port);
    send(c->fd, msg, n, 0);

    char buf[256];
    for (;;) {
        int r = recv(c->fd, buf, sizeof buf, 0);
        if (r <= 0) break;            /* 0 — клиент закрыл, SOCKET_ERROR — обрыв */
        send(c->fd, buf, r, 0);       /* эхо: вернуть то, что получили */
    }

    closesocket(c->fd);

    pthread_mutex_lock(&stats_m);
    active--;
    now = active;
    pthread_mutex_unlock(&stats_m);
    printf("[thread %lu] disconnected %s:%d, active threads: %d\n", tid, ip, port, now);

    free(c);                          /* память выделил main, освобождает поток */
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* вывод сразу, без буфера */

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { fail("socket"); WSACleanup(); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
        fail("bind"); closesocket(srv); WSACleanup(); return 1;
    }
    if (listen(srv, 16) == SOCKET_ERROR) {
        fail("listen"); closesocket(srv); WSACleanup(); return 1;
    }
    printf("listening port %d (thread on every client), stopping: Ctrl+C\n", PORT);

    for (;;) {
        struct sockaddr_in peer;
        int len = sizeof peer;
        SOCKET cl = accept(srv, (struct sockaddr *)&peer, &len);
        if (cl == INVALID_SOCKET) {
            if (WSAGetLastError() == WSAEINTR) continue;
            fail("accept");
            break;
        }

        struct client *c = malloc(sizeof *c);
        if (c == NULL) { closesocket(cl); continue; }
        c->fd = cl;
        c->addr = peer;

        pthread_t tid;
        if (pthread_create(&tid, NULL, worker, c) == 0) {
            pthread_detach(tid);      /* join не нужен: поток живёт сам */
        } else {
            fprintf(stderr, "pthread_create failed\n");
            closesocket(cl);
            free(c);
        }
    }

    closesocket(srv);
    WSACleanup();
    return 0;
}
