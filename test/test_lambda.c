#include "../lambda/lambda.h"
#include "../lambda/transpiler.h"

int add(int a, int b) {
    return a + b;
}

char* get_string(int val) {
    return val > 100 ? "great":"not great";
}

int main() {
    LambdaItem item;
    printf("sizeof(LambdaItem) = %zu\n", sizeof(item));
    assert(sizeof(item) == 8);

    // directly shift LMD_TYPE_BOOL without casting to uint64_t first will hang on Mac
    // uint64_t val = (LMD_TYPE_BOOL<<56) | 1;  // this will hang on Mac
    uint64_t val = (((uint64_t)LMD_TYPE_BOOL)<<56) | 1;
    printf("val: %llu\n", val);
    
    int result = add(3, 4);
    printf("3 + 4 = %d\n", result);

    // test returning const string
    char* str = get_string(101);
    printf("Returned string: %s\n", str);
    
    return 0;
}

// zig cc -o test_lambda.exe  test_lambda.c -I../lambda/tree-sitter/lib/include -I/usr/local/include