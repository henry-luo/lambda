#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include "../lib/strview.h"

// String Slice Tests
Test(strview_suite, basic) {
    const char* str = "Hello, World!";
    StrView s = strview_from_str(str);
    
    cr_expect(eq(u64, s.length, strlen(str)));
    cr_expect(eq(i8, strview_get(&s, 0), 'H'));
    cr_expect(eq(i8, strview_get(&s, s.length), '\0')); // Out of bounds should return '\0'
    cr_expect(eq(i8, strview_get(&s, s.length - 1), '!')); // Last character
}

Test(strview_suite, sub) {
    StrView s = strview_from_str("Hello, World!");
    StrView sub = strview_sub(&s, 7, 12);
    
    cr_expect(eq(u64, sub.length, 5));
    cr_expect(strview_eq(&sub, &strview_from_str("World")));
}

Test(strview_suite, sub_edge_cases) {
    StrView s = strview_from_str("Hello");
    
    // Valid substring
    StrView sub1 = strview_sub(&s, 1, 4);
    cr_expect(eq(u64, sub1.length, 3));
    cr_expect(strview_equal(&sub1, "ell"));
    
    // Invalid range: start > end
    StrView sub2 = strview_sub(&s, 3, 1);
    cr_expect(eq(u64, sub2.length, 0));
    cr_expect(eq(ptr, (void*)sub2.str, (void*)NULL));
    
    // Invalid range: end > length
    StrView sub3 = strview_sub(&s, 0, 10);
    cr_expect(eq(u64, sub3.length, 0));
    cr_expect(eq(ptr, (void*)sub3.str, (void*)NULL));
    
    // Empty substring
    StrView sub4 = strview_sub(&s, 2, 2);
    cr_expect(eq(u64, sub4.length, 0));
}

Test(strview_suite, prefix_suffix) {
    StrView s = strview_from_str("Hello, World!");
    
    cr_expect(strview_start_with(&s, "Hello"));
    cr_expect_not(strview_start_with(&s, "World"));
    cr_expect(strview_end_with(&s, "World!"));
    cr_expect_not(strview_end_with(&s, "Hello"));
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
    
    cr_expect(strview_eq(&s, &strview_from_str("Hello, World!")));
    cr_expect(eq(u64, s.length, 13));
}

Test(strview_suite, to_cstr) {
    StrView s = strview_from_str("Hello");
    char* cstr = strview_to_cstr(&s);
    
    cr_expect_not_null(cstr);
    cr_expect(eq(str, cstr, "Hello"));
    free(cstr);
}

Test(strview_suite, equal_cstr) {
    StrView s = strview_from_str("Hello");
    
    cr_expect(strview_equal(&s, "Hello"));
    cr_expect_not(strview_equal(&s, "World"));
    cr_expect_not(strview_equal(&s, "Hello, World!"));
}

Test(strview_suite, to_int) {
    StrView s1 = strview_from_str("123");
    StrView s2 = strview_from_str("-456");
    StrView s3 = strview_from_str("0");
    StrView s4 = strview_from_str("abc");
    StrView s5 = strview_from_str("123abc");
    
    cr_expect(eq(i32, strview_to_int(&s1), 123));
    cr_expect(eq(i32, strview_to_int(&s2), -456));
    cr_expect(eq(i32, strview_to_int(&s3), 0));
    cr_expect(eq(i32, strview_to_int(&s4), 0));
    cr_expect(eq(i32, strview_to_int(&s5), 123));
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