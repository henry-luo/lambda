/* Native C2MIR port of beng/fasta.ls. */
extern int printf(const char *, ...);
extern int putchar(int);

static const char alu[] = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA";
static int seed = 42;

static void repeat_fasta(const char *id, const char *description, const char *source, int count) {
    int position = 0;
    int length = 0;
    int i;
    while (source[length] != 0) length++;
    printf(">%s %s\n", id, description);
    while (count > 0) {
        int line = count < 60 ? count : 60;
        for (i = 0; i < line; i++) { putchar(source[position++]); if (position == length) position = 0; }
        putchar('\n'); count -= line;
    }
}

static void random_fasta(const char *id, const char *description, const char *letters, const double *probabilities, int letter_count, int count) {
    double cumulative[15];
    double total = 0.0;
    int i;
    printf(">%s %s\n", id, description);
    for (i = 0; i < letter_count; i++) { total += probabilities[i]; cumulative[i] = total; }
    while (count > 0) {
        int line = count < 60 ? count : 60;
        for (i = 0; i < line; i++) {
            int choice = 0;
            double value;
            seed = (seed * 3877 + 29573) % 139968;
            value = (double) seed / 139968.0;
            while (choice < letter_count - 1 && cumulative[choice] < value) choice++;
            putchar(letters[choice]);
        }
        putchar('\n'); count -= line;
    }
}

int main(void) {
    static const char iub[] = "acgtBDHKMNRSVWY";
    static const double iub_probabilities[15] = {.27,.12,.12,.27,.02,.02,.02,.02,.02,.02,.02,.02,.02,.02,.02};
    static const char hs[] = "acgt";
    static const double hs_probabilities[4] = {.3029549426680,.1979883004921,.1975473066391,.3015094502008};
    repeat_fasta("ONE", "Homo sapiens alu", alu, 2000);
    random_fasta("TWO", "IUB ambiguity codes", iub, iub_probabilities, 15, 3000);
    random_fasta("THREE", "Homo sapiens frequency", hs, hs_probabilities, 4, 5000);
    // preserve the benchmark fixture's trailing line after timing is stripped.
    putchar('\n');
    return 0;
}
