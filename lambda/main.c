
#include "transpiler.h"

int main(void) {
    assert(sizeof(LambdaItem) == 8);
    Runner runner;  StrBuf *strbuf = strbuf_new_cap(256);
    runner_init(&runner);

    // run_script(&runner, "test/hello-world.ls");
    Item ret = run_script_at(&runner, "test/lambda/expr.ls");
    print_item(strbuf, ret);
    printf("Returned item: %s\n", strbuf->str);

    strbuf_free(strbuf);
    runner_cleanup(&runner);
    return 0;
}