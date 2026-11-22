/* Практика 3: разбор WebSocket-кадра (короткие кадры, длина до 125 байт). */
#include <stdio.h>

static void parse(const char *title, const unsigned char *b, int n) {
    int fin = b[0] >> 7, op = b[0] & 15, mask = b[1] >> 7, len = b[1] & 127;
    int pos = 2;
    const unsigned char *key = b + pos;          /* ключ маски идёт сразу за длиной */
    if (mask) pos += 4;
    (void)n;

    printf("%s\n", title);
    printf("  FIN=%d  OPCODE=0x%X (%s)  MASK=%d  LEN=%d\n", fin, op,
           op == 1 ? "text" : op == 8 ? "close" : op == 9 ? "ping" : op == 10 ? "pong" : "different",
           mask, len);
    if (mask) printf("  MASKING KEY = %02x %02x %02x %02x\n", key[0], key[1], key[2], key[3]);

    printf("  PAYLOAD was get as = ");
    for (int i = 0; i < len; i++) printf("%02x ", b[pos + i]);
    if (op == 1) {
        printf(mask ? "\n  after XOR with key = " : "\n  text              = ");
        for (int i = 0; i < len; i++) printf("%c", mask ? b[pos + i] ^ key[i % 4] : b[pos + i]);
    }
    printf("\n\n");
}

int main(void) {
    unsigned char client_text[] = { 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 };
    unsigned char server_text[] = { 0x81, 0x05, 'H', 'e', 'l', 'l', 'o' };
    unsigned char client_ping[] = { 0x89, 0x80, 0x12, 0x34, 0x56, 0x78 };
    unsigned char server_close[] = { 0x88, 0x02, 0x03, 0xe8 };

    parse("Client sending \"Hello\" (with mask)", client_text, sizeof client_text);
    parse("Server sending \"Hello\" (without mask)", server_text, sizeof server_text);
    parse("Client sending ping", client_ping, sizeof client_ping);
    parse("Server closing connection (code 0x03E8 = 1000)", server_close, sizeof server_close);
    return 0;
}
