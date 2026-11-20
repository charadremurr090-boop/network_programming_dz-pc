#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define URL "https://jsonplaceholder.typicode.com/users/1"

/* буфер, в который libcurl складывает тело ответа по частям */
struct buf {
    char  *data;
    size_t len;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct buf *b = (struct buf *)userdata;
    size_t n = size * nmemb;

    char *p = realloc(b->data, b->len + n + 1);
    if (p == NULL) return 0;                 /* 0 = ошибка, curl прервёт запрос */
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

/* вывести поле obj[key], если оно строка или число */
static void print_field(const char *label, const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v))
        printf("%-10s %s\n", label, v->valuestring);
    else if (cJSON_IsNumber(v))
        printf("%-10s %g\n", label, v->valuedouble);
}

int main(void) {
    struct buf body = { NULL, 0 };

    CURL *h = curl_easy_init();
    if (h == NULL) return 1;
    curl_easy_setopt(h, CURLOPT_URL, URL);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 5L);        /* не ждать ответ бесконечно */
    /* При ошибке сертификата (SSL certificate problem) на время учебного
       запроса можно раскомментировать строку ниже. */
    /* curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L); */

    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);                            /* освободить handle */

    if (rc != CURLE_OK) {
        fprintf(stderr, "Ошибка запроса: %s\n", curl_easy_strerror(rc));
        return 1;
    }
    printf("GET %s -> %ld\n\n", URL, status);
    if (status != 200) {                             /* код ответа проверяем всегда */
        fprintf(stderr, "Сервер вернул ошибку. Тело ответа:\n%s\n", body.data ? body.data : "");
        free(body.data);
        return 1;
    }

    cJSON *json = cJSON_Parse(body.data);
    free(body.data);
    if (json == NULL) {
        fprintf(stderr, "Не удалось разобрать JSON\n");
        return 1;
    }

    print_field("id", json, "id");
    print_field("name", json, "name");
    print_field("username", json, "username");
    print_field("email", json, "email");
    print_field("phone", json, "phone");
    print_field("city", cJSON_GetObjectItemCaseSensitive(json, "address"), "city");
    print_field("company", cJSON_GetObjectItemCaseSensitive(json, "company"), "name");

    cJSON_Delete(json);
    return 0;
}
