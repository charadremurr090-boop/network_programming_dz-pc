/* common.h - общее для клиента и сервера */
#ifndef COMMON_H
#define COMMON_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  typedef SOCKET sock_t;
  #define sock_close closesocket
  static void net_init(void) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); SetConsoleOutputCP(CP_UTF8); }
  static void sleep_ms(int ms) { Sleep(ms); }
  static int64_t now_ms(void) { return (int64_t)GetTickCount64(); }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <time.h>
  typedef int sock_t;
  #define sock_close close
  static void net_init(void) {}
  static void sleep_ms(int ms) { usleep(ms * 1000); }
  static int64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }
#endif

#define PORT 50051

/* вид кадра: два метода из .proto + ответ + итоговый статус */
enum { K_GET = 1, K_LIST = 2, K_DATA = 3, K_STATUS = 4 };
enum { OK = 0, NOT_FOUND = 5 };

typedef struct { int id; char name[32]; char email[48]; } User;   /* UserReply */

/* ---- protobuf (упрощённо: числа и строки короче 128 байт) ---- */
/* поле 1 (id): тег 0x08 + число; поле 2 (name): тег 0x12 + длина + байты; поле 3 (email): 0x1A */
static int encode_request(int id, uint8_t *o) {            /* UserRequest */
    o[0] = 0x08; o[1] = (uint8_t)id; return 2;
}
static int decode_request(const uint8_t *b, int n) {
    return n >= 2 ? b[1] : 0;
}
static int encode_user(const User *u, uint8_t *o) {         /* UserReply */
    int n = 0, ln = (int)strlen(u->name), le = (int)strlen(u->email);
    o[n++] = 0x08; o[n++] = (uint8_t)u->id;
    o[n++] = 0x12; o[n++] = (uint8_t)ln; memcpy(o + n, u->name, ln);  n += ln;
    o[n++] = 0x1A; o[n++] = (uint8_t)le; memcpy(o + n, u->email, le); n += le;
    return n;
}
static void decode_user(const uint8_t *b, int n, User *u) {
    memset(u, 0, sizeof *u);
    for (int i = 0; i + 1 < n; ) {
        int tag = b[i], len = b[i + 1];
        if (tag == 0x08)      { u->id = len; i += 2; }
        else if (tag == 0x12) { memcpy(u->name, b + i + 2, len);  i += 2 + len; }
        else if (tag == 0x1A) { memcpy(u->email, b + i + 2, len); i += 2 + len; }
        else break;
    }
}

/* ---- кадр: [stream 4 байта][kind 1][status 1][len 2][данные] ---- */
typedef struct { uint32_t stream; uint8_t kind, status; uint16_t len; } Hdr;

static int send_all(sock_t s, const void *b, int n) {
    const char *p = b;
    while (n > 0) { int k = send(s, p, n, 0); if (k <= 0) return -1; p += k; n -= k; }
    return 0;
}
static int recv_all(sock_t s, void *b, int n) {
    char *p = b;
    while (n > 0) { int k = recv(s, p, n, 0); if (k <= 0) return -1; p += k; n -= k; }
    return 0;
}
static int send_frame(sock_t s, uint32_t stream, int kind, int status, const uint8_t *data, int len) {
    uint8_t buf[8 + 256];
    Hdr h = { htonl(stream), (uint8_t)kind, (uint8_t)status, htons((uint16_t)len) };
    memcpy(buf, &h, 8);
    if (len) memcpy(buf + 8, data, len);
    return send_all(s, buf, 8 + len);
}
static int recv_frame(sock_t s, Hdr *h, uint8_t *data) {
    if (recv_all(s, h, 8) < 0) return -1;
    h->stream = ntohl(h->stream); h->len = ntohs(h->len);
    return h->len ? recv_all(s, data, h->len) : 0;
}
#endif
