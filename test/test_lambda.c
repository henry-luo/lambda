#include "../lambda/lambda.h"
#include "../lambda/transpiler.h"

int main() {
    LambdaItem item;
    printf("sizeof(LambdaItem) = %zu\n", sizeof(item));
    assert(sizeof(item) == 8);

    uint64_t val = (((uint64_t)LMD_TYPE_BOOL)<<56) | 1;
    printf("val: %llu\n", val);
    return 0;
}

// zig cc -o test_lambda.exe  test_lambda.c -I../lambda/tree-sitter/lib/include -I/usr/local/include