/* Native C2MIR port of awfy/storage2.ls.  Each node allocation is retained for the run. */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);

static int next_random(int *seed) {
    *seed = (*seed * 1309 + 13849) % 65536;
    return *seed;
}

typedef struct Branch {
    void *children[4];
} Branch;

static void *build_tree_depth(int depth, int *seed, int *count) {
    int i;
    (*count)++;
    if (depth == 1) {
        int size = next_random(seed) % 10 + 1;
        return malloc((unsigned long) size * sizeof(int));
    }
    {
        Branch *node = (Branch *) malloc(sizeof(Branch));
        if (node == 0) return 0;
        for (i = 0; i < 4; i++) node->children[i] = build_tree_depth(depth - 1, seed, count);
        return node;
    }
}

int main(void) {
    int seed = 74755;
    int count = 0;
    build_tree_depth(7, &seed, &count);
    printf(count == 5461 ? "Storage: PASS\n" : "Storage: FAIL result=%d\n", count);
    return count != 5461;
}
