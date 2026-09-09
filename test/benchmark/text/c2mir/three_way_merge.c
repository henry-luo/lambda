/* Native C2MIR port of text/three_way_merge.ls.
 *
 * Line-level three-way merge with a word-level fallback on lines both sides
 * edited, rebuilt from scratch on every round exactly as the Lambda port does.
 */
extern int printf(const char *, ...);

#define MERGE_ROUNDS 11000
#define LINE_COUNT 768
#define MODULUS 1000000007L
#define MAX_LINE 256
#define MAX_MERGED 262144
#define MAX_WORDS 64
#define MAX_WORD 64

static char base_lines[LINE_COUNT][MAX_LINE];
static char left_lines[LINE_COUNT][MAX_LINE];
static char right_lines[LINE_COUNT][MAX_LINE];
static char merged[MAX_MERGED];

static int str_length(const char *s) {
    int n = 0;
    while (s[n] != 0) n++;
    return n;
}

static int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int append_text(char *out, int at, const char *text) {
    int i = 0;
    while (text[i] != 0) out[at++] = text[i++];
    out[at] = 0;
    return at;
}

static int append_int(char *out, int at, int value) {
    char digits[16];
    int n = 0;
    if (value == 0) {
        out[at++] = '0';
        out[at] = 0;
        return at;
    }
    while (value > 0) {
        digits[n++] = (char) ('0' + value % 10);
        value /= 10;
    }
    while (n > 0) out[at++] = digits[--n];
    out[at] = 0;
    return at;
}

static void build_base(void) {
    int index;
    for (index = 0; index < LINE_COUNT; index++) {
        char *line = base_lines[index];
        int at = append_text(line, 0, "section ");
        at = append_int(line, at, index);
        append_text(line, at, " records the base document with stable words for merging and review");
    }
}

static void make_variant(char target[LINE_COUNT][MAX_LINE], const char *side) {
    int index;
    int is_left = str_equal(side, "left");
    for (index = 0; index < LINE_COUNT; index++) {
        char *line = target[index];
        int at = append_text(line, 0, base_lines[index]);
        if (index % 17 == 0) {
            at = append_text(line, at, " ");
            at = append_text(line, at, side);
            at = append_text(line, at, " edit ");
            at = append_int(line, at, index % 31);
            append_text(line, at, " keeps the paragraph useful");
        } else if (is_left && index % 23 == 0) {
            append_text(line, at, " left-only annotation");
        } else if (!is_left && index % 29 == 0) {
            append_text(line, at, " right-only annotation");
        }
    }
}

/* split on a single space, matching Lambda's split(line, " ") */
static int split_words(const char *line, char words[MAX_WORDS][MAX_WORD]) {
    int count = 0;
    int at = 0;
    int i = 0;
    for (;;) {
        char c = line[i];
        if (c == 0 || c == ' ') {
            words[count][at] = 0;
            count++;
            at = 0;
            if (c == 0) break;
        } else {
            words[count][at++] = c;
        }
        i++;
    }
    return count;
}

static const char *word_at(char words[MAX_WORDS][MAX_WORD], int count, int index) {
    static const char *empty = "";
    if (index < count) return words[index];
    return empty;
}

static int merge_words(const char *base_line, const char *left_line,
                       const char *right_line, char *out, int at) {
    static char base_words[MAX_WORDS][MAX_WORD];
    static char left_words[MAX_WORDS][MAX_WORD];
    static char right_words[MAX_WORDS][MAX_WORD];
    int base_count;
    int left_count;
    int right_count;
    int count;
    int index;
    int written = 0;
    if (str_equal(left_line, right_line)) return append_text(out, at, left_line);
    if (str_equal(left_line, base_line)) return append_text(out, at, right_line);
    if (str_equal(right_line, base_line)) return append_text(out, at, left_line);
    base_count = split_words(base_line, base_words);
    left_count = split_words(left_line, left_words);
    right_count = split_words(right_line, right_words);
    count = base_count;
    if (left_count > count) count = left_count;
    if (right_count > count) count = right_count;
    for (index = 0; index < count; index++) {
        const char *base_word = word_at(base_words, base_count, index);
        const char *left_word = word_at(left_words, left_count, index);
        const char *right_word = word_at(right_words, right_count, index);
        if (str_equal(left_word, right_word)) {
            if (written++) at = append_text(out, at, " ");
            at = append_text(out, at, left_word);
        } else if (str_equal(left_word, base_word)) {
            if (written++) at = append_text(out, at, " ");
            at = append_text(out, at, right_word);
        } else if (str_equal(right_word, base_word)) {
            if (written++) at = append_text(out, at, " ");
            at = append_text(out, at, left_word);
        } else {
            if (written++) at = append_text(out, at, " ");
            at = append_text(out, at, "<<<<<<< LEFT");
            at = append_text(out, at, " ");
            at = append_text(out, at, left_word);
            at = append_text(out, at, " =======");
            at = append_text(out, at, " ");
            at = append_text(out, at, right_word);
            at = append_text(out, at, " >>>>>>> RIGHT");
            written += 4;
        }
    }
    return at;
}

static int merge_lines(void) {
    int index;
    int at = 0;
    for (index = 0; index < LINE_COUNT; index++) {
        const char *base_line = base_lines[index];
        const char *left_line = left_lines[index];
        const char *right_line = right_lines[index];
        if (index > 0) at = append_text(merged, at, "\n");
        if (str_equal(left_line, right_line)) at = append_text(merged, at, left_line);
        else if (str_equal(left_line, base_line)) at = append_text(merged, at, right_line);
        else if (str_equal(right_line, base_line)) at = append_text(merged, at, left_line);
        else at = merge_words(base_line, left_line, right_line, merged, at);
    }
    return at;
}

int main(void) {
    long checksum = 0;
    int round;
    build_base();
    make_variant(left_lines, "left");
    make_variant(right_lines, "right");
    for (round = 0; round < MERGE_ROUNDS; round++) {
        int length = merge_lines();
        int position = (int) (((long) round * 37) % (long) length);
        checksum = (checksum + (long) length * 31 +
                    (long) (unsigned char) merged[position]) % MODULUS;
    }
    printf("three_way_merge: CHECKSUM:%ld\n", checksum);
    (void) str_length;
    return checksum != 342313356;
}
