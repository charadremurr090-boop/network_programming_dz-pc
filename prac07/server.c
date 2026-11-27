/* server.c - сервер: GetUser (unary) и ListUsers (server stream) */
#include "common.h"
#include <pthread.h>

static const User USERS[] = {
    {1, "Alice", "alice@example.com"}, {2, "Bob",  "bob@example.com"},
    {3, "Carol", "carol@example.com"}, {4, "Dave", "dave@example.com"},
    {5, "Eve",   "eve@example.com"},
};
#define N ((int)(sizeof USERS / sizeof USERS[0]))

static sock_t conn;                                   /* текущий клиент */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; /* чтобы кадры из разных потоков не смешались */

typedef struct { uint32_t stream; int kind, id; } Job;

static void reply(uint32_t stream, int kind, int status, const User *u) {
    uint8_t buf[200];
    int n = u ? encode_user(u, buf) : 0;
    pthread_mutex_lock(&mu);
    send_frame(conn, stream, kind, status, buf, n);
    pthread_mutex_unlock(&mu);
}

static void *worker(void *arg) {                      /* один вызов = один поток */
    Job j = *(Job *)arg; free(arg);
    if (j.kind == K_GET) {
        sleep_ms(300);                                /* имитация работы */
        if (j.id < 1 || j.id > N) reply(j.stream, K_STATUS, NOT_FOUND, NULL);
        else { reply(j.stream, K_DATA, 0, &USERS[j.id - 1]); reply(j.stream, K_STATUS, OK, NULL); }
    } else {                                          /* K_LIST: поток ответов */
        for (int i = 0; i < N; i++) { sleep_ms(200); reply(j.stream, K_DATA, 0, &USERS[i]); }
        reply(j.stream, K_STATUS, OK, NULL);
    }
    return NULL;
}

int main(void) {
    net_init();
    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(PORT);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 1) < 0) { perror("bind"); return 1; }
    printf("сервер слушает 127.0.0.1:%d\n", PORT);

    for (;;) {
        conn = accept(ls, NULL, NULL);
        Hdr h; uint8_t data[256];
        while (recv_frame(conn, &h, data) == 0) {     /* читаем запросы */
            Job *j = malloc(sizeof *j);
            j->stream = h.stream; j->kind = h.kind; j->id = decode_request(data, h.len);
            pthread_t t; pthread_create(&t, NULL, worker, j); pthread_detach(t);
        }
        sock_close(conn);
    }
}
