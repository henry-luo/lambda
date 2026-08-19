/* Native C2MIR port of jetstream/crypto_sha1.ls. */
extern int printf(const char *, ...);

#define CHRSZ 8
#define BASE_LEN 629
#define TEXT_LEN 10064 /* base text doubled 4 times */
#define WORDS ((TEXT_LEN * CHRSZ >> 5) + 2)
#define TOTAL_WORDS (WORDS + 20) /* core_sha1 total_len for this input */

static const char base_text[] =
    "Two households, both alike in dignity,\n"
    "In fair Verona, where we lay our scene,\n"
    "From ancient grudge break to new mutiny,\n"
    "Where civil blood makes civil hands unclean.\n"
    "From forth the fatal loins of these two foes\n"
    "A pair of star-cross'd lovers take their life;\n"
    "Whole misadventured piteous overthrows\n"
    "Do with their death bury their parents' strife.\n"
    "The fearful passage of their death-mark'd love,\n"
    "And the continuance of their parents' rage,\n"
    "Which, but their children's end, nought could remove,\n"
    "Is now the two hours' traffic of our stage;\n"
    "The which if you with patient ears attend,\n"
    "What here shall miss, our toil shall strive to mend.";

static const char expected[] = "2524d264def74cce2498bf112bedf00e6c0b796d";

static unsigned rol(unsigned n, int cnt) {
    return (n << cnt) | (n >> (32 - cnt));
}

static unsigned sha1_ft(int t, unsigned b, unsigned c, unsigned d) {
    if (t < 20) return (b & c) | (~b & d);
    if (t < 40) return b ^ c ^ d;
    if (t < 60) return (b & c) | (b & d) | (c & d);
    return b ^ c ^ d;
}

static unsigned sha1_kt(int t) {
    if (t < 20) return 0x5A827999u;
    if (t < 40) return 0x6ED9EBA1u;
    if (t < 60) return 0x8F1BBCDCu;
    return 0xCA62C1D6u;
}

/* Mirrors str2binb + core_sha1 + binb2hex from the .ls: big-endian word
 * packing, in-place padding, and 80-round compression per 16-word block. */
static void hex_sha1(const char *s, int slen, char out[41]) {
    static unsigned bin[WORDS];
    static unsigned x[TOTAL_WORDS];
    unsigned w[80];
    unsigned hash[5];
    const char hex_chars[] = "0123456789abcdef";
    int input_len = slen * CHRSZ;
    int bin_len = (input_len >> 5) + 1;
    int padded_len, len_idx, pad_idx, total_len;
    unsigned a, b, c, d, e;
    int i, j;
    for (i = 0; i < bin_len + 1; i++) bin[i] = 0;
    for (i = 0; i < input_len; i += CHRSZ) {
        unsigned ch = (unsigned char) s[i / CHRSZ];
        bin[i >> 5] |= (ch & 0xFF) << (32 - CHRSZ - (i % 32));
    }
    padded_len = (input_len + 64) >> 9;
    total_len = (padded_len << 4) + 16 + 1;
    if (total_len < bin_len + 1 + 20) total_len = bin_len + 1 + 20;
    for (i = 0; i < total_len; i++) x[i] = i < bin_len + 1 ? bin[i] : 0;
    pad_idx = input_len >> 5;
    x[pad_idx] |= 0x80u << (24 - (input_len % 32));
    len_idx = (padded_len << 4) + 15;
    x[len_idx] = (unsigned) input_len;
    a = 0x67452301u; b = 0xEFCDAB89u; c = 0x98BADCFEu; d = 0x10325476u; e = 0xC3D2E1F0u;
    for (i = 0; i < len_idx + 1; i += 16) {
        unsigned olda = a, oldb = b, oldc = c, oldd = d, olde = e;
        for (j = 0; j < 80; j++) {
            unsigned t;
            if (j < 16) w[j] = x[i + j];
            else w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
            t = rol(a, 5) + sha1_ft(j, b, c, d) + e + w[j] + sha1_kt(j);
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        a += olda; b += oldb; c += oldc; d += oldd; e += olde;
    }
    hash[0] = a; hash[1] = b; hash[2] = c; hash[3] = d; hash[4] = e;
    for (i = 0; i < 20; i++) {
        unsigned byte = (hash[i >> 2] >> ((3 - (i % 4)) * 8)) & 0xFF;
        out[i * 2] = hex_chars[byte >> 4];
        out[i * 2 + 1] = hex_chars[byte & 0xF];
    }
    out[40] = 0;
}

int main(void) {
    static char plain_text[TEXT_LEN + 1];
    char digest[41];
    int len = 0;
    int i, iter;
    int pass = 1;
    while (base_text[len] != 0) { plain_text[len] = base_text[len]; len++; }
    for (i = 0; i < 4; i++) { /* double the text 4 times */
        int k;
        for (k = 0; k < len; k++) plain_text[len + k] = plain_text[k];
        len *= 2;
    }
    for (iter = 0; iter < 25; iter++) { /* JetStream runs 25 iterations */
        hex_sha1(plain_text, len, digest);
        for (i = 0; i < 40; i++) {
            if (digest[i] != expected[i]) { pass = 0; break; }
        }
    }
    printf(pass ? "crypto-sha1: PASS\n" : "crypto-sha1: FAIL\n");
    return !pass;
}
