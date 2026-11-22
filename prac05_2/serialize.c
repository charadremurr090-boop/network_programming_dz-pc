/* Практика 1: одна структура в JSON и в protobuf.
   Схема protobuf:  message User { int32 id = 1; string name = 2; repeated string tags = 3; } */
#include <stdio.h>
#include <string.h>

/* строка в protobuf: тег = (номер поля << 3) | 2, потом длина, потом сами байты.
   Упрощение: длины и числа меньше 128, поэтому каждое занимает один байт. */
static int put_string(unsigned char *o, int field, const char *s) {
    int n = (int)strlen(s);
    o[0] = (unsigned char)((field << 3) | 2);
    o[1] = (unsigned char)n;
    memcpy(o + 2, s, n);
    return n + 2;
}

int main(void) {
    int id = 42;
    const char *name = "Ada";
    const char *tags[] = { "cpp", "net" };

    char json[128];
    int nj = snprintf(json, sizeof json, "{\"id\":%d,\"name\":\"%s\",\"tags\":[\"%s\",\"%s\"]}",
                      id, name, tags[0], tags[1]);

    unsigned char pb[128];
    int np = 0;
    pb[np++] = (1 << 3) | 0;                     /* поле 1 (id), тип «число» */
    pb[np++] = (unsigned char)id;                /* значение 42 = 0x2A */
    np += put_string(pb + np, 2, name);          /* поле 2: name */
    np += put_string(pb + np, 3, tags[0]);       /* поле 3: tags, каждый элемент отдельно */
    np += put_string(pb + np, 3, tags[1]);

    printf("JSON     %2d Б  %s\n", nj, json);
    printf("Protobuf %2d Б  ", np);
    for (int i = 0; i < np; i++) printf("%02X ", pb[i]);
    printf("\n\nProtobuf < JSON in %.1f time\n", (double)nj / np);
    return 0;
}
