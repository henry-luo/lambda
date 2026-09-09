/* Native C2MIR port of text/text_search.ls.
 *
 * Naive, KMP and Boyer-Moore substring search over a generated corpus, run as
 * character-code arrays so all three engines compare the same integer work.
 */
extern int printf(const char *, ...);

#define SEARCH_ROUNDS 1536
#define MODULUS 1000000007L
#define CORPUS_ROWS 512
#define PATTERN_COUNT 8
#define MAX_CORPUS 65536
#define MAX_PATTERN 64

static int corpus_codes[MAX_CORPUS];
static int corpus_length = 0;

static int pattern_codes[PATTERN_COUNT][MAX_PATTERN];
static int pattern_lengths[PATTERN_COUNT];

static const char *patterns[PATTERN_COUNT] = {
    "record-0 alpha",
    "record-2048 alpha",
    "token-22 omega",
    "needle-10",
    "omega needle-7",
    "alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3",
    "missing-marker",
    "record-2047 omega"
};

static void push_text(const char *text) {
    int i = 0;
    while (text[i] != 0) {
        corpus_codes[corpus_length++] = (int) (unsigned char) text[i];
        i++;
    }
}

static void push_int(int value) {
    char digits[16];
    int n = 0;
    if (value == 0) {
        corpus_codes[corpus_length++] = '0';
        return;
    }
    while (value > 0) {
        digits[n++] = (char) ('0' + value % 10);
        value /= 10;
    }
    while (n > 0) corpus_codes[corpus_length++] = (int) digits[--n];
}

static void build_corpus(void) {
    int index;
    for (index = 0; index < CORPUS_ROWS; index++) {
        if (index > 0) corpus_codes[corpus_length++] = '\n';
        push_text("record-");
        push_int(index);
        push_text(" alpha aaaaaaaaaaaaaaaaaaaaaaaa token-");
        push_int(index % 23);
        push_text(" omega needle-");
        push_int(index % 11);
    }
}

static void build_patterns(void) {
    int p;
    for (p = 0; p < PATTERN_COUNT; p++) {
        const char *text = patterns[p];
        int n = 0;
        while (text[n] != 0) {
            pattern_codes[p][n] = (int) (unsigned char) text[n];
            n++;
        }
        pattern_lengths[p] = n;
    }
}

static int naive_search(const int *text, int text_len,
                        const int *pattern, int pattern_len, int start) {
    int position;
    if (pattern_len == 0) return start;
    for (position = start; position <= text_len - pattern_len; position++) {
        int offset = 0;
        while (offset < pattern_len && text[position + offset] == pattern[offset]) offset++;
        if (offset == pattern_len) return position;
    }
    return -1;
}

static int prefix_table(const int *pattern, int pattern_len, int *table) {
    int length = 0;
    int index = 1;
    for (index = 0; index < pattern_len; index++) table[index] = 0;
    index = 1;
    while (index < pattern_len) {
        if (pattern[index] == pattern[length]) {
            length++;
            table[index] = length;
            index++;
        } else if (length > 0) {
            length = table[length - 1];
        } else {
            index++;
        }
    }
    return pattern_len;
}

static int kmp_search(const int *text, int text_len,
                      const int *pattern, int pattern_len, int start) {
    static int table[MAX_PATTERN];
    int text_index = start;
    int pattern_index = 0;
    if (pattern_len == 0) return start;
    prefix_table(pattern, pattern_len, table);
    while (text_index < text_len) {
        if (text[text_index] == pattern[pattern_index]) {
            text_index++;
            pattern_index++;
            if (pattern_index == pattern_len) return text_index - pattern_len;
        } else if (pattern_index > 0) {
            pattern_index = table[pattern_index - 1];
        } else {
            text_index++;
        }
    }
    return -1;
}

static int boyer_moore_search(const int *text, int text_len,
                              const int *pattern, int pattern_len, int start) {
    int occurrences[256];
    int index;
    int position;
    if (pattern_len == 0) return start;
    for (index = 0; index < 256; index++) occurrences[index] = -1;
    for (index = 0; index < pattern_len - 1; index++) occurrences[pattern[index]] = index;
    position = start;
    while (position <= text_len - pattern_len) {
        int offset = pattern_len - 1;
        int previous;
        int shift;
        while (offset >= 0 && text[position + offset] == pattern[offset]) offset--;
        if (offset < 0) return position;
        previous = occurrences[text[position + offset]];
        shift = offset - previous;
        position += shift > 1 ? shift : 1;
    }
    return -1;
}

int main(void) {
    long checksum = 0;
    int round;
    build_corpus();
    build_patterns();
    for (round = 0; round < SEARCH_ROUNDS; round++) {
        int index;
        for (index = 0; index < PATTERN_COUNT; index++) {
            int start = (round * 17 + index * 13) % 97;
            int naive = naive_search(corpus_codes, corpus_length,
                                     pattern_codes[index], pattern_lengths[index], start);
            int kmp = kmp_search(corpus_codes, corpus_length,
                                 pattern_codes[index], pattern_lengths[index], start);
            int boyer_moore = boyer_moore_search(corpus_codes, corpus_length,
                                                 pattern_codes[index], pattern_lengths[index], start);
            if (naive != kmp || kmp != boyer_moore) {
                printf("text_search: FAIL algorithm disagreement\n");
                return 1;
            }
            checksum = (checksum + (long) (naive + 2) * (index + 3) + (long) (round + 1) * 7) % MODULUS;
        }
    }
    printf("text_search: CHECKSUM:%ld\n", checksum);
    return checksum != 91395120;
}
