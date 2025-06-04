#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <string.h>
#include "../../lambda/transpiler.h"
#include "../../lambda/lambda.h"

// Setup and teardown functions
void setup(void) {
    // Runs before each test
}

void teardown(void) {
    // Runs after each test
}

TestSuite(strbuf_tests, .init = setup, .fini = teardown);

void run_test(Runner *runner, char* source, char* expected) {
    StrBuf *strbuf = strbuf_new_cap(256);
    Item ret = run_script(runner, source, "test_value.ls");
    print_item(strbuf, ret);
    cr_assert_str_eq(strbuf->str, expected);
    strbuf_free(strbuf);
    runner_cleanup(runner);
}

Test(strbuf_tests, test_single_value) {
    Runtime runtime;
    runtime_init(&runtime);
    Runner runner;
    runner_init(&runtime, &runner);
    StrBuf *strbuf = strbuf_new_cap(256);

    char* source = "123";
    run_test(&runner, source, source);
}