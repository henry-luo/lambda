/* Native C2MIR port of beng/regexredux.ls without a host regex dependency. */
extern int printf(const char *, ...);
extern void *fopen(const char *, const char *);
extern int fgetc(void *);
extern int fclose(void *);
static int lower_ascii(int ch) { return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch; }
static int literal_at(const char *text, int length, int at, const char *literal) { int i = 0; while (literal[i] != 0) { if (at + i >= length || text[at + i] != literal[i]) return 0; i++; } return i; }
static int in_class(char ch, const char *choices) { int i = 0; while (choices[i] != 0) { if (ch == choices[i]) return 1; i++; } return 0; }
static int match_pattern(const char *text, int length, int at, int pattern) {
    int n;
    if (pattern == 0) return (n = literal_at(text,length,at,"agggtaaa")) ? n : literal_at(text,length,at,"tttaccct");
    if (pattern == 1) return at + 8 <= length && ((in_class(text[at],"cgt") && literal_at(text,length,at+1,"gggtaaa")) || (literal_at(text,length,at,"tttaccc") && in_class(text[at+7],"acg"))) ? 8 : 0;
    if (pattern == 2) return at + 8 <= length && ((text[at]=='a' && in_class(text[at+1],"act") && literal_at(text,length,at+2,"ggtaaa")) || (literal_at(text,length,at,"tttacc") && in_class(text[at+6],"agt") && text[at+7]=='t')) ? 8 : 0;
    if (pattern == 3) return at + 8 <= length && ((literal_at(text,length,at,"ag") && in_class(text[at+2],"act") && literal_at(text,length,at+3,"gtaaa")) || (literal_at(text,length,at,"tttac") && in_class(text[at+5],"agt") && literal_at(text,length,at+6,"ct"))) ? 8 : 0;
    if (pattern == 4) return at + 8 <= length && ((literal_at(text,length,at,"agg") && in_class(text[at+3],"act") && literal_at(text,length,at+4,"taaa")) || (literal_at(text,length,at,"ttta") && in_class(text[at+4],"agt") && literal_at(text,length,at+5,"cct"))) ? 8 : 0;
    if (pattern == 5) return at + 8 <= length && ((literal_at(text,length,at,"aggg") && in_class(text[at+4],"acg") && literal_at(text,length,at+5,"aaa")) || (literal_at(text,length,at,"ttt") && in_class(text[at+3],"cgt") && literal_at(text,length,at+4,"ccct"))) ? 8 : 0;
    if (pattern == 6) return at + 8 <= length && ((literal_at(text,length,at,"agggt") && in_class(text[at+5],"cgt") && literal_at(text,length,at+6,"aa")) || (literal_at(text,length,at,"tt") && in_class(text[at+2],"acg") && literal_at(text,length,at+3,"taccct"))) ? 8 : 0;
    if (pattern == 7) return at + 8 <= length && ((literal_at(text,length,at,"agggta") && in_class(text[at+6],"cgt") && text[at+7]=='a') || (text[at]=='t' && in_class(text[at+1],"acg") && literal_at(text,length,at+2,"ataccct"))) ? 8 : 0;
    return at + 8 <= length && ((literal_at(text,length,at,"agggtaa") && in_class(text[at+7],"cgt")) || (in_class(text[at],"acg") && literal_at(text,length,at+1,"aataccct"))) ? 8 : 0;
}
int main(void) {
    void *input = fopen("test/benchmark/beng/input/fasta_1000.txt", "r");
    char sequence[10001]; int sequence_length = 0; int original_length = 0; int at_line_start = 1; int in_header = 0; int ch; int pattern;
    if (input == 0) return 1;
    while ((ch = fgetc(input)) != -1) { original_length++; if (at_line_start && ch == '>') in_header = 1; if (!in_header && ch != '\n' && ch != '\r') sequence[sequence_length++] = (char) lower_ascii(ch); if (ch == '\n') { at_line_start = 1; in_header = 0; } else at_line_start = 0; }
    fclose(input); sequence[sequence_length] = 0;
    for (pattern = 0; pattern < 9; pattern++) { int i = 0; int count = 0; while (i < sequence_length) { int width = match_pattern(sequence, sequence_length, i, pattern); if (width) { count++; i += width; } else i++; } if (pattern == 0) printf("agggtaaa|tttaccct %d\n",count); else if (pattern == 1) printf("[cgt]gggtaaa|tttaccc[acg] %d\n",count); else if (pattern == 2) printf("a[act]ggtaaa|tttacc[agt]t %d\n",count); else if (pattern == 3) printf("ag[act]gtaaa|tttac[agt]ct %d\n",count); else if (pattern == 4) printf("agg[act]taaa|ttta[agt]cct %d\n",count); else if (pattern == 5) printf("aggg[acg]aaa|ttt[cgt]ccct %d\n",count); else if (pattern == 6) printf("agggt[cgt]aa|tt[acg]taccct %d\n",count); else if (pattern == 7) printf("agggta[cgt]a|t[acg]ataccct %d\n",count); else printf("agggtaa[cgt]|[acg]aataccct %d\n",count); }
    { int i; int expanded_length = 0; for (i = 0; i < sequence_length; i++) { char c = sequence[i]; expanded_length += c=='b'||c=='d'||c=='h'||c=='v' ? 7 : c=='n' ? 9 : c=='k'||c=='m'||c=='r'||c=='s'||c=='w'||c=='y' ? 5 : 1; } printf("\n%d\n%d\n%d\n", original_length, sequence_length, expanded_length); }
    return 0;
}
