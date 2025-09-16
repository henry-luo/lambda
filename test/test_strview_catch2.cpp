#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "../lib/strview.h"
}

TEST_CASE("StrView Basic Operations", "[strview][basic]") {
    const char* str = "Hello, World!";
    StrView s = strview_from_str(str);
    
    REQUIRE(s.length == strlen(str));
    REQUIRE(strview_get(&s, 0) == 'H');
    REQUIRE(strview_get(&s, s.length) == '\0'); // Out of bounds should return '\0'
    REQUIRE(strview_get(&s, s.length - 1) == '!'); // Last character
}

TEST_CASE("StrView Substring", "[strview][substring]") {
    StrView s = strview_from_str("Hello, World!");
    StrView sub = strview_sub(&s, 7, 12);
    
    REQUIRE(sub.length == 5);
    StrView expected = strview_from_str("World");
    REQUIRE(strview_eq(&sub, &expected));
}

TEST_CASE("StrView Substring Edge Cases", "[strview][substring][edge_cases]") {
    StrView s = strview_from_str("Hello");
    
    SECTION("Valid substring") {
        StrView sub1 = strview_sub(&s, 1, 4);
        REQUIRE(sub1.length == 3);
        REQUIRE(strview_equal(&sub1, "ell"));
    }
    
    SECTION("Invalid range: start > end") {
        StrView sub2 = strview_sub(&s, 3, 1);
        REQUIRE(sub2.length == 0);
        REQUIRE(sub2.str == nullptr);
    }
    
    SECTION("Invalid range: end > length") {
        StrView sub3 = strview_sub(&s, 0, 10);
        REQUIRE(sub3.length == 0);
        REQUIRE(sub3.str == nullptr);
    }
    
    SECTION("Empty substring") {
        StrView sub4 = strview_sub(&s, 2, 2);
        REQUIRE(sub4.length == 0);
    }
}

TEST_CASE("StrView Prefix and Suffix", "[strview][prefix][suffix]") {
    StrView s = strview_from_str("Hello, World!");
    
    SECTION("Prefix tests") {
        REQUIRE(strview_start_with(&s, "Hello"));
        REQUIRE_FALSE(strview_start_with(&s, "World"));
    }
    
    SECTION("Suffix tests") {
        REQUIRE(strview_end_with(&s, "World!"));
        REQUIRE_FALSE(strview_end_with(&s, "Hello"));
    }
}

TEST_CASE("StrView Find", "[strview][find]") {
    StrView s = strview_from_str("Hello, World!");
    
    REQUIRE(strview_find(&s, "World") == 7);
    REQUIRE(strview_find(&s, "NotFound") == -1);
    REQUIRE(strview_find(&s, ",") == 5);
}

TEST_CASE("StrView Trim", "[strview][trim]") {
    StrView s = strview_from_str("  Hello, World!  ");
    strview_trim(&s);
    
    StrView expected = strview_from_str("Hello, World!");
    REQUIRE(strview_eq(&s, &expected));
    REQUIRE(s.length == 13);
}

TEST_CASE("StrView To C String", "[strview][conversion]") {
    StrView s = strview_from_str("Hello");
    char* cstr = strview_to_cstr(&s);
    
    REQUIRE(cstr != nullptr);
    REQUIRE(strcmp(cstr, "Hello") == 0);
    free(cstr);
}

TEST_CASE("StrView Equal C String", "[strview][comparison]") {
    StrView s = strview_from_str("Hello");
    
    REQUIRE(strview_equal(&s, "Hello"));
    REQUIRE_FALSE(strview_equal(&s, "World"));
    REQUIRE_FALSE(strview_equal(&s, "Hello, World!"));
}

TEST_CASE("StrView To Integer", "[strview][conversion][integer]") {
    SECTION("Positive integer") {
        StrView s1 = strview_from_str("123");
        REQUIRE(strview_to_int(&s1) == 123);
    }
    
    SECTION("Negative integer") {
        StrView s2 = strview_from_str("-456");
        REQUIRE(strview_to_int(&s2) == -456);
    }
    
    SECTION("Zero") {
        StrView s3 = strview_from_str("0");
        REQUIRE(strview_to_int(&s3) == 0);
    }
    
    SECTION("Invalid string") {
        StrView s4 = strview_from_str("abc");
        REQUIRE(strview_to_int(&s4) == 0);
    }
    
    SECTION("Mixed string") {
        StrView s5 = strview_from_str("123abc");
        REQUIRE(strview_to_int(&s5) == 123);
    }
}
