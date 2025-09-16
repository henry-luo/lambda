#include "lib/unit_test/include/criterion/criterion.h"
#include <stdio.h>

Test(basic_tests, simple_assertion) {
    cr_assert_eq(1 + 1, 2, "Basic math should work");
}

Test(basic_tests, string_test) {
    cr_assert_str_eq("hello", "hello", "Strings should match");
}

Test(basic_tests, pointer_test) {
    int x = 42;
    cr_assert_not_null(&x, "Address should not be null");
}
