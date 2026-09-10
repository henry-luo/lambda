/* Native C2MIR port of text/three_way_merge.ls. */
extern int printf(const char *, ...);

#define MERGE_ROUNDS 11000
#define MERGE_LINE_COUNT 768
#define MERGE_MODULUS 1000000007
#define MERGE_LINE_CAPACITY 160
#define MERGE_MAX_WORDS 24
#define MERGE_MAX_OUTPUT 262144

typedef struct {
    const char *text;
    int length;
} WordSpan;

static char base_lines[MERGE_LINE_COUNT][MERGE_LINE_CAPACITY];
static char left_lines[MERGE_LINE_COUNT][MERGE_LINE_CAPACITY];
static char right_lines[MERGE_LINE_COUNT][MERGE_LINE_CAPACITY];
static char merged[MERGE_MAX_OUTPUT];
static int merged_length;

static void append_char(char value) {
    merged[merged_length] = value;
    merged_length++;
}

static void append_text(const char *text) {
    int index = 0;
    while (text[index] != 0) {
        append_char(text[index]);
        index++;
    }
}

static int line_append_char(char *line, int length, char value) {
    line[length] = value;
    return length + 1;
}

static int line_append_text(char *line, int length, const char *text) {
    int index = 0;
    while (text[index] != 0) {
        length = line_append_char(line, length, text[index]);
        index++;
    }
    return length;
}

static int line_append_decimal(char *line, int length, int value) {
    int divisor = 1;
    while (value / divisor >= 10) divisor *= 10;
    while (divisor > 0) {
        length = line_append_char(line, length, '0' + value / divisor);
        value %= divisor;
        divisor /= 10;
    }
    return length;
}

static int text_length(const char *text) {
    int length = 0;
    while (text[length] != 0) length++;
    return length;
}

static int text_equals(const char *left, const char *right) {
    int index = 0;
    while (left[index] != 0 && right[index] != 0) {
        if (left[index] != right[index]) return 0;
        index++;
    }
    return left[index] == right[index];
}

static void build_base_line(char *line, int index) {
    int length = 0;
    length = line_append_text(line, length, "section ");
    length = line_append_decimal(line, length, index);
    length = line_append_text(line, length,
        " records the base document with stable words for merging and review");
    line[length] = 0;
}

static void make_variant_line(char *line, const char *base, int index,
                              const char *side) {
    int length = 0;
    int base_length = text_length(base);
    int copied;
    for (copied = 0; copied < base_length; copied++) {
        length = line_append_char(line, length, base[copied]);
    }
    if (index % 17 == 0) {
        length = line_append_text(line, length, " ");
        length = line_append_text(line, length, side);
        length = line_append_text(line, length, " edit ");
        length = line_append_decimal(line, length, index % 31);
        length = line_append_text(line, length, " keeps the paragraph useful");
    } else if (side[0] == 'l' && index % 23 == 0) {
        length = line_append_text(line, length, " left-only annotation");
    } else if (side[0] == 'r' && index % 29 == 0) {
        length = line_append_text(line, length, " right-only annotation");
    }
    line[length] = 0;
}

static void build_documents(void) {
    int index;
    for (index = 0; index < MERGE_LINE_COUNT; index++) {
        build_base_line(base_lines[index], index);
        make_variant_line(left_lines[index], base_lines[index], index, "left");
        make_variant_line(right_lines[index], base_lines[index], index, "right");
    }
}

static int split_words(const char *line, WordSpan *words) {
    int count = 0;
    int start;
    int index = 0;
    while (line[index] != 0) {
        start = index;
        while (line[index] != 0 && line[index] != ' ') index++;
        words[count].text = line + start;
        words[count].length = index - start;
        count++;
        if (line[index] == ' ') index++;
    }
    return count;
}

static WordSpan word_at(WordSpan *words, int count, int index) {
    WordSpan empty = {"", 0};
    if (index < count) return words[index];
    return empty;
}

static int words_equal(WordSpan left, WordSpan right) {
    int index;
    if (left.length != right.length) return 0;
    for (index = 0; index < left.length; index++) {
        if (left.text[index] != right.text[index]) return 0;
    }
    return 1;
}

static void append_word(WordSpan word, int *has_word) {
    int index;
    if (*has_word) append_char(' ');
    for (index = 0; index < word.length; index++) append_char(word.text[index]);
    *has_word = 1;
}

static void append_literal_word(const char *text, int *has_word) {
    WordSpan word = {text, text_length(text)};
    append_word(word, has_word);
}

static void merge_words(const char *base_line, const char *left_line,
                        const char *right_line) {
    WordSpan base_words[MERGE_MAX_WORDS];
    WordSpan left_words[MERGE_MAX_WORDS];
    WordSpan right_words[MERGE_MAX_WORDS];
    int base_count = split_words(base_line, base_words);
    int left_count = split_words(left_line, left_words);
    int right_count = split_words(right_line, right_words);
    int count = base_count;
    int index;
    int has_word = 0;
    if (left_count > count) count = left_count;
    if (right_count > count) count = right_count;
    for (index = 0; index < count; index++) {
        WordSpan base_word = word_at(base_words, base_count, index);
        WordSpan left_word = word_at(left_words, left_count, index);
        WordSpan right_word = word_at(right_words, right_count, index);
        if (words_equal(left_word, right_word)) {
            append_word(left_word, &has_word);
        } else if (words_equal(left_word, base_word)) {
            append_word(right_word, &has_word);
        } else if (words_equal(right_word, base_word)) {
            append_word(left_word, &has_word);
        } else {
            append_literal_word("<<<<<<< LEFT", &has_word);
            append_word(left_word, &has_word);
            append_literal_word("=======", &has_word);
            append_word(right_word, &has_word);
            append_literal_word(">>>>>>> RIGHT", &has_word);
        }
    }
}

static int merge_lines(void) {
    int index;
    merged_length = 0;
    for (index = 0; index < MERGE_LINE_COUNT; index++) {
        if (text_equals(left_lines[index], right_lines[index])) {
            append_text(left_lines[index]);
        } else if (text_equals(left_lines[index], base_lines[index])) {
            append_text(right_lines[index]);
        } else if (text_equals(right_lines[index], base_lines[index])) {
            append_text(left_lines[index]);
        } else {
            merge_words(base_lines[index], left_lines[index], right_lines[index]);
        }
        if (index + 1 < MERGE_LINE_COUNT) append_char('\n');
    }
    return merged_length;
}

int main(void) {
    int checksum = 0;
    int round;
    build_documents();
    for (round = 0; round < MERGE_ROUNDS; round++) {
        int length = merge_lines();
        checksum = (checksum + length * 31 + merged[(round * 37) % length]) % MERGE_MODULUS;
    }
    if (checksum == 342313356) {
        printf("three_way_merge: CHECKSUM:%d\n", checksum);
        return 0;
    }
    printf("three_way_merge: FAIL checksum=%d\n", checksum);
    return 1;
}
