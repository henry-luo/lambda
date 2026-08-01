/* Native C2MIR port of kostya/base64.ls. */
extern int printf(const char *, ...);

static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int encode(const unsigned char input[10000], char output[13337]) {
    int i = 0;
    int out = 0;
    while (i + 2 < 10000) {
        unsigned int value = ((unsigned int) input[i] << 16) | ((unsigned int) input[i + 1] << 8) | input[i + 2];
        output[out++] = table[(value >> 18) & 63]; output[out++] = table[(value >> 12) & 63];
        output[out++] = table[(value >> 6) & 63]; output[out++] = table[value & 63];
        i += 3;
    }
    if (i + 1 == 10000) {
        output[out++] = table[input[i] >> 2]; output[out++] = table[(input[i] & 3) << 4];
        output[out++] = '='; output[out++] = '=';
    } else if (i + 2 == 10000) {
        output[out++] = table[input[i] >> 2]; output[out++] = table[((input[i] & 3) << 4) | (input[i + 1] >> 4)];
        output[out++] = table[(input[i + 1] & 15) << 2]; output[out++] = '=';
    }
    output[out] = 0;
    return out;
}

int main(void) {
    unsigned char input[10000];
    char encoded[13337];
    int i;
    int encoded_len = 0;
    for (i = 0; i < 10000; i++) input[i] = 'a';
    for (i = 0; i < 100; i++) encoded_len = encode(input, encoded);
    printf(encoded_len == 13336 ? "base64: encoded_len=13336 decoded_len=10000\nbase64: PASS\n" : "base64: FAIL\n");
    return encoded_len != 13336;
}
