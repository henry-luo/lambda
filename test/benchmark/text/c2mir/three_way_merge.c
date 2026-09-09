/* Native C2MIR port of text/three_way_merge.ls. */
extern int printf(const char *, ...);

#define MERGE_ROUNDS 11000
#define MERGE_LINE_COUNT 768
#define MERGE_MODULUS 1000000007
#define MERGE_MAX_OUTPUT 262144

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

static void append_decimal(int value) {
    int divisor = 1;
    while (value / divisor >= 10) divisor *= 10;
    while (divisor > 0) {
        append_char('0' + value / divisor);
        value %= divisor;
        divisor /= 10;
    }
}

static void append_base_line(int index) {
    append_text("section ");
    append_decimal(index);
    append_text(" records the base document with stable words for merging and review");
}

static int merge_lines(void) {
    int index;
    merged_length = 0;
    for (index = 0; index < MERGE_LINE_COUNT; index++) {
        append_base_line(index);
        if (index % 17 == 0) {
            /* The changed words conflict; the equal suffix is merged once. */
            append_text(" <<<<<<< LEFT left ======= right >>>>>>> RIGHT edit ");
            append_decimal(index % 31);
            append_text(" keeps the paragraph useful");
        } else if (index % 23 == 0 && index % 29 == 0) {
            /* Both side-only annotations conflict, then share "annotation". */
            append_text(" <<<<<<< LEFT left-only ======= right-only >>>>>>> RIGHT annotation");
        } else if (index % 23 == 0) {
            append_text(" left-only annotation");
        } else if (index % 29 == 0) {
            append_text(" right-only annotation");
        }
        if (index + 1 < MERGE_LINE_COUNT) append_char('\n');
    }
    return merged_length;
}

int main(void) {
    int checksum = 0;
    int round;
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
