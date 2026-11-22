/* Практики 2 и 4: WebSocket-сервер, поток на каждого клиента.
   ws_server.exe        — echo: вернуть сообщение отправителю
   ws_server.exe chat   — чат: разослать сообщение остальным клиентам */
#include "ws.h"

#define PORT        9001
#define MAX_CLIENTS 16

static SOCKET clients[MAX_CLIENTS];     /* INVALID_SOCKET = свободно; номер клиента = индекс + 1 */
static pthread_mutex_t clients_m = PTHREAD_MUTEX_INITIALIZER;
static int chat_mode = 0;

/* отправка идёт под мьютексом — упрощение: медленный клиент задержит остальных */
static void deliver(int id, int opcode, const unsigned char *data, int len) {
    unsigned char msg[MAX_PAYLOAD + 16];
    int n = 0;
    if (chat_mode && opcode == OP_TEXT) n = snprintf((char *)msg, 16, "[#%d] ", id);
    memcpy(msg + n, data, len);
    n += len;

    pthread_mutex_lock(&clients_m);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == INVALID_SOCKET) continue;
        int is_sender = (i + 1 == id);
        if (chat_mode ? is_sender : !is_sender) continue;    /* chat — всем, кроме автора; echo — только автору */
        send_frame(clients[i], opcode, msg, n, 0);           /* сервер кадры не маскирует */
    }
    pthread_mutex_unlock(&clients_m);
}

/* рукопожатие: обычный HTTP-запрос с Upgrade, отвечаем 101 */
static int handshake(SOCKET fd) {
    char head[2048], key[32], acc[32], resp[256];
    if (read_head(fd, head, sizeof head) < 0) return -1;
    char *k = strstr(head, "Sec-WebSocket-Key: ");
    if (k == NULL || sscanf(k + 19, "%31s", key) != 1) return -1;

    accept_key(key, acc);
    int n = snprintf(resp, sizeof resp,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", acc);
    return send_all(fd, (unsigned char *)resp, n);
}

static void *worker(void *arg) {
    int slot = (int)(intptr_t)arg;
    int id = slot + 1;
    SOCKET fd = clients[slot];

    if (handshake(fd) == 0) {
        printf("[#%d] connected\n", id);
        for (;;) {                                           /* после 101 — только кадры, не HTTP */
            struct frame f;
            int rc = read_frame(fd, &f);
            if (rc == -2) { send_close(fd, 1009, 0); printf("[#%d] too big msg -> close 1009\n", id); break; }
            if (rc < 0) break;                               /* соединение оборвано */

            if (!f.masked) {                                 /* клиент обязан маскировать кадры */
                send_close(fd, 1002, 0);
                printf("[#%d] frame without mask -> close 1002\n", id);
                break;
            }
            if (f.opcode == OP_PING) {
                send_frame(fd, OP_PONG, f.payload, f.len, 0);
                printf("[#%d] ping -> pong\n", id);
            } else if (f.opcode == OP_CLOSE) {
                send_close(fd, 1000, 0);
                printf("[#%d] close\n", id);
                break;
            } else if ((f.opcode == OP_TEXT || f.opcode == OP_BIN) && f.fin) {
                printf("[#%d] msg (%d Б): %.60s\n", id, f.len, f.payload);
                deliver(id, f.opcode, f.payload, f.len);
            } else if (f.opcode != OP_PONG) {
                send_close(fd, 1003, 0);                     /* фрагментацию (FIN=0) не поддерживаем */
                printf("[#%d] fragments doesn't support -> close 1003\n", id);
                break;
            }
        }
        printf("[#%d] disconnected\n", id);
    }

    pthread_mutex_lock(&clients_m);
    clients[slot] = INVALID_SOCKET;
    pthread_mutex_unlock(&clients_m);
    closesocket(fd);
    return NULL;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    chat_mode = (argc > 1 && strcmp(argv[1], "chat") == 0);
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i] = INVALID_SOCKET;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) die("WSAStartup");
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) die("socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) die("bind");
    if (listen(srv, 16) == SOCKET_ERROR) die("listen");
    printf("WebSocket-server, mode %s, port %d, stopping: Ctrl+C\n", chat_mode ? "chat" : "echo", PORT);

    for (;;) {
        SOCKET cl = accept(srv, NULL, NULL);
        if (cl == INVALID_SOCKET) die("accept");

        int slot = -1;                                       /* ищем свободную ячейку */
        pthread_mutex_lock(&clients_m);
        for (int i = 0; i < MAX_CLIENTS && slot < 0; i++)
            if (clients[i] == INVALID_SOCKET) { clients[i] = cl; slot = i; }
        pthread_mutex_unlock(&clients_m);

        pthread_t t;
        if (slot < 0 || pthread_create(&t, NULL, worker, (void *)(intptr_t)slot) != 0) {
            if (slot >= 0) clients[slot] = INVALID_SOCKET;
            closesocket(cl);
            continue;
        }
        pthread_detach(t);
    }
}
