
#include "transpiler.h"

int main(void) {
    _Static_assert(sizeof(LambdaItem) == 8, "LambdaItem size mismatch");
    Runner runner;  StrBuf *strbuf = strbuf_new_cap(256);  Item ret;

    runner_init(&runner);
    ret = run_script_at(&runner, "test/lambda/value.ls");
    print_item(strbuf, ret);
    printf("Returned item: %s\n", strbuf->str);
    runner_cleanup(&runner);
    strbuf_reset(strbuf);

    runner_init(&runner);
    ret = run_script_at(&runner, "test/lambda/func.ls");
    print_item(strbuf, ret);
    printf("Returned item: %s\n", strbuf->str);
    runner_cleanup(&runner);
    strbuf_free(strbuf);

    return 0;
}