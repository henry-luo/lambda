
#include "transpiler.h"

void run_test_script(Runner *runner, const char *script, StrBuf *strbuf) {
    runner_init(runner);
    char path[256] = "test/lambda/";
    strcat(path, script);
    Item ret = run_script_at(runner, path);
    strbuf_append_format(strbuf, "Script '%s' result: ", script);
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
    _Static_assert(sizeof(Item) == 8, "Item size == 8 bytes");
    _Static_assert(sizeof(LambdaItem) == 8, "LambdaItem size == 8 bytes");
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
    return 0;
}