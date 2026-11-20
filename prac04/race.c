#include <stdio.h>
#include <pthread.h>

#define N 2000000   /* сколько раз каждый поток увеличивает счётчик */
#define RUNS 3      /* сколько раз повторяем опыт */

volatile long counter = 0;   /* общая переменная */
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

/* без защиты: counter++ это чтение, прибавление и запись */
void *worker_race(void *arg) {
    (void)arg;
    for (long i = 0; i < N; i++) {
        counter++;
    }
    return NULL;
}

/* с мьютексом: внутри секции только один поток */
void *worker_lock(void *arg) {
    (void)arg;
    for (long i = 0; i < N; i++) {
        pthread_mutex_lock(&m);
        counter++;
        pthread_mutex_unlock(&m);
    }
    return NULL;
}

/* запускает два потока с функцией f и возвращает итог */
long run(void *(*f)(void *)) {
    pthread_t t1, t2;
    counter = 0;
    pthread_create(&t1, NULL, f, NULL);
    pthread_create(&t2, NULL, f, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return counter;
}

int main(void) {
    printf("Waiting: %ld (2 thread for %d sum)\n\n", 2L * N, N);
    for (int i = 1; i <= RUNS; i++) {
        long a = run(worker_race);
        printf("starting %d: without mutex = %ld  (lost %ld)\n", i, a, 2L * N - a);
    }
    printf("\n");
    for (int i = 1; i <= RUNS; i++) {
        long b = run(worker_lock);
        printf("starting %d: with mutex  = %ld  (lost %ld)\n", i, b, 2L * N - b);
    }
    return 0;
}
