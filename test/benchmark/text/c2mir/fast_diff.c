/* Native C2MIR port of text/fast_diff.ls. */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);
extern void free(void *);

static int text_length(const char *text) {
    int n = 0;
    while (text[n] != 0) n++;
    return n;
}

static int diff_score(const char *left, const char *right) {
    int n = text_length(left);
    int m = text_length(right);
    int cols = m + 1;
    int total = (n + 1) * cols;
    int *table = (int *) malloc((unsigned long) total * sizeof(int));
    int i;
    int j;
    int operation = 2;
    int part_length = 0;
    int part_count = 0;
    int score = 0;
    for (i = 0; i < total; i++) table[i] = 0;
    for (i = n - 1; i >= 0; i--) {
        for (j = m - 1; j >= 0; j--) {
            if (left[i] == right[j]) {
                table[i * cols + j] = table[(i + 1) * cols + j + 1] + 1;
            } else {
                int down = table[(i + 1) * cols + j];
                int across = table[i * cols + j + 1];
                table[i * cols + j] = down >= across ? down : across;
            }
        }
    }
    i = 0;
    j = 0;
    while (i < n || j < m) {
        int next_operation;
        if (i < n && j < m && left[i] == right[j]) next_operation = 0;
        else if (j >= m || (i < n && table[(i + 1) * cols + j] >= table[i * cols + j + 1])) next_operation = -1;
        else next_operation = 1;
        if (part_length > 0 && next_operation != operation) {
            score += operation * 31 + part_length;
            part_count++;
            part_length = 0;
        }
        operation = next_operation;
        part_length++;
        if (next_operation == 0) { i++; j++; }
        else if (next_operation < 0) i++;
        else j++;
    }
    if (part_length > 0) {
        score += operation * 31 + part_length;
        part_count++;
    }
    free(table);
    return score + part_count * 17;
}

int main(void) {
    const char *left[6] = {
        "function parse(input) {\n  return input.trim();\n}\n",
        "const config = {\n  retries: 2,\n  timeout: 1000\n};\n",
        "class Renderer {\n  render(node) {\n    return node.text;\n  }\n}\n",
        "import { readFile } from \"fs\";\n\nexport function load(path) {\n  return readFile(path);\n}\n",
        "line one\nline two\nline three\nline four\n",
        "The quick brown fox jumps over the lazy dog.\nThis paragraph is stable.\n"
    };
    const char *right[6] = {
        "function parse(source) {\n  const value = source.trim();\n  return normalize(value);\n}\n",
        "const config = {\n  retries: 3,\n  timeout: 1500,\n  backoff: true\n};\n",
        "class Renderer {\n  render(node) {\n    return escapeHtml(node.text);\n  }\n\n  flush() {\n    return true;\n  }\n}\n",
        "import { readFile } from \"fs/promises\";\n\nexport async function load(path) {\n  const source = await readFile(path, \"utf8\");\n  return source;\n}\n",
        "line zero\nline one\nline 2 changed\nline three\nline four\nline five\n",
        "The quick red fox leaps over the sleepy dog.\nThis paragraph has changed.\n"
    };
    int checksum = 0;
    int round;
    int index;
    for (round = 0; round < 256; round++) {
        for (index = 0; index < 6; index++) checksum += diff_score(left[index], right[index]);
    }
    printf("fast_diff: CHECKSUM:%d\n", checksum);
    return checksum == 0;
}
