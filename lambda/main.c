
#include "transpiler.h"
#include <gmp.h>

void run_test_script(Runner *runner, const char *script, StrBuf *strbuf) {
    runner_init(runner);
    char path[256] = "test/lambda/";
    strcat(path, script);
    Item ret = run_script_at(runner, path);
    strbuf_append_format(strbuf, "\nScript '%s' result: ", script);
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
    runner_cleanup(runner);
}

int main(void) {
    _Static_assert(sizeof(bool) == 1, "bool size == 1 byte");
    _Static_assert(sizeof(uint8_t) == 1, "uint8_t size == 1 byte");
    _Static_assert(sizeof(uint16_t) == 2, "uint16_t size == 2 bytes");
    _Static_assert(sizeof(uint32_t) == 4, "uint32_t size == 4 bytes");
    _Static_assert(sizeof(uint64_t) == 8, "uint64_t size == 8 bytes");
    _Static_assert(sizeof(int32_t) == 4, "int32_t size == 4 bytes");
    _Static_assert(sizeof(int64_t) == 8, "int64_t size == 8 bytes");
    _Static_assert(sizeof(Item) == sizeof(double), "Item size == double size");
    _Static_assert(sizeof(LambdaItem) == sizeof(Item), "LambdaItem size == Item size");
    LambdaItem itm = {.item = ITEM_ERROR};
    assert(itm.type_id == LMD_TYPE_ERROR);

    Runner runner;  StrBuf *strbuf = strbuf_new_cap(256);  Item ret;
    strbuf_append_str(strbuf, "Test result ===============\n");
    run_test_script(&runner, "value.ls", strbuf);
    run_test_script(&runner, "expr.ls", strbuf);
    run_test_script(&runner, "box_unbox.ls", strbuf);
    run_test_script(&runner, "func.ls", strbuf);
    run_test_script(&runner, "mem.ls", strbuf);

    printf("%s", strbuf->str);
    strbuf_free(strbuf);

    mpz_t a, b, sum, product;

    // Initialize variables
    mpz_init(a);
    mpz_init(b);
    mpz_init(sum);
    mpz_init(product);
    
    mpz_set_str(a, "123456789123456789123456789", 10);  // base 10
    mpz_set_str(b, "987654321987654321987654321", 10);

    // Compute sum = a + b
    mpz_add(sum, a, b);

    // Compute product = a * b
    mpz_mul(product, a, b);

    // Print results
    gmp_printf("a        = %Zd\n", a);
    gmp_printf("b        = %Zd\n", b);
    gmp_printf("Sum      = %Zd\n", sum);
    gmp_printf("Product  = %Zd\n", product);

    // Clear memory
    mpz_clear(a);
    mpz_clear(b);
    mpz_clear(sum);
    mpz_clear(product);
        
    return 0;
}