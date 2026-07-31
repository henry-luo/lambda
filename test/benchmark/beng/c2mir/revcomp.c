/* Native C2MIR port of beng/revcomp.ls. */
extern int printf(const char *, ...);
extern void *fopen(const char *, const char *);
extern int fgetc(void *);
extern int fclose(void *);
extern int putchar(int);

static int upper_ascii(int ch) { return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch; }
static int complement(int ch) {
    ch = upper_ascii(ch);
    if (ch == 'A') return 'T'; if (ch == 'T') return 'A'; if (ch == 'C') return 'G'; if (ch == 'G') return 'C';
    if (ch == 'M') return 'K'; if (ch == 'K') return 'M'; if (ch == 'R') return 'Y'; if (ch == 'Y') return 'R';
    if (ch == 'V') return 'B'; if (ch == 'B') return 'V'; if (ch == 'H') return 'D'; if (ch == 'D') return 'H';
    return ch;
}
static void emit_sequence(const char *header, const char *sequence, int length) {
    int start;
    int i;
    if (length == 0) return;
    putchar('>');
    for (i = 0; header[i] != 0; i++) putchar(header[i]);
    putchar('\n');
    for (start = length; start > 0; start -= 60) {
        int end = start - 60;
        if (end < 0) end = 0;
        for (i = start - 1; i >= end; i--) putchar(complement(sequence[i]));
        putchar('\n');
    }
}
int main(void) {
    void *input = fopen("test/benchmark/beng/input/fasta_1000.txt", "r");
    char header[256] = {0};
    char sequence[12000];
    int header_length = 0;
    int sequence_length = 0;
    int at_line_start = 1;
    int ch;
    if (input == 0) return 1;
    while ((ch = fgetc(input)) != -1) {
        if (at_line_start && ch == '>') {
            int next;
            emit_sequence(header, sequence, sequence_length);
            header_length = 0; sequence_length = 0;
            while ((next = fgetc(input)) != -1 && next != '\n' && header_length < 255) header[header_length++] = next;
            header[header_length] = 0;
            at_line_start = 1;
            continue;
        }
        if (ch != '\n' && ch != '\r') sequence[sequence_length++] = (char) upper_ascii(ch);
        at_line_start = ch == '\n';
    }
    emit_sequence(header, sequence, sequence_length);
    fclose(input);
    putchar('\n');
    return 0;
}
