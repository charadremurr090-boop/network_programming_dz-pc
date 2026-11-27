/* client.c - клиент: unary, streaming, последовательно vs параллельно */
#include "common.h"

static sock_t s;
static int pending;      /* сколько вызовов ещё ждут итогового статуса */
static int64_t t0;

/* отправить запрос и сразу вернуться (не ждём ответа) */
static void start(int stream, int kind, int id) {
    uint8_t buf[4];
    int n = encode_request(id, buf);
    send_frame(s, (uint32_t)stream, kind, 0, buf, n);
    pending++;
}

/* "event loop": принимаем кадры любых вызовов, пока не завершатся все */
static void run(void) {
    while (pending > 0) {
        Hdr h; uint8_t data[256];
        if (recv_frame(s, &h, data) < 0) { printf("соединение закрыто\n"); exit(1); }
        if (h.kind == K_DATA) {
            User u; decode_user(data, h.len, &u);
            printf("   [%4lld мс] вызов %u: %s <%s>\n", (long long)(now_ms() - t0), h.stream, u.name, u.email);
        } else {                                       /* K_STATUS */
            if (h.status != OK) printf("   [%4lld мс] вызов %u: ошибка, статус %d\n", (long long)(now_ms() - t0), h.stream, h.status);
            pending--;
        }
    }
}

int main(void) {
    net_init();
    s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_port = htons(PORT); a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { printf("запустите server\n"); return 1; }

    printf("== Unary ==\n");
    t0 = now_ms(); start(1, K_GET, 2);  run();
    t0 = now_ms(); start(1, K_GET, 99); run();         /* несуществующий id */

    printf("== Server streaming ==\n");
    t0 = now_ms(); start(1, K_LIST, 0); run();

    printf("== Последовательно, 5 вызовов ==\n");
    t0 = now_ms();
    for (int i = 1; i <= 5; i++) { start(i, K_GET, i); run(); }
    printf("   итого: %lld мс\n", (long long)(now_ms() - t0));

    printf("== Параллельно, 5 вызовов ==\n");
    t0 = now_ms();
    for (int i = 1; i <= 5; i++) start(i, K_GET, i);   /* отправили все сразу */
    run();                                             /* ждём все вместе */
    printf("   итого: %lld мс\n", (long long)(now_ms() - t0));

    sock_close(s);
    return 0;
}
