/**
 * @file test_enhanced_errors.cpp
 * @brief Test enhanced error reporting with path formatting and suggestions
 */

#include <gtest/gtest.h>
#include "../lambda/validator/validator.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"
#include "../lib/strbuf.h"
#include <cstring>

class EnhancedErrorsTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
    }

    void TearDown() override {
        pool_destroy(pool);
    }
};

TEST_F(EnhancedErrorsTest, PathFormatting) {
    // create a validation path: root.user.name[0]
    // path is stored leaf-first, so HEAD of list is the deepest node
    // build: [0] -> name -> user -> NULL

    PathSegment* path_user = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    path_user->type = PATH_FIELD;
    path_user->data.field_name = (StrView){"user", 4};
    path_user->next = nullptr;

    PathSegment* path_name = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    path_name->type = PATH_FIELD;
    path_name->data.field_name = (StrView){"name", 4};
    path_name->next = path_user;

    PathSegment* path_idx = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    path_idx->type = PATH_INDEX;
    path_idx->data.index = 0;
    path_idx->next = path_name;

    // format the path (pass the HEAD which is the leaf)
    String* formatted = format_validation_path(path_idx, pool);

    ASSERT_NE(formatted, nullptr);
    EXPECT_STREQ(formatted->chars, ".user.name[0]");
}

TEST_F(EnhancedErrorsTest, TypeMismatchErrorWithSuggestions) {
    // create a type mismatch error: expected string, got int
    Type* string_type = (Type*)pool_calloc(pool, sizeof(Type));
    string_type->type_id = LMD_TYPE_STRING;

    Item int_item = {.int_val = 42, ._type_id = LMD_TYPE_INT};

    PathSegment* path = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    path->type = PATH_FIELD;
    path->data.field_name = (StrView){"age", 3};
    path->next = nullptr;

    ValidationError* error = create_validation_error_ex(
        VALID_ERROR_TYPE_MISMATCH,
        "Type mismatch",
        path,
        string_type,
        *(ConstItem*)&int_item,
        pool
    );

    // generate suggestions
    List* suggestions = generate_type_suggestions(LMD_TYPE_INT, string_type, pool);
    error->suggestions = suggestions;

    // format the error
    String* formatted = format_error_with_context(error, pool);

    ASSERT_NE(formatted, nullptr);
    ASSERT_NE(formatted->chars, nullptr);

    // check that formatted error contains expected information
    const char* text = formatted->chars;
    EXPECT_NE(strstr(text, "TYPE_MISMATCH"), nullptr) << "Error code should appear";
    EXPECT_NE(strstr(text, ".age"), nullptr) << "Path should appear";
    EXPECT_NE(strstr(text, "Expected:"), nullptr) << "Expected type should appear";
    EXPECT_NE(strstr(text, "Actual:"), nullptr) << "Actual type should appear";

    // if suggestions were generated, they should appear
    if (suggestions && suggestions->length > 0) {
        EXPECT_NE(strstr(text, "Suggestions:"), nullptr) << "Suggestions section should appear";
    }
}

TEST_F(EnhancedErrorsTest, MissingFieldError) {
    // create a missing field error
    PathSegment* path = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    path->type = PATH_FIELD;
    path->data.field_name = (StrView){"username", 8};
    path->next = nullptr;

    ValidationError* error = create_validation_error(
        VALID_ERROR_MISSING_FIELD,
        "Required field 'username' is missing",
        path,
        pool
    );

    // format the error
    String* formatted = format_error_with_context(error, pool);

    ASSERT_NE(formatted, nullptr);
    ASSERT_NE(formatted->chars, nullptr);

    const char* text = formatted->chars;
    EXPECT_NE(strstr(text, "MISSING_FIELD"), nullptr);
    EXPECT_NE(strstr(text, ".username"), nullptr);
    EXPECT_NE(strstr(text, "missing"), nullptr);
}

TEST_F(EnhancedErrorsTest, ValidationReportGeneration) {
    // create a validation result with multiple errors
    ValidationResult* result = (ValidationResult*)pool_calloc(pool, sizeof(ValidationResult));
    result->valid = false;
    result->error_count = 2;
    result->warning_count = 0;

    // error 1: type mismatch
    PathSegment* path1 = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    path1->type = PATH_FIELD;
    path1->data.field_name = (StrView){"age", 3};

    ValidationError* error1 = create_validation_error(
        VALID_ERROR_TYPE_MISMATCH,
        "Expected number, got string",
        path1,
        pool
    );

    // error 2: missing field
    PathSegment* path2 = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    path2->type = PATH_FIELD;
    path2->data.field_name = (StrView){"email", 5};

    ValidationError* error2 = create_validation_error(
        VALID_ERROR_MISSING_FIELD,
        "Required field missing",
        path2,
        pool
    );

    error1->next = error2;
    error2->next = nullptr;
    result->errors = error1;

    // generate report
    String* report = generate_validation_report(result, pool);

    ASSERT_NE(report, nullptr);
    ASSERT_NE(report->chars, nullptr);

    const char* text = report->chars;
    EXPECT_NE(strstr(text, "Validation failed"), nullptr);
    EXPECT_NE(strstr(text, "Errors: 2"), nullptr);
    EXPECT_NE(strstr(text, ".age"), nullptr);
    EXPECT_NE(strstr(text, ".email"), nullptr);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
