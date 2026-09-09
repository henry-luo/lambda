/* Native C2MIR port of text/text_search.ls. */
extern int printf(const char *, ...);

#define TEXT_SEARCH_ROWS 512
#define TEXT_SEARCH_ROUNDS 1536
#define TEXT_SEARCH_PATTERN_COUNT 8
#define TEXT_SEARCH_MAX_CORPUS 65536
#define TEXT_SEARCH_MAX_PATTERN 80
#define TEXT_SEARCH_MODULUS 1000000007

static int corpus[TEXT_SEARCH_MAX_CORPUS];
static int corpus_length;

static void append_char(int value) {
    corpus[corpus_length] = value;
    corpus_length++;
}

static void append_text(const char *text) {
    int index = 0;
    while (text[index] != 0) {
        append_char(text[index]);
        index++;
    }
}

static void append_decimal(int value) {
    int divisor = 1;
    while (value / divisor >= 10) divisor *= 10;
    while (divisor > 0) {
        append_char('0' + value / divisor);
        value %= divisor;
        divisor /= 10;
    }
}

static void build_corpus(void) {
    int index;
    corpus_length = 0;
    for (index = 0; index < TEXT_SEARCH_ROWS; index++) {
        append_text("record-");
        append_decimal(index);
        append_text(" alpha aaaaaaaaaaaaaaaaaaaaaaaa token-");
        append_decimal(index % 23);
        append_text(" omega needle-");
        append_decimal(index % 11);
        if (index + 1 < TEXT_SEARCH_ROWS) append_char('\n');
    }
}

static int to_codes(const char *text, int *codes) {
    int length = 0;
    while (text[length] != 0) {
        codes[length] = text[length];
        length++;
    }
    return length;
}

static int naive_search(const int *text, int text_length, const int *pattern,
                        int pattern_length, int start) {
    int position;
    if (pattern_length == 0) return start;
    for (position = start; position <= text_length - pattern_length; position++) {
        int offset = 0;
        while (offset < pattern_length && text[position + offset] == pattern[offset]) {
            offset++;
        }
        if (offset == pattern_length) return position;
    }
    return -1;
}

static int kmp_search(const int *text, int text_length, const int *pattern,
                      int pattern_length, int start) {
    int table[TEXT_SEARCH_MAX_PATTERN];
    int length = 0;
    int index = 1;
    int text_index = start;
    int pattern_index = 0;
    if (pattern_length == 0) return start;
    table[0] = 0;
    while (index < pattern_length) {
        if (pattern[index] == pattern[length]) {
            length++;
            table[index] = length;
            index++;
        } else if (length > 0) {
            length = table[length - 1];
        } else {
            table[index] = 0;
            index++;
        }
    }
    while (text_index < text_length) {
        if (text[text_index] == pattern[pattern_index]) {
            text_index++;
            pattern_index++;
            if (pattern_index == pattern_length) return text_index - pattern_length;
        } else if (pattern_index > 0) {
            pattern_index = table[pattern_index - 1];
        } else {
            text_index++;
        }
    }
    return -1;
}

static int boyer_moore_search(const int *text, int text_length, const int *pattern,
                              int pattern_length, int start) {
    int occurrences[256];
    int index;
    int position;
    if (pattern_length == 0) return start;
    for (index = 0; index < 256; index++) occurrences[index] = -1;
    for (index = 0; index < pattern_length - 1; index++) {
        occurrences[pattern[index]] = index;
    }
    position = start;
    while (position <= text_length - pattern_length) {
        int offset = pattern_length - 1;
        while (offset >= 0 && text[position + offset] == pattern[offset]) offset--;
        if (offset < 0) return position;
        {
            int shift = offset - occurrences[text[position + offset]];
            position += shift > 1 ? shift : 1;
        }
    }
    return -1;
}

int main(void) {
    const char *patterns[TEXT_SEARCH_PATTERN_COUNT] = {
        "record-0 alpha", "record-2048 alpha", "token-22 omega", "needle-10",
        "omega needle-7", "alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3",
        "missing-marker", "record-2047 omega"
    };
    int pattern_codes[TEXT_SEARCH_PATTERN_COUNT][TEXT_SEARCH_MAX_PATTERN];
    int pattern_lengths[TEXT_SEARCH_PATTERN_COUNT];
    int checksum = 0;
    int round;
    int index;

    build_corpus();
    for (index = 0; index < TEXT_SEARCH_PATTERN_COUNT; index++) {
        pattern_lengths[index] = to_codes(patterns[index], pattern_codes[index]);
    }
    for (round = 0; round < TEXT_SEARCH_ROUNDS; round++) {
        for (index = 0; index < TEXT_SEARCH_PATTERN_COUNT; index++) {
            int start = (round * 17 + index * 13) % 97;
            int naive = naive_search(corpus, corpus_length, pattern_codes[index],
                                     pattern_lengths[index], start);
            int kmp = kmp_search(corpus, corpus_length, pattern_codes[index],
                                 pattern_lengths[index], start);
            int boyer_moore = boyer_moore_search(corpus, corpus_length, pattern_codes[index],
                                                 pattern_lengths[index], start);
            if (naive != kmp || kmp != boyer_moore) {
                printf("text_search: FAIL algorithm disagreement\n");
                return 1;
            }
            checksum = (checksum + (naive + 2) * (index + 3) + (round + 1) * 7)
                       % TEXT_SEARCH_MODULUS;
        }
    }
    if (checksum == 91395120) {
        printf("text_search: CHECKSUM:%d\n", checksum);
        return 0;
    }
    printf("text_search: FAIL checksum=%d\n", checksum);
    return 1;
}
