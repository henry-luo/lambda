#include <gtest/gtest.h>

#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/js/js_transpiler.hpp"

TEST(JsScriptOwnership, AdoptsCommonScriptPrefixIntoRuntimeCatalog) {
    const char source[] = "let retained = 41; retained + 1;";
    JsTranspiler* tp = js_transpiler_create(NULL);
    ASSERT_NE(tp, nullptr);
    ASSERT_TRUE(js_transpiler_parse(tp, source, sizeof(source) - 1));

    TSNode root = ts_tree_root_node(tp->tree);
    JsAstNode* ast = build_js_ast_indexed(tp, root);
    ASSERT_NE(ast, nullptr);

    Pool* ast_pool = tp->pool;
    NamePool* static_names = tp->name_pool;
    AstNode* ast_root = tp->ast_root;
    ASSERT_NE(ast_pool, nullptr);
    ASSERT_NE(static_names, nullptr);
    ASSERT_NE(ast_root, nullptr);

    Runtime runtime = {};
    runtime.scripts = arraylist_new(2);
    ASSERT_NE(runtime.scripts, nullptr);

    JsScript* script = js_script_adopt_transpiler(tp, &runtime, "owner.js");
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->profile, &js_profile);
    EXPECT_EQ(script->pool, ast_pool);
    EXPECT_EQ(script->name_pool, static_names);
    EXPECT_EQ(script->ast_root, ast_root);
    EXPECT_EQ(script->source_length, sizeof(source) - 1);
    EXPECT_NE(script->source, source);
    EXPECT_STREQ(script->source, source);
    EXPECT_STREQ(script->reference, "owner.js");
    EXPECT_EQ(script->index, 0);
    EXPECT_EQ(script->module_state_id, 0u);
    ASSERT_EQ(runtime.scripts->length, 1);
    EXPECT_EQ(runtime.scripts->data[0], (Script*)script);

    // Runtime teardown owns the adopted Script once it has been catalogued.
    runtime_free_all_scripts(&runtime);
}
