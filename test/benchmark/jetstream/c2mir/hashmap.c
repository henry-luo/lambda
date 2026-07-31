/* Native C2MIR port of jetstream/hashmap.ls. */
extern int printf(const char *, ...);

#define COUNT 90000
#define CAPACITY 262144
#define EMPTY -1

static long compute_hash(long key) {
    key ^= key >> 16;
    key *= 73244475;
    key ^= key >> 16;
    return key < 0 ? -key : key;
}

int main(void) {
    static long keys[COUNT];
    static long values[COUNT];
    static long hashes[COUNT];
    static int nexts[COUNT];
    static int heads[CAPACITY];
    long lookup_total = 0;
    long key_total = 0;
    long value_total = 0;
    int i;
    int pass = 1;
    for (i = 0; i < CAPACITY; i++) heads[i] = EMPTY;
    for (i = 0; i < COUNT; i++) {
        long hash = compute_hash(i);
        int bucket = (int) (hash % CAPACITY);
        keys[i] = i; values[i] = 42; hashes[i] = hash; nexts[i] = heads[bucket]; heads[bucket] = i;
    }
    for (i = 0; i < 5; i++) {
        int key;
        for (key = 0; key < COUNT; key++) {
            long hash = compute_hash(key);
            int entry = heads[hash % CAPACITY];
            while (entry != EMPTY && keys[entry] != key) entry = nexts[entry];
            if (entry == EMPTY) { pass = 0; break; }
            lookup_total += values[entry];
        }
    }
    for (i = 0; i < COUNT; i++) { key_total += keys[i]; value_total += values[i]; }
    if (lookup_total != 42L * COUNT * 5 || key_total != (long) COUNT * (COUNT - 1) / 2 || value_total != 42L * COUNT) pass = 0;
    printf(pass ? "hash-map: PASS\n" : "hash-map: FAIL\n");
    return !pass;
}
