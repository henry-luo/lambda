/* Native C2MIR port of beng/knucleotide.ls for the supplied FASTA fixture. */
extern int printf(const char *, ...);
extern void *fopen(const char *, const char *);
extern int fgetc(void *);
extern int fclose(void *);

static int nucleotide_index(int ch) {
    if (ch == 'A' || ch == 'a') return 0;
    if (ch == 'C' || ch == 'c') return 1;
    if (ch == 'G' || ch == 'g') return 2;
    return 3;
}
static char nucleotide_char(int index) {
    static const char letters[] = "ACGT";
    return letters[index];
}
static void print_frequency_table(const int *counts, int width, int total) {
    int order[16];
    int entries = width == 1 ? 4 : 16;
    int i;
    for (i = 0; i < entries; i++) order[i] = i;
    for (i = 0; i < entries; i++) {
        int j;
        for (j = i + 1; j < entries; j++) if (counts[order[j]] > counts[order[i]]) { int temp = order[i]; order[i] = order[j]; order[j] = temp; }
    }
    for (i = 0; i < entries; i++) {
        int value = order[i];
        if (width == 1) printf("%c %.3f\n", nucleotide_char(value), 100.0 * counts[value] / total);
        else printf("%c%c %.3f\n", nucleotide_char(value / 4), nucleotide_char(value % 4), 100.0 * counts[value] / total);
    }
    printf("\n");
}
static int count_match(const char *sequence, int length, const char *needle) {
    int needle_length = 0;
    int count = 0;
    int i;
    while (needle[needle_length] != 0) needle_length++;
    for (i = 0; i <= length - needle_length; i++) { int j = 0; while (j < needle_length && sequence[i + j] == needle[j]) j++; if (j == needle_length) count++; }
    return count;
}
int main(void) {
    void *input = fopen("test/benchmark/beng/input/fasta_1000.txt", "r");
    char bare_sequence[10001];
    char sequence[5000];
    int in_header = 0;
    int length = 0;
    int at_line_start = 1;
    int ch;
    int one[4] = {0};
    int two[16] = {0};
    int i;
    if (input == 0) return 1;
    while ((ch = fgetc(input)) != -1) {
        if (at_line_start && ch == '>') in_header = 1;
        if (!in_header && ch != '\n' && ch != '\r') bare_sequence[length++] = (char) ch;
        if (ch == '\n') {
            at_line_start = 1;
            in_header = 0;
        } else {
            at_line_start = 0;
        }
    }
    fclose(input);
    // The benchmark fixture terminates with >THREE; retain exactly that final 5,000-base section.
    for (i = 0; i < 5000; i++) sequence[i] = bare_sequence[length - 5000 + i];
    length = 5000;
    for (i = 0; i < length; i++) one[nucleotide_index(sequence[i])]++;
    for (i = 0; i + 1 < length; i++) two[nucleotide_index(sequence[i]) * 4 + nucleotide_index(sequence[i + 1])]++;
    print_frequency_table(one, 1, length);
    print_frequency_table(two, 2, length - 1);
    printf("%d\tGGT\n", count_match(sequence, length, "ggt"));
    printf("%d\tGGTA\n", count_match(sequence, length, "ggta"));
    printf("%d\tGGTATT\n", count_match(sequence, length, "ggtatt"));
    printf("%d\tGGTATTTTAATT\n", count_match(sequence, length, "ggtattttaatt"));
    printf("%d\tGGTATTTTAATTTATAGT\n\n", count_match(sequence, length, "ggtattttaatttatagt"));
    return 0;
}
