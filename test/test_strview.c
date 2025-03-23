
#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include "../lib/strview.h"

// String Slice Tests
Test(strview_suite, basic) {
    const char* str = "Hello, World!";
    StrView s = strview_from_str(str);
    
    cr_expect(eq(u64, s.len, strlen(str)));
    cr_expect(eq(i8, strview_get(&s, 0), 'H'));
    cr_expect(eq(i8, strview_get(&s, s.len), '\0'));
}

Test(strview_suite, sub) {
    StrView s = strview_from_str("Hello, World!");
    StrView sub = strview_sub(&s, 7, 12);
    
    cr_expect(eq(u64, sub.len, 5));
    cr_expect(strview_equals(&sub, &strview_from_str("World")));
}

Test(strview_suite, prefix_suffix) {
    StrView s = strview_from_str("Hello, World!");
    
    cr_expect(strview_starts_with(&s, "Hello"));
    cr_expect_not(strview_starts_with(&s, "World"));
    cr_expect(strview_ends_with(&s, "World!"));
    cr_expect_not(strview_ends_with(&s, "Hello"));
}

Test(strview_suite, find) {
    StrView s = strview_from_str("Hello, World!");
    
    cr_expect(eq(i32, strview_find(&s, "World"), 7));
    cr_expect(eq(i32, strview_find(&s, "NotFound"), -1));
    cr_expect(eq(i32, strview_find(&s, ","), 5));
}

Test(strview_suite, trim) {
    StrView s = strview_from_str("  Hello, World!  ");
    strview_trim(&s);
    
    cr_expect(strview_equals(&s, &strview_from_str("Hello, World!")));
    cr_expect(eq(u64, s.len, 13));
}

Test(strview_suite, to_cstr) {
    StrView s = strview_from_str("Hello");
    char* cstr = strview_to_cstr(&s);
    
    cr_expect_not_null(cstr);
    cr_expect(eq(str, cstr, "Hello"));
    free(cstr);
}

int main(int argc, char *argv[]) {
    struct criterion_test_set *tests = criterion_initialize();
    int result = 0;
    
    if (criterion_handle_args(argc, argv, true)) {
        result = !criterion_run_all_tests(tests);
    }
    
    criterion_finalize(tests);
    return result;
}