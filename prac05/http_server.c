#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define PORT    8080
#define ROOT    "www"        /* папка с файлами рядом с программой */
#define REQ_MAX 2048

static void die(const char *what) {
    fprintf(stderr, "%s: ошибка Winsock %d\n", what, WSAGetLastError());
    exit(1);
}

/* send() может отправить меньше, чем просили, поэтому досылаем циклом */
static void send_all(SOCKET s, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return;
        sent += n;
    }
}

/* Content-Type по расширению файла */
static const char *content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot && !strcmp(dot, ".txt"))  return "text/plain; charset=utf-8";
    if (dot && !strcmp(dot, ".json")) return "application/json";
    return "application/octet-stream";
}

/* ответ целиком: статус, заголовки, тело */
static void respond(SOCKET s, int code, const char *reason, const char *type,
                    const char *body, int len, const char *extra) {
    char head[256];
    int n = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        code, reason, type, len, extra);
    send_all(s, head, n);
    send_all(s, body, len);
}

/* ответ-ошибка с коротким текстом */
static int send_error(SOCKET s, int code, const char *reason, const char *extra) {
    char body[64];
    int n = snprintf(body, sizeof body, "%d %s\n", code, reason);
    respond(s, code, reason, "text/plain", body, n, extra);
    return code;
}

/* отдать файл из ROOT; возвращает код ответа */
static int serve(SOCKET s, const char *url) {
    size_t n = strlen(url);
    if (strstr(url, "..")) return send_error(s, 400, "Bad Request", "");
    if (n == 0 || url[n - 1] == '/') return send_error(s, 404, "Not Found", "");

    char path[300];
    snprintf(path, sizeof path, "%s%s", ROOT, url);

    FILE *f = fopen(path, "rb");
    if (f == NULL) return send_error(s, 404, "Not Found", "");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *data = malloc(size > 0 ? size : 1);
    size_t got = data ? fread(data, 1, size, f) : 0;
    fclose(f);
    if (data == NULL) return send_error(s, 500, "Internal Server Error", "");

    respond(s, 200, "OK", content_type(path), data, (int)got, "");
    free(data);
    return 200;
}

/* прочитать запрос, ответить */
static void handle(SOCKET cl, const char *ip, int port) {
    char req[REQ_MAX];
    int total = 0;
    req[0] = '\0';

    /* читаем, пока не придёт пустая строка после заголовков */
    while (!strstr(req, "\r\n\r\n")) {
        int r = recv(cl, req + total, REQ_MAX - 1 - total, 0);
        if (r <= 0) return;                  /* клиент закрыл соединение или ошибка */
        total += r;
        req[total] = '\0';
        if (total == REQ_MAX - 1) break;     /* слишком длинный запрос */
    }

    char method[8] = "", url[256] = "";
    int code;
    if (sscanf(req, "%7s %255s", method, url) != 2)
        code = send_error(cl, 400, "Bad Request", "");
    else if (strcmp(method, "GET") != 0)
        code = send_error(cl, 405, "Method Not Allowed", "Allow: GET\r\n");
    else
        code = serve(cl, url);

    printf("[%s:%d] %s %s -> %d\n", ip, port, method, url, code);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

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
    printf("listening port %d, files from folder %s, stopping: Ctrl+C\n", PORT, ROOT);

    for (;;) {                               /* клиенты обслуживаются по очереди */
        struct sockaddr_in peer;
        int len = sizeof peer;
        SOCKET cl = accept(srv, (struct sockaddr *)&peer, &len);
        if (cl == INVALID_SOCKET) die("accept");

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        handle(cl, ip, ntohs(peer.sin_port));
        closesocket(cl);
    }
}
