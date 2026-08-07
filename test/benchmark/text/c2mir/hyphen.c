/* Native C2MIR port of text/hyphen.ls. */
extern int printf(const char *, ...);

static int is_letter(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static int is_vowel(char ch) {
    return ch == 'A' || ch == 'E' || ch == 'I' || ch == 'O' || ch == 'U' ||
           ch == 'a' || ch == 'e' || ch == 'i' || ch == 'o' || ch == 'u';
}

static int result_length(const char *text) {
    int length = 0;
    int i;
    int in_tag = 0;
    for (i = 0; text[i] != 0; i++) {
        char ch = text[i];
        if (ch == '<') in_tag = 1;
        else if (ch == '>') in_tag = 0;
        length++;
        if (!in_tag && i > 0 && text[i + 1] != 0 && text[i + 2] != 0 &&
            is_letter(text[i - 1]) && is_letter(ch) && is_vowel(ch) &&
            is_letter(text[i + 1]) && is_letter(text[i + 2]) &&
            !is_vowel(text[i + 1])) length++;
    }
    return length;
}

static int result_char_at(const char *text, int target) {
    int position = 0;
    int i;
    int in_tag = 0;
    for (i = 0; text[i] != 0; i++) {
        char ch = text[i];
        int insert_hyphen;
        if (ch == '<') in_tag = 1;
        else if (ch == '>') in_tag = 0;
        insert_hyphen = !in_tag && i > 0 && text[i + 1] != 0 && text[i + 2] != 0 &&
            is_letter(text[i - 1]) && is_letter(ch) && is_vowel(ch) &&
            is_letter(text[i + 1]) && is_letter(text[i + 2]) &&
            !is_vowel(text[i + 1]);
        if (position == target) return (unsigned char) ch;
        position++;
        if (insert_hyphen) {
            if (position == target) return '-';
            position++;
        }
    }
    return 0;
}

int main(void) {
    const char *texts[6] = {
        "A certain king had a beautiful garden, and every morning he walked through it to admire the flowers.",
        "The tortoise never stopped for a moment, walking slowly but steadily right to the end of the course.",
        "A compiler transforms structured source text into executable instructions while preserving useful diagnostics.",
        "Text processing includes punctuation, capitalization, multiline paragraphs, and carefully selected exceptions.",
        "<article><h1>Hyphenation benchmark</h1><p>Beautiful documents require readable typography and consistent line breaking.</p></article>",
        "The algorithm combines a pattern trie with exception handling and a configurable hyphenation character."
    };
    int checksum = 0;
    int round;
    int index;
    for (round = 0; round < 32; round++) {
        for (index = 0; index < 6; index++) {
            int length = result_length(texts[index]);
            checksum += length * 29;
            if (length > 0) checksum += result_char_at(texts[index], index % length);
        }
    }
    printf("hyphen: CHECKSUM:%d\n", checksum);
    return checksum == 0;
}
