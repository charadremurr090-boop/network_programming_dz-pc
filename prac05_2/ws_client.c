/* Практики 2 и 4: WebSocket-клиент.  ws_client.exe [IP] [порт]
   Текст + Enter — отправить, /ping — проверить канал, /quit — закрыть соединение. */
#include "ws.h"
#include <time.h>

static SOCKET sock;
static volatile int closing = 0;

/* поток-приёмник: сообщения от сервера приходят в любой момент */
static void *receiver(void *arg) {
    (void)arg;
    for (;;) {
        struct frame f;
        if (read_frame(sock, &f) < 0) { printf("соединение закрыто\n"); exit(0); }

        if (f.opcode == OP_TEXT) {
            printf("< %s\n", f.payload);
        } else if (f.opcode == OP_PING) {
            send_frame(sock, OP_PONG, f.payload, f.len, 1);      /* на ping отвечаем pong */
            printf("< ping (sending pong)\n");
        } else if (f.opcode == OP_PONG) {
            printf("< pong\n");
        } else if (f.opcode == OP_CLOSE) {
            if (!closing) send_close(sock, 1000, 1);             /* ответный close, если закрывал сервер */
            printf("server closed connection, code %d\n", f.len >= 2 ? f.payload[0] * 256 + f.payload[1] : 0);
            exit(0);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *ip = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 9001;
    setvbuf(stdout, NULL, _IONBF, 0);
    srand((unsigned)time(NULL));

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) die("WSAStartup");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) die("socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(sock, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) die("connect");

    /* рукопожатие: ключ — 16 случайных байт в base64 */
    unsigned char raw[16];
    char key[32], expected[32], req[512], head[2048], *got;
    for (int i = 0; i < 16; i++) raw[i] = (unsigned char)(rand() & 255);
    base64(raw, 16, key);
    int n = snprintf(req, sizeof req,
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n", ip, port, key);
    send_all(sock, (unsigned char *)req, n);

    accept_key(key, expected);                                   /* что должен прислать сервер */
    got = NULL;
    if (read_head(sock, head, sizeof head) > 0 && strncmp(head, "HTTP/1.1 101", 12) == 0)
        got = strstr(head, expected);
    if (got == NULL) { fprintf(stderr, "handshake was not succesful:\n%s\n", head); return 1; }
    printf("Connected (101 Switching Protocols). Text + Enter, /ping, /quit.\n");

    pthread_t rx;
    pthread_create(&rx, NULL, receiver, NULL);

    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;
        if (strcmp(line, "/quit") == 0) break;
        if (strcmp(line, "/ping") == 0) send_frame(sock, OP_PING, "hi", 2, 1);
        else                            send_frame(sock, OP_TEXT, line, (int)strlen(line), 1);   /* клиент маскирует */
    }

    closing = 1;
    send_close(sock, 1000, 1);        /* штатное закрытие */
    Sleep(1000);                      /* ждём ответный close от сервера */
    closesocket(sock);
    WSACleanup();
    return 0;
}
