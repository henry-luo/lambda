#pragma once
// A gtest fixture that owns one evaluation context: a GC heap, a runtime name
// pool, the side stack and a schema validator. Native tests that call runtime
// functions directly (no lambda.exe) derive from it.

#include <gtest/gtest.h>
#include <string.h>
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/heap_api.h"
#include "../lambda/validator/validator.hpp"

class EvalContextTest : public ::testing::Test {
protected:
    EvalContext eval = {};
    Pool* names = nullptr;

    void SetUp() override {
        names = pool_create();
        ASSERT_NE(names, nullptr);
        ASSERT_TRUE(eval_context_init(&eval));
        heap_init();
        ASSERT_NE(eval.heap, nullptr);
        eval.pool = eval.heap->pool;
        eval.name_pool = name_pool_create_runtime(names);
        ASSERT_NE(eval.name_pool, nullptr);
        ASSERT_TRUE(lambda_side_stack_bind());
        eval.validator = schema_validator_create(names);
        ASSERT_NE(eval.validator, nullptr);
    }

    void TearDown() override {
        schema_validator_destroy(eval.validator);
        eval.validator = nullptr;
        heap_destroy();
        eval.heap = nullptr;
        EXPECT_TRUE(eval_context_shutdown(&eval));
        pool_destroy(names);
    }

    // a native view's attribute, read the way a script reads `view.name`
    Item field(Item view, const char* name) {
        RootFrame roots(1);
        Rooted<Item> receiver(roots, view);
        Item key = {.item = s2it(heap_create_name(name, strlen(name)))};
        return velmt_attr_get(receiver.get().velmt, key);
    }
};
