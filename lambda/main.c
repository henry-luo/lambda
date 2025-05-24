
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
    _Static_assert(sizeof(LambdaItem) == 8, "LambdaItem size mismatch");
    LambdaItem itm = {.item = ITEM_ERROR};
    assert(itm.type_id == LMD_TYPE_ERROR);

    Runner runner;  StrBuf *strbuf = strbuf_new_cap(256);  Item ret;

    strbuf_append_str(strbuf, "Test result ===============\n");
    run_test_script(&runner, "value.ls", strbuf);
    run_test_script(&runner, "expr.ls", strbuf);
    run_test_script(&runner, "box_unbox.ls", strbuf);
    run_test_script(&runner, "func.ls", strbuf);
    // run_test_script(&runner, "mem.ls", strbuf);

    printf("%s", strbuf->str);
    strbuf_free(strbuf);
    return 0;
}