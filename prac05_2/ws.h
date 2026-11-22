/* Общий код WebSocket для сервера и клиента. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pthread.h>

#define MAX_PAYLOAD 4096                    /* лимит сообщения: больше — close 1009 */
enum { OP_TEXT = 1, OP_BIN = 2, OP_CLOSE = 8, OP_PING = 9, OP_PONG = 10 };

static pthread_mutex_t send_m = PTHREAD_MUTEX_INITIALIZER;   /* кадры из разных потоков не смешиваются */

static inline void die(const char *what) {
    fprintf(stderr, "%s: ошибка Winsock %d\n", what, WSAGetLastError());
    exit(1);
}

/* ---------- SHA-1 и base64: готовые алгоритмы, нужны только для Sec-WebSocket-Accept ---------- */
static inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static inline void sha1(const unsigned char *msg, size_t len, unsigned char out[20]) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    size_t total = ((len + 8) / 64 + 1) * 64;
    unsigned char *buf = calloc(total, 1);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    for (int i = 0; i < 8; i++) buf[total - 1 - i] = (unsigned char)(((uint64_t)len * 8) >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)buf[off + 4 * i] << 24) | ((uint32_t)buf[off + 4 * i + 1] << 16) |
                   ((uint32_t)buf[off + 4 * i + 2] << 8) | buf[off + 4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f = i < 20 ? (b & c) | (~b & d) : i < 60 && i >= 40 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
            uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    free(buf);
    for (int i = 0; i < 20; i++) out[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
}

static inline void base64(const unsigned char *in, int len, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= in[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = (i + 1 < len) ? T[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < len) ? T[v & 63] : '=';
    }
    out[o] = '\0';
}

/* Sec-WebSocket-Accept = base64(SHA1(key + GUID)) */
static inline void accept_key(const char *key, char out[32]) {
    char tmp[128];
    unsigned char digest[20];
    snprintf(tmp, sizeof tmp, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    sha1((unsigned char *)tmp, strlen(tmp), digest);
    base64(digest, 20, out);
}

/* ---------- сокеты: TCP — поток, поэтому пишем и читаем «ровно n байт» ---------- */
static inline int send_all(SOCKET s, const unsigned char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, (const char *)buf + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return -1;
        sent += n;
    }
    return 0;
}

static inline int recv_exact(SOCKET s, unsigned char *buf, int n) {
    int got = 0;
    while (got < n) {
        int r = recv(s, (char *)buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += r;
    }
    return 0;
}

/* HTTP-заголовки до пустой строки; читаем по байту, чтобы не захватить первые кадры */
static inline int read_head(SOCKET s, char *buf, int max) {
    int n = 0;
    while (n < max - 1) {
        if (recv(s, buf + n, 1, 0) <= 0) return -1;
        buf[++n] = '\0';
        if (n >= 4 && !memcmp(buf + n - 4, "\r\n\r\n", 4)) return n;
    }
    return -1;
}

/* ---------- кадры ---------- */
struct frame {
    int fin, opcode, masked, len;
    unsigned char payload[MAX_PAYLOAD + 1];      /* уже размаскирован */
};

/* 0 — кадр прочитан; -1 — соединение закрыто; -2 — сообщение больше лимита */
static inline int read_frame(SOCKET s, struct frame *f) {
    unsigned char h[2], ext[2], key[4] = { 0, 0, 0, 0 };
    if (recv_exact(s, h, 2) < 0) return -1;
    f->fin = h[0] >> 7;
    f->opcode = h[0] & 15;
    f->masked = h[1] >> 7;

    int len = h[1] & 127;
    if (len == 127) return -2;                   /* 64-битная длина заведомо больше лимита */
    if (len == 126) {                            /* длина в следующих двух байтах */
        if (recv_exact(s, ext, 2) < 0) return -1;
        len = ext[0] * 256 + ext[1];
    }
    if (len > MAX_PAYLOAD) return -2;

    if (f->masked && recv_exact(s, key, 4) < 0) return -1;
    if (recv_exact(s, f->payload, len) < 0) return -1;
    for (int i = 0; i < len; i++) f->payload[i] ^= key[i % 4];   /* без маски key = 0, ничего не меняется */
    f->payload[len] = '\0';
    f->len = len;
    return 0;
}

/* собрать кадр и отправить одним куском; mask = 1 у клиента, 0 у сервера */
static inline int send_frame(SOCKET s, int opcode, const void *data, int len, int mask) {
    unsigned char buf[MAX_PAYLOAD + 32], key[4] = { 0, 0, 0, 0 };
    int n = 0, m = mask ? 0x80 : 0;
    if (len > MAX_PAYLOAD + 16) return -1;

    buf[n++] = (unsigned char)(0x80 | opcode);   /* FIN=1: сообщение целиком в одном кадре */
    if (len < 126) {
        buf[n++] = (unsigned char)(m | len);
    } else {
        buf[n++] = (unsigned char)(m | 126);
        buf[n++] = (unsigned char)(len >> 8);
        buf[n++] = (unsigned char)(len & 255);
    }
    if (mask) {
        for (int i = 0; i < 4; i++) key[i] = (unsigned char)(rand() & 255);
        memcpy(buf + n, key, 4);
        n += 4;
    }
    for (int i = 0; i < len; i++) buf[n + i] = ((const unsigned char *)data)[i] ^ key[i % 4];
    n += len;

    pthread_mutex_lock(&send_m);
    int rc = send_all(s, buf, n);
    pthread_mutex_unlock(&send_m);
    return rc;
}

static inline int send_close(SOCKET s, int code, int mask) {
    unsigned char p[2] = { (unsigned char)(code >> 8), (unsigned char)(code & 255) };
    return send_frame(s, OP_CLOSE, p, 2, mask);
}
