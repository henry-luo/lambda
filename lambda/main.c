
#include "transpiler.h"

int main(void) {
    _Static_assert(sizeof(LambdaItem) == 8, "LambdaItem size mismatch");
    Runner runner;  StrBuf *strbuf = strbuf_new_cap(256);  Item ret;

    strbuf_append_str(strbuf, "Test result ===============\n");
    runner_init(&runner);
    ret = run_script_at(&runner, "test/lambda/value.ls");
    strbuf_append_str(strbuf, "Script 'value.ls' result: ");
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
    runner_cleanup(&runner);

    runner_init(&runner);
    ret = run_script_at(&runner, "test/lambda/expr.ls");
    strbuf_append_str(strbuf, "Script 'expr.ls' result: ");
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
    runner_cleanup(&runner);    

    runner_init(&runner);
    ret = run_script_at(&runner, "test/lambda/box_unbox.ls");
    strbuf_append_str(strbuf, "Script 'box_unbox.ls' result: ");
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
    runner_cleanup(&runner);

    runner_init(&runner);
    ret = run_script_at(&runner, "test/lambda/func.ls");
    strbuf_append_str(strbuf, "Script 'func.ls' result: ");
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
    runner_cleanup(&runner);

    printf("%s", strbuf->str);
    strbuf_free(strbuf);
    return 0;
}