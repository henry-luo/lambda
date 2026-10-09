/**
 * CSS Animation Unit Tests
 *
 * Tests: property interpolation (float, color, transform),
 * @keyframes parsing, keyframe registry, animation creation/tick.
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/event.hpp"
#include "../radiant/render.hpp"
#include "../radiant/render_css3d.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/selector_matcher.hpp"

extern "C" {
#include "../lib/mempool.h"
#include "../lib/arena.h"
}

#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/js/js_interp.hpp"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/runtime/interp.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/dom/dom_events.h"
#include "../lambda/dom/dom_cssom.h"
#include "../lambda/dom/dom_core.h"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "../lambda/runtime/template_registry.h"

static struct {
    unsigned starts, ends, iterations, cancels;
    double elapsed;
} animation_events;

// exercise the production queued event path instead of replacing its dispatcher.
static Item record_animation_event(Item event) {
    Item type = js_get_name_key(event, "type");
    if (js_string_equals(type, "animationstart")) animation_events.starts++;
    if (js_string_equals(type, "animationend")) animation_events.ends++;
    if (js_string_equals(type, "animationiteration")) animation_events.iterations++;
    if (js_string_equals(type, "animationcancel")) animation_events.cancels++;
    animation_events.elapsed = js_get_name_key(event, "elapsedTime").get_double();
    return ItemNull;
}

struct CssomDocumentLifetimeProbe : DomDocumentResourceData {
    unsigned* destroyed;
};

static void cssom_document_lifetime_destroyed(DomDocumentResourceData* data) {
    CssomDocumentLifetimeProbe* probe = (CssomDocumentLifetimeProbe*)data;
    ++*probe->destroyed;
    mem_free(probe);
}

static DomDocument* cssom_create_owned_document(unsigned* destroyed) {
    Pool* pool = pool_create();
    Input* input = pool ? Input::create(pool) : nullptr;
    DomDocument* document = input ? dom_document_create(input) : nullptr;
    if (!document) {
        if (pool) pool_destroy(pool);
        return nullptr;
    }
    dom_document_finalize_loader_pool(document, pool);
    document->root = lam::up(DomElement::create(document, "html", nullptr));
    CssomDocumentLifetimeProbe* probe = (CssomDocumentLifetimeProbe*)mem_calloc(
        1, sizeof(*probe), MEM_CAT_LAYOUT);
    if (!probe || !document->root) {
        if (probe) mem_free(probe);
        free_document(document);
        return nullptr;
    }
    probe->destroyed = destroyed;
    if (!dom_document_add_resource(document, probe, cssom_document_lifetime_destroyed)) {
        mem_free(probe);
        free_document(document);
        return nullptr;
    }
    return document;
}

TEST(DomDocumentOwnership, LambdaOnlyEvaluatorClosesDocumentsAtResetAndTeardown) {
    unsigned destroyed = 0;
    Runtime runtime = {};
    runtime_init(&runtime);
    struct Cleanup {
        Runtime* runtime;
        Pool* pool;
        ~Cleanup() {
            if (runtime) runtime_cleanup(runtime);
            if (pool) pool_destroy(pool);
        }
    } cleanup = {&runtime, pool_create()};
    ASSERT_NE(cleanup.pool, nullptr);
    EvalContext* owner = runtime_get_eval_context(&runtime);
    ASSERT_NE(owner, nullptr);
    ASSERT_TRUE(eval_context_init(owner));
    heap_init();
    ASSERT_NE(owner->heap, nullptr);
    owner->pool = owner->heap->pool;
    owner->name_pool = name_pool_create_runtime(cleanup.pool);
    ASSERT_NE(owner->name_pool, nullptr);
    ASSERT_EQ(js_runtime_state_for(runtime.eval_context), nullptr);
    DomDocument* document = cssom_create_owned_document(&destroyed);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(dom_retain_owned_document(document));
    RuntimeResourceTable* table = runtime_resource_table_context(runtime.eval_context);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(runtime_resource_table_active_count(table), 1);
    EXPECT_TRUE(dom_retain_owned_document(document));
    EXPECT_EQ(runtime_resource_table_active_count(table), 1);
    static const char node_brand = 0;
    DomElement* native_root = document->root;
    Item node_wrapper = vmap_new();
    virtual_host_set(node_wrapper, &node_brand, native_root);
    heap_register_gc_root(&node_wrapper.item);
    ASSERT_TRUE(dom_cache_node_wrapper(native_root, document, node_wrapper));
    EXPECT_EQ(dom_cached_node_wrapper(native_root).item, node_wrapper.item);
    EXPECT_EQ(dom_cached_node_wrapper_document(node_wrapper), document);
    DomText* detached = DomText::create_detached_copy(document, "detached", 8);
    ASSERT_NE(detached, nullptr);
    EXPECT_EQ(dom_node_owner_document_bridge(detached), document);
    // weak script carriers cannot collect a document still owned by the evaluator.
    heap_gc_collect();
    EXPECT_EQ(destroyed, 0u);
    EXPECT_STREQ(document->root->tag_name, "html");
    free_document(document);
    EXPECT_EQ(virtual_host_data(node_wrapper), nullptr);
    heap_unregister_gc_root(&node_wrapper.item);
    EXPECT_EQ(destroyed, 1u);
    EXPECT_EQ(runtime_resource_table_active_count(table), 0);

    document = cssom_create_owned_document(&destroyed);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(dom_retain_owned_document(document));
    runtime_reset_heap(&runtime);
    EXPECT_EQ(destroyed, 2u);
    EXPECT_EQ(runtime_resource_table_active_count(table), 0);
    heap_init();
    ASSERT_NE(owner->heap, nullptr);
    owner->pool = owner->heap->pool;
    owner->name_pool = name_pool_create_runtime(cleanup.pool);
    ASSERT_NE(owner->name_pool, nullptr);
    document = cssom_create_owned_document(&destroyed);
    ASSERT_NE(document, nullptr);
    ASSERT_TRUE(dom_retain_owned_document(document));
    runtime_cleanup(&runtime);
    cleanup.runtime = nullptr;
    EXPECT_EQ(destroyed, 3u);
}

TEST(CssomIdentity, WeakWrappersKeepDeclarationParentsAcrossGcAndDetachSafely) {
    Runtime runtime = {};
    runtime_init(&runtime);
    struct Cleanup {
        Runtime* runtime;
        Pool* pool = nullptr;
        DomDocument* document = nullptr;
        ~Cleanup() {
            if (document) dom_cssom_invalidate_document(document);
            dom_set_document(nullptr);
            runtime_cleanup(runtime);
            if (document) dom_document_destroy(document);
            css_property_system_cleanup();
            if (pool) pool_destroy(pool);
        }
    } cleanup = {&runtime};
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "null;", 5,
        "cssom-identity.js", nullptr)));
    cleanup.pool = pool_create();
    ASSERT_NE(cleanup.pool, nullptr);
    Input* input = Input::create(cleanup.pool);
    ASSERT_NE(input, nullptr);
    cleanup.document = dom_document_create(input);
    ASSERT_NE(cleanup.document, nullptr);
    DomElement* root = DomElement::create(cleanup.document, "html", nullptr);
    ASSERT_NE(root, nullptr);
    cleanup.document->root = lam::up(root);
    dom_set_document(cleanup.document);
    CssEngine* engine = css_engine_create(cleanup.pool);
    ASSERT_NE(engine, nullptr);
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        ".a { color: red; marker: url(#probe); & .b { color: blue; } color: green; }", nullptr);
    CssStylesheet* imported = css_parse_stylesheet(engine, ".child { width: 10px; }", "child.css");
    const char* import_text = "@import 'child.css';";
    CssRule* import_rule = css_parse_rule_text(import_text, strlen(import_text), cleanup.pool);
    css_engine_destroy(engine);
    ASSERT_NE(sheet, nullptr);
    ASSERT_NE(imported, nullptr);
    ASSERT_NE(import_rule, nullptr);
    sheet->owner_document = cleanup.document;
    css_rule_attach(import_rule, nullptr, sheet);
    imported->parent_stylesheet = sheet;
    imported->owner_rule = import_rule;
    import_rule->data.import_rule.stylesheet = imported;
    struct WorkerCall { EvalContext* eval; CssStylesheet* sheet; } call = {
        runtime.eval_context, sheet};
    // create the weak index on a worker, then destroy the document after the legal handoff.
    ASSERT_TRUE(js_runtime_state_shutdown(call.eval));
    Item wrapper = interp_run_on_large_stack(call.eval, [](void* opaque) -> Item {
        WorkerCall* call = (WorkerCall*)opaque;
        if (!js_runtime_state_init(call->eval)) return ItemError;
        Item result = dom_cssom_wrap_stylesheet(call->sheet);
        return js_runtime_state_shutdown(call->eval) ? result : ItemError;
    }, &call);
    RootFrame roots(14);
    Rooted<Item> read_root(roots, ItemNull);
    Rooted<Item> sheet_root(roots, wrapper);
    ASSERT_TRUE(js_runtime_state_init(call.eval));
    ASSERT_EQ(get_type_id(sheet_root.get()), LMD_TYPE_VMAP);
    Rooted<Item> document_root(roots, dom_cached_node_wrapper(cleanup.document->js.doc_node));
    ASSERT_EQ(get_type_id(document_root.get()), LMD_TYPE_VELMT);
    EXPECT_EQ(dom_cached_node_wrapper_document(document_root.get()), cleanup.document);
    Rooted<Item> element_root(roots, dom_wrap_element(root));
    Rooted<Item> style_key_root(roots, js_name_item("style"));
    Rooted<Item> inline_root(roots, dom_core_get_property(element_root.get(), style_key_root.get()));
    Rooted<Item> computed_root(roots, dom_get_computed_style(element_root.get(), ItemNull));
    ASSERT_TRUE(dom_is_inline_style_item(inline_root.get()));
    ASSERT_TRUE(dom_is_computed_style_item(computed_root.get()));
    Rooted<Item> rule_root(roots, dom_cssom_stylesheet_rule_at(
        sheet_root.get(), (Item){.item = i2it(0)}));
    Rooted<Item> declaration_root(roots, dom_cssom_rule_get_style(rule_root.get()));
    Rooted<Item> rules_root(roots, dom_cssom_stylesheet_get_css_rules(sheet_root.get()));
    Rooted<Item> nested_root(roots, dom_cssom_rule_get_css_rules(rule_root.get()));
    Rooted<Item> key_root(roots, js_name_item("parentRule"));
    ASSERT_GT(css_property_register_custom("--probe", cleanup.pool), 0);
    // legacy native property IDs must not collapse distinct CSSOM DOMString names.
    key_root.set(js_make_string_len("--probe\0a", 9));
    style_key_root.set(js_name_item("10px"));
    EXPECT_FALSE(item_is_error(dom_cssom_rule_decl_set_value(inline_root.get(),
        key_root.get(), style_key_root.get(), ItemNull)));
    key_root.set(js_name_item("--probe"));
    style_key_root.set(js_name_item("20px"));
    EXPECT_FALSE(item_is_error(dom_cssom_rule_decl_set_value(inline_root.get(),
        key_root.get(), style_key_root.get(), ItemNull)));
    key_root.set(js_make_string_len("--probe\0a", 9));
    EXPECT_STREQ(fn_to_cstr(dom_cssom_rule_decl_get_value(inline_root.get(), key_root.get())), "10px");
    key_root.set(js_name_item("marker-end"));
    read_root.set(dom_cssom_rule_decl_get_value(declaration_root.get(), key_root.get()));
    EXPECT_STREQ(fn_to_cstr(read_root.get()), "url(\"#probe\")");
    style_key_root.set(js_name_item("markerEnd"));
    read_root.set(dom_cssom_rule_decl_get_property(declaration_root.get(), style_key_root.get()));
    style_key_root.set(js_name_item("cssText"));
    read_root.set(dom_cssom_rule_decl_get_property(declaration_root.get(), style_key_root.get()));
    CssRule* block = (CssRule*)virtual_host_data(declaration_root.get());
    ASSERT_NE(block, nullptr); ASSERT_NE(block->pool, nullptr);
    size_t bytes_before = 0, count_before = 0;
    pool_get_stats(block->pool, &bytes_before, &count_before);
    for (int repeat = 0; repeat < 32; repeat++) {
        read_root.set(dom_cssom_rule_decl_get_value(declaration_root.get(), key_root.get()));
        EXPECT_STREQ(fn_to_cstr(read_root.get()), "url(\"#probe\")");
        read_root.set(dom_cssom_rule_decl_get_property(declaration_root.get(), style_key_root.get()));
        EXPECT_EQ(get_type_id(read_root.get()), LMD_TYPE_STRING);
    }
    size_t bytes_after = 0, count_after = 0;
    pool_get_stats(block->pool, &bytes_after, &count_after);
    // read projections must not accumulate allocations in a retained stylesheet (D4.5.1v4).
    EXPECT_EQ(bytes_after, bytes_before); EXPECT_EQ(count_after, count_before);
    key_root.set(js_name_item("parentRule"));
    style_key_root.set(js_name_item("style"));
    ASSERT_EQ(get_type_id(declaration_root.get()), LMD_TYPE_VMAP);
    EXPECT_EQ(sheet_root.get().item, dom_cssom_wrap_stylesheet(sheet).item);
    EXPECT_EQ(rule_root.get().item, dom_cssom_stylesheet_rule_at(
        sheet_root.get(), (Item){.item = i2it(0)}).item);
    EXPECT_EQ(declaration_root.get().item, dom_cssom_rule_get_style(rule_root.get()).item);
    EXPECT_EQ(inline_root.get().item,
        dom_core_get_property(element_root.get(), style_key_root.get()).item);
    EXPECT_EQ(rules_root.get().item, dom_cssom_stylesheet_get_css_rules(sheet_root.get()).item);
    EXPECT_EQ(nested_root.get().item, dom_cssom_rule_get_css_rules(rule_root.get()).item);
    EXPECT_FALSE(it2b(js_strict_equal(rule_root.get(), declaration_root.get())));
    Rooted<Item> imported_root(roots, dom_cssom_wrap_stylesheet(imported));
    Rooted<Item> import_root(roots, dom_cssom_stylesheet_get_owner_rule(imported_root.get()));
    EXPECT_EQ(dom_cssom_stylesheet_get_owner_node(imported_root.get()).item, ItemNull.item);
    EXPECT_EQ(dom_cssom_stylesheet_get_parent_style_sheet(imported_root.get()).item, sheet_root.get().item);
    EXPECT_EQ(dom_cssom_stylesheet_get_owner_rule(imported_root.get()).item, import_root.get().item);
    css_rule_attach(import_rule, nullptr, nullptr);
    EXPECT_EQ(dom_cssom_stylesheet_get_parent_style_sheet(imported_root.get()).item, ItemNull.item);
    EXPECT_EQ(dom_cssom_stylesheet_get_owner_rule(imported_root.get()).item, import_root.get().item);
    uint64_t parent_identity = rule_root.get().item;
    // the declaration's hidden traced edge must retain its parent without native-stack scanning.
    sheet_root.set(ItemNull);
    rule_root.set(ItemNull);
    rules_root.set(ItemNull);
    nested_root.set(ItemNull);
    heap_gc_collect();
    rule_root.set(dom_cssom_rule_decl_get_property(declaration_root.get(), key_root.get()));
    EXPECT_EQ(rule_root.get().item, parent_identity);
    EXPECT_EQ(declaration_root.get().item, dom_cssom_rule_get_style(rule_root.get()).item);
    rules_root.set(dom_cssom_rule_get_css_rules(rule_root.get()));
    nested_root.set(dom_cssom_get_document_stylesheets());
    // direct native destruction uses the document resource hook, not a UI-only path.
    dom_document_destroy(cleanup.document);
    cleanup.document = nullptr;
    dom_set_document(nullptr);
    EXPECT_EQ(virtual_host_data(document_root.get()), nullptr);
    // retained declarations must detach before their document-owned payload is freed.
    EXPECT_EQ(virtual_host_data(inline_root.get()), nullptr);
    EXPECT_EQ(virtual_host_data(computed_root.get()), nullptr);
    ASSERT_FALSE(dom_is_css_rule(rule_root.get()));
    ASSERT_FALSE(dom_is_rule_style_decl(declaration_root.get()));
    ASSERT_FALSE(dom_is_stylesheet(imported_root.get()));
    ASSERT_FALSE(dom_is_css_rule(import_root.get()));
    EXPECT_EQ(dom_cssom_rule_get_style(rule_root.get()).item, ItemNull.item);
    EXPECT_EQ(rules_root.get().varray->vtable->items.count(rules_root.get().varray->data), 0);
    EXPECT_EQ(nested_root.get().varray->vtable->items.count(nested_root.get().varray->data), 0);
    declaration_root.set(ItemNull);
    rule_root.set(ItemNull);
    heap_gc_collect();
}

TEST(CssCascade, SelectorListUsesStrongestMatchingBranch) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    DomDocument* doc = dom_document_create(input);
    ASSERT_NE(doc, nullptr);
    DomElement* element = DomElement::create(doc, "div", nullptr);
    ASSERT_NE(element, nullptr);
    ASSERT_TRUE(element->set_attribute("id", "target"));
    ASSERT_TRUE(element->add_class("a"));

    CssEngine* engine = css_engine_create(pool);
    ASSERT_NE(engine, nullptr);
    CssStylesheet* sheet = css_parse_stylesheet(engine,
        "div.a, #target.a { color: red; } #target { color: blue; }", nullptr);
    ASSERT_NE(sheet, nullptr);
    ASSERT_EQ(sheet->rule_count, 2u);
    SelectorMatcher* matcher = selector_matcher_create(pool);
    ASSERT_NE(matcher, nullptr);
    for (size_t i = 0; i < sheet->rule_count; i++) {
        radiant_apply_css_rule_to_element(element, sheet->rules[i], matcher, pool, engine);
    }

    CssDeclaration* winner = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(winner, nullptr);
    // the second branch has (1,1,0), above the later #target rule's (1,0,0).
    EXPECT_EQ(winner->specificity.ids, 1);
    EXPECT_EQ(winner->specificity.classes, 1);

    DomElement* inline_target = DomElement::create(doc, "div", nullptr);
    ASSERT_NE(inline_target, nullptr);
    ASSERT_TRUE(inline_target->set_attribute("id", "inline-target"));
    ASSERT_TRUE(inline_target->set_attribute("style", "color: blue !important"));
    CssStylesheet* important_sheet = css_parse_stylesheet(engine,
        "#inline-target { color: red !important; }", nullptr);
    ASSERT_NE(important_sheet, nullptr);
    ASSERT_EQ(important_sheet->rule_count, 1u);
    radiant_apply_css_rule_to_element(inline_target, important_sheet->rules[0],
                                      matcher, pool, engine);
    CssDeclaration* important_winner = dom_element_get_specified_value(
        inline_target, CSS_PROPERTY_COLOR);
    ASSERT_NE(important_winner, nullptr);
    // Both are author-important; the inline declaration wins on specificity.
    EXPECT_EQ(important_winner->origin, CSS_ORIGIN_AUTHOR);
    EXPECT_EQ(important_winner->specificity.inline_style, 1);
    selector_matcher_destroy(matcher);
    dom_document_destroy(doc);
    pool_destroy(pool);
}

TEST(CssPropTable, RowsAreUniqueAndSerializeSyntheticElement) {
    struct MetadataOwner {
        Pool* pool = pool_create();
        ~MetadataOwner() {css_property_system_cleanup(); pool_destroy(pool);}
    } metadata;
    ASSERT_NE(metadata.pool, nullptr);
    // synthetic documents still need the same initialized property metadata as a loaded document.
    ASSERT_TRUE(css_property_system_init(metadata.pool));
    size_t count = 0;
    const CssPropAccessor* rows = css_prop_accessors(&count);
    ASSERT_NE(rows, nullptr);
    ASSERT_GT(count, 0u);

    DomDocument doc = {};
    DomElement element = {};
    element.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    element.doc = lam::up(&doc);
    element.set_styles_resolved(true);
    doc.root = lam::up(&element);
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(css_prop_accessor(rows[i].id), &rows[i]);
        EXPECT_GT(rows[i].id, CSS_PROPERTY_UNKNOWN);
        EXPECT_LT(rows[i].id, CSS_PROPERTY_COUNT);
        EXPECT_NE(rows[i].serialize, nullptr);
        for (size_t j = 0; j < i; j++) EXPECT_NE(rows[i].id, rows[j].id);

        char value[512];
        EXPECT_TRUE(css_prop_serialize_computed(
            &element, rows[i].id, 0, value, sizeof(value))) << rows[i].id;
    }
}

TEST(CssPropTable, DirtyMutationDoesNotConsumePendingLayout) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    DomDocument* doc = dom_document_create(input);
    ASSERT_NE(doc, nullptr);
    DomElement* element = DomElement::create(doc, "div", nullptr);
    ASSERT_NE(element, nullptr);
    InlineProp in_line = INLINE_PROP_DEFAULT;
    in_line.opacity = 1.0f;
    element->in_line = lam::view_ref(&in_line);
    element->set_styles_resolved(true);
    doc->root = lam::up(element);
    ASSERT_TRUE(element->set_attribute("style", "opacity:.25"));
    doc->js.mutation_count = 1;
    char value[64];
    // a declaration read observes the write without committing pending geometry.
    EXPECT_TRUE(css_prop_serialize_computed(
        element, CSS_PROPERTY_OPACITY, 0, value, sizeof(value)));
    EXPECT_STREQ(value, "0.25");
    EXPECT_FLOAT_EQ(in_line.opacity, 1.0f);
    EXPECT_EQ(doc->js.mutation_count, 1);
    element->in_line = nullptr;
    dom_document_destroy(doc);
    pool_destroy(pool);
}

TEST(CssPropTable, UnanimatedDeclarationReadsDoNotRequestGeometry) {
    Pool* pool = pool_create();
    Input* input = Input::create(pool);
    DomDocument* doc = dom_document_create(input);
    ASSERT_NE(doc, nullptr);
    DomElement* element = DomElement::create(doc, "div", nullptr);
    ASSERT_NE(element, nullptr);
    doc->root = lam::up(element);
    ASSERT_TRUE(element->set_attribute("style",
        "display:contents;--fade:20%;opacity:calc(var(--fade) + 5%)"));
    doc->js.mutation_count = 1;
    static int sync_count = 0;
    sync_count = 0;
    radiant_set_cssom_used_value_sync([](DomDocument*) { sync_count++; return true; });
    char value[32];
    EXPECT_TRUE(css_prop_serialize_computed(element, CSS_PROPERTY_DISPLAY, 0, value, sizeof(value)));
    EXPECT_STREQ(value, "contents");
    EXPECT_TRUE(css_prop_serialize_computed(element, CSS_PROPERTY_OPACITY, 0, value, sizeof(value)));
    EXPECT_STREQ(value, "0.25");
    EXPECT_EQ(sync_count, 0);
    EXPECT_EQ(doc->js.mutation_count, 1);
    radiant_set_cssom_used_value_sync(nullptr);
    dom_document_destroy(doc);
    pool_destroy(pool);
}

TEST(CssPropTable, CommittedStylesIgnoreRangeDocumentWrapper) {
    DomDocument doc = {};
    DomElement element = {}, document_wrapper = {};
    element.node_type = document_wrapper.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    document_wrapper.set_synthetic(true);
    document_wrapper.tag_name = lam::up("#document");
    element.doc = lam::up(&doc);
    element.parent = lam::up(static_cast<DomNode*>(&document_wrapper));
    element.set_styles_resolved(true);
    InlineProp in_line = INLINE_PROP_DEFAULT;
    in_line.opacity = 0.3f;
    element.in_line = lam::view_ref(&in_line);
    doc.root = lam::up(&element);
    char value[64];
    EXPECT_TRUE(css_prop_serialize_computed(
        &element, CSS_PROPERTY_OPACITY, 0, value, sizeof(value)));
    EXPECT_STREQ(value, "0.3");
}

class MotionCascadeTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    CssEngine* engine = nullptr;
    DomDocument doc = {};
    DomElement element = {};
    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        engine = css_engine_create(pool);
        ASSERT_NE(engine, nullptr);
        element.node_type = DOM_NODE_ELEMENT;
        element.set_synthetic(true);
        element.set_styles_resolved(true);
        element.doc = lam::up(&doc);
        doc.document_pool = lam::own(pool);
        doc.root = lam::up(&element);
        element.specified_style = lam::shared(style_tree_create(pool));
    }
    void TearDown() override {
        css_engine_destroy(engine);
        pool_destroy(pool);
    }
    void apply(const char* text) {
        CssDeclaration* declaration = css_parse_declaration_text(text, strlen(text), pool);
        ASSERT_NE(declaration, nullptr);
        ASSERT_NE(style_tree_apply_declaration(element.specified_style, declaration), nullptr);
    }
};

TEST_F(MotionCascadeTest, ScrollKeepsSelectorCacheValidButHoverInvalidatesIt) {
    ASSERT_NE(state_store_create(&doc), nullptr);
    DocState* state = doc.state;
    ScrollPane pane = {};
    pane.v_max_scroll = 200.0f;
    uint64_t content = doc_state_content_version(state);
    uint64_t selectors = doc_state_selector_version(state);
    scroll_state_set_position_for_view(state, static_cast<View*>(&element), &pane, 0.0f, 50.0f, false);
    EXPECT_GT(doc_state_content_version(state), content);
    EXPECT_EQ(doc_state_selector_version(state), selectors);
    doc_state_request_repaint(state);
    EXPECT_EQ(doc_state_selector_version(state), selectors);
    doc_state_set_hover_target(state, static_cast<View*>(&element));
    EXPECT_GT(doc_state_selector_version(state), selectors);
    state_store_destroy(&doc);
}

TEST_F(MotionCascadeTest, PresentationCustomPropertiesReplaceRetainCascadeAndClear) {
    ASSERT_EQ(dom_element_apply_inline_style(&element, "--height:20px;--light:0.5"), 2);
    bool changed = false;
    ASSERT_TRUE(dom_element_set_presentation_style(&element, "--height", "40px", &changed));
    EXPECT_TRUE(changed);
    ASSERT_TRUE(dom_element_set_presentation_style(&element, "--height", "40px", &changed));
    EXPECT_FALSE(changed);
    DomElement child = {};
    child.node_type = DOM_NODE_ELEMENT;
    child.parent = lam::up(static_cast<DomNode*>(&element));
    DomElement* owner = nullptr;
    const CssValue* value = dom_element_lookup_custom_property(&child, "--height", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(owner, &element);
    EXPECT_FLOAT_EQ(value->data.length.value, 40.0f);
    for (unsigned i = 0; i < 200; i++) {
        ASSERT_TRUE(dom_element_set_presentation_style(&element, "--height", i % 2 ? "41px" : "40px", &changed));
    }
    unsigned count = 0;
    for (CssCustomProp* prop = element.css_variables; prop; prop = prop->next) count++;
    EXPECT_EQ(count, 3u);
    dom_element_clear_cascaded_styles(&element);
    value = dom_element_lookup_custom_property(&child, "--height", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_FLOAT_EQ(value->data.length.value, 41.0f);
    ASSERT_EQ(dom_element_apply_inline_style(&element, "--height:80px!important"), 1);
    value = dom_element_lookup_custom_property(&child, "--height", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_FLOAT_EQ(value->data.length.value, 80.0f);
    EXPECT_FALSE(dom_element_set_presentation_style(&element, "--height", "]", &changed));
    EXPECT_TRUE(dom_element_clear_presentation_style(&element));
    value = dom_element_lookup_custom_property(&child, "--height", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_FLOAT_EQ(value->data.length.value, 80.0f);
    EXPECT_FALSE(dom_element_clear_presentation_style(&element));
}

TEST_F(MotionCascadeTest, AnimationShorthandProjectsWinningLonghands) {
    const char* declarations[] = {
        "animation-duration: 9s", "animation: fade 2s linear -1s 1.5 alternate both paused, grow 4s",
        "animation-duration: 3000ms, 5s"
    };
    for (const char* text : declarations) apply(text);
    const CssPropertyCode properties[] = {CSS_PROPERTY_ANIMATION_NAME, CSS_PROPERTY_ANIMATION_DURATION,
        CSS_PROPERTY_ANIMATION_DELAY, CSS_PROPERTY_ANIMATION_ITERATION_COUNT,
        CSS_PROPERTY_ANIMATION_DIRECTION, CSS_PROPERTY_ANIMATION_FILL_MODE, CSS_PROPERTY_ANIMATION_PLAY_STATE};
    const char* expected[] = {"fade, grow", "3s, 5s", "-1s, 0s", "1.5, 1", "alternate, normal", "both, none", "paused, running"};
    for (unsigned i = 0; i < sizeof(properties) / sizeof(*properties); i++) {
        char value[128];
        ASSERT_TRUE(css_prop_serialize_computed(&element, properties[i], 0, value, sizeof(value)));
        EXPECT_STREQ(value, expected[i]);
    }
    apply("animation: 1s linear Ease");
    char name[64];
    ASSERT_TRUE(css_prop_serialize_computed(&element, CSS_PROPERTY_ANIMATION_NAME, 0, name, sizeof(name)));
    EXPECT_STREQ(name, "Ease");
    apply("animation-name: EASE, Red, BLOCK");
    ASSERT_TRUE(css_prop_serialize_computed(&element, CSS_PROPERTY_ANIMATION_NAME, 0, name, sizeof(name)));
    EXPECT_STREQ(name, "EASE, Red, BLOCK");
}

TEST_F(MotionCascadeTest, TransitionListsCycleAndLastPropertyEntryWinsBeyondEight) {
    apply("transition-duration: 9s");
    apply("transition: opacity 2s linear -1s, width 4s ease-in");
    apply("transition-property: first, second, third, fourth, fifth, sixth, seventh, eighth, opacity, width");
    apply("transition-duration: 3000ms, 5s");
    char serialized[128];
    ASSERT_TRUE(css_prop_serialize_computed(&element, CSS_PROPERTY_TRANSITION_DURATION, 0,
        serialized, sizeof(serialized)));
    EXPECT_STREQ(serialized, "3s, 5s");
    CssTransitionList list;
    CssTransitionProp config;
    css_transition_resolve_config(&element, pool, &list);
    ASSERT_TRUE(css_transition_select_config(&list, CSS_PROPERTY_OPACITY, &config));
    EXPECT_FLOAT_EQ(config.duration, 3.0f);
    EXPECT_FLOAT_EQ(config.delay, -1.0f);
    EXPECT_EQ(config.timing.type, TIMING_LINEAR);
    ASSERT_TRUE(css_transition_select_config(&list, CSS_PROPERTY_WIDTH, &config));
    EXPECT_FLOAT_EQ(config.duration, 5.0f);
    EXPECT_FLOAT_EQ(config.delay, 0.0f);
    apply("transition-property: all, opacity");
    css_transition_resolve_config(&element, pool, &list);
    ASSERT_TRUE(css_transition_select_config(&list, CSS_PROPERTY_OPACITY, &config));
    EXPECT_FLOAT_EQ(config.duration, 5.0f);
    apply("transition: none");
    css_transition_resolve_config(&element, pool, &list);
    EXPECT_FALSE(css_transition_select_config(&list, CSS_PROPERTY_OPACITY, &config));
}

TEST(CssPropTable, VisibilityUsesRenderEnumNames) {
    DomDocument doc = {};
    DomElement element = {};
    element.node_type = DOM_NODE_ELEMENT;
    element.set_synthetic(true);
    InlineProp in_line = INLINE_PROP_DEFAULT;
    element.doc = lam::up(&doc);
    element.in_line = lam::view_ref(&in_line);
    element.set_styles_resolved(true);
    doc.root = lam::up(&element);

    struct VisibilityCase {
        Visibility value;
        const char* expected;
    } cases[] = {
        {VIS_VISIBLE, "visible"},
        {VIS_HIDDEN, "hidden"},
        {VIS_COLLAPSE, "collapse"},
    };

    for (const VisibilityCase& test_case : cases) {
        in_line.visibility = test_case.value;
        char value[32];
        ASSERT_TRUE(css_prop_serialize_computed(
            &element, CSS_PROPERTY_VISIBILITY, 0, value, sizeof(value)));
        EXPECT_STREQ(value, test_case.expected);
    }
}

TEST(DomSelectorRows, RepeatedQueriesLeaveDocumentPoolUnchanged) {
    Runtime runtime = {};
    runtime_init(&runtime);
    struct Cleanup {
        Runtime* runtime;
        Pool* pool = nullptr;
        DomDocument* document = nullptr;
        ~Cleanup() {
            dom_set_document(nullptr);
            runtime_cleanup(runtime);
            if (document) dom_document_destroy(document);
            if (pool) pool_destroy(pool);
        }
    } cleanup = {&runtime};
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "null;", 5,
        "selector-rows.js", nullptr)));
    cleanup.pool = pool_create();
    ASSERT_NE(cleanup.pool, nullptr);
    Input* input = Input::create(cleanup.pool);
    ASSERT_NE(input, nullptr);
    cleanup.document = dom_document_create(input);
    ASSERT_NE(cleanup.document, nullptr);
    DomElement* root = DomElement::create(cleanup.document, "html", nullptr);
    ASSERT_NE(root, nullptr);
    cleanup.document->root = lam::up(root);
    dom_set_document(cleanup.document);
    auto* child = (DomElement*)dom_create_backed_element_bridge(cleanup.document, "div");
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("class", "probe"));
    ASSERT_TRUE(static_cast<DomNode*>(root)->append_child(child));

    RootFrame roots(4);
    Rooted<Item> root_item(roots, dom_wrap_element(root));
    Rooted<Item> child_item(roots, dom_wrap_element(child));
    Rooted<Item> list(roots, js_name_item("html > .probe, .missing"));
    Rooted<Item> found(roots, ItemNull);
    auto query_all_rows = [&]() {
        found.set(dom_core_query_selector(root_item.get(), list.get()));
        EXPECT_EQ(dom_unwrap_element(found.get()), (void*)child);
        found.set(dom_core_query_selector_all(root_item.get(), list.get()));
        EXPECT_NE(get_type_id(found.get()), LMD_TYPE_NULL);
        EXPECT_EQ(dom_core_matches(child_item.get(), list.get()).item, ITEM_TRUE);
        found.set(dom_core_closest(child_item.get(), list.get()));
        EXPECT_EQ(dom_unwrap_element(found.get()), (void*)child);
    };
    // selectors and matchers are per-call; repeated rows must not grow the document
    query_all_rows();
    PoolStats warm = {};
    pool_get_detailed_stats(cleanup.document->document_pool, &warm);
    for (int i = 0; i < 64; i++) query_all_rows();
    PoolStats repeated = {};
    pool_get_detailed_stats(cleanup.document->document_pool, &repeated);
    EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
}

TEST(SelectorMatcherLifetime, DestroyReturnsTheMatcherBlockToItsPool) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    PoolStats before = {};
    pool_get_detailed_stats(pool, &before);
    for (int i = 0; i < 32; i++) selector_matcher_destroy(selector_matcher_create(pool));
    PoolStats after = {};
    pool_get_detailed_stats(pool, &after);
    EXPECT_EQ(after.live_bytes, before.live_bytes);
    pool_destroy(pool);
}

// Helper: set up a stylesheet with one @keyframes rule on a doc
static void setup_keyframes_sheet(DomDocument* doc, CssStylesheet* sheet,
                                   CssRule* rule, CssRule** rule_ptr,
                                   CssStylesheet** sheet_ptr,
                                   const char* content) {
    memset(sheet, 0, sizeof(*sheet));
    sheet->pool = doc->document_pool;
    sheet->disabled = false;

    memset(rule, 0, sizeof(*rule));
    rule->type = CSS_RULE_KEYFRAMES;
    rule->data.generic_rule.name = "keyframes";
    rule->data.generic_rule.content = content;

    *rule_ptr = rule;
    sheet->rules = rule_ptr;
    sheet->rule_count = 1;

    *sheet_ptr = sheet;
    doc->stylesheets = lam::own_arr(sheet_ptr);
    doc->stylesheet_count = 1;
}

TEST_F(MotionCascadeTest, ExtendingExpiredDurationResumesTheRetainedTimeline) {
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;
    setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr,
        "fade { from { opacity: 0; } to { opacity: 1; } }");
    InlineProp in_line = INLINE_PROP_DEFAULT;
    element.in_line = lam::view_ref(&in_line);
    DocState state = {};
    state.animation_scheduler = animation_scheduler_create(pool);
    doc.state = lam::up(&state);
    UiContext ui = {};
    ui.document = lam::up(&doc);
    LayoutContext context = {};
    context.pool = lam::up(pool);
    context.ui_context = lam::up(&ui);
    apply("animation: fade 1s linear forwards");
    css_animation_resolve(&element, &context);
    AnimationInstance* instance = state.animation_scheduler->first;
    ASSERT_NE(instance, nullptr);
    animation_scheduler_tick(state.animation_scheduler, 2.0, nullptr);
    ASSERT_EQ(instance->play_state, ANIM_PLAY_FINISHED);
    apply("animation-duration: 4s");
    css_animation_resolve(&element, &context);
    EXPECT_EQ(state.animation_scheduler->first, instance);
    EXPECT_EQ(instance->play_state, ANIM_PLAY_RUNNING);
    EXPECT_DOUBLE_EQ(instance->start_time, 0.0);
    EXPECT_TRUE(state.animation_scheduler->has_active_animations);
    EXPECT_FLOAT_EQ(in_line.opacity, 0.5f);
    animation_scheduler_tick(state.animation_scheduler, 3.0, nullptr);
    EXPECT_FLOAT_EQ(in_line.opacity, 0.75f);
    animation_scheduler_destroy(state.animation_scheduler);
    doc.state = nullptr;
}

TEST_F(MotionCascadeTest, RepeatedResolveDoesNotGrowRetainedLayoutPool) {
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;
    setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr,
        "fade { from { opacity: 0; } to { opacity: 1; } }");
    InlineProp in_line = INLINE_PROP_DEFAULT;
    element.in_line = lam::view_ref(&in_line);
    DocState state = {};
    state.animation_scheduler = animation_scheduler_create(pool);
    doc.state = lam::up(&state);
    UiContext ui = {};
    ui.document = lam::up(&doc);
    // the layout pool is the view tree's retained prop pool in real passes
    Pool* view_pool = pool_create();
    ASSERT_NE(view_pool, nullptr);
    LayoutContext context = {};
    context.pool = lam::up(view_pool);
    context.ui_context = lam::up(&ui);

    // unanimated elements still compute animation-name, so both shapes resolve a list
    const char* styles[] = {"animation-name: none", "animation: fade 1s linear forwards"};
    for (const char* style : styles) {
        apply(style);
        css_animation_resolve(&element, &context);
        PoolStats warm = {};
        pool_get_detailed_stats(view_pool, &warm);
        for (int i = 0; i < 128; i++) css_animation_resolve(&element, &context);
        PoolStats repeated = {};
        pool_get_detailed_stats(view_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes) << style;
    }
    EXPECT_NE(state.animation_scheduler->first, nullptr);
    animation_scheduler_destroy(state.animation_scheduler);
    doc.state = nullptr;
    pool_destroy(view_pool);
}

// ============================================================================
// Float Interpolation Tests
// ============================================================================

TEST(CssInterpolation, FloatLerp) {
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(css_interpolate_float(0.0f, 1.0f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(css_interpolate_float(10.0f, 20.0f, 0.3f), 13.0f);
}

TEST(CssInterpolation, FloatNegative) {
    EXPECT_FLOAT_EQ(css_interpolate_float(-10.0f, 10.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(-10.0f, 10.0f, 0.0f), -10.0f);
    EXPECT_FLOAT_EQ(css_interpolate_float(-10.0f, 10.0f, 1.0f), 10.0f);
}

TEST(CssTransform, Translate3dPercentagesUseTransformReferenceBox) {
    TransformFunction translate = {};
    translate.type = TRANSFORM_TRANSLATE3D;
    translate.translate_x_percent = 25.0f;
    translate.translate_y_percent = 100.0f;
    translate.params.translate3d.z = 12.0f;

    RdtMatrix matrix = radiant::compute_transform_matrix(
        &translate, 400.0f, 83.6f, 0.0f, 0.0f);

    EXPECT_FLOAT_EQ(matrix.e13, 100.0f);
    EXPECT_FLOAT_EQ(matrix.e23, 83.6f);
}

TEST(CssTransform, GroupingEffectsFlattenUsedTransformStyle) {
    DomElement element = {};
    DomElementExt extension = {};
    TransformProp transform = TRANSFORM_PROP_DEFAULT;
    ScrollProp scroll = SCROLL_PROP_DEFAULT;
    InlineProp inline_prop = INLINE_PROP_DEFAULT;
    BlockProp block = BLOCK_PROP_DEFAULT;
    FilterProp filter = {};
    FilterFunction function = {};
    element.set_synthetic(true);
    element.ext = lam::own(&extension);
    element.transform = lam::view_prop(&transform);
    element.scroller = lam::view_prop(&scroll);
    element.in_line = lam::view_ref(&inline_prop);
    element.blk = lam::view_prop(&block);
    extension.filter = lam::view_prop(&filter);
    transform.transform_style = CSS_VALUE_PRESERVE_3D;
    EXPECT_TRUE(radiant::transform_preserves_3d(&element));
    scroll.overflow_x = CSS_VALUE_CLIP;
    EXPECT_TRUE(radiant::transform_preserves_3d(&element));
    scroll.overflow_y = CSS_VALUE_HIDDEN;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    scroll.overflow_y = CSS_VALUE_VISIBLE;
    inline_prop.opacity = 0.5f;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    inline_prop.opacity = 1.0f;
    inline_prop.mix_blend_mode = CSS_VALUE_MULTIPLY;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    inline_prop.mix_blend_mode = CSS_VALUE_NORMAL;
    filter.functions = lam::own(&function);
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    filter.functions = nullptr;
    block.contain_paint = true;
    EXPECT_FALSE(radiant::transform_preserves_3d(&element));
    block.contain_paint = false;
    EXPECT_TRUE(radiant::transform_preserves_3d(&element));
    EXPECT_EQ(transform.transform_style, CSS_VALUE_PRESERVE_3D);
}

TEST(CssTransform, BackfaceNormalUsesInverseTranspose) {
    RdtMatrix4 matrix = rdt_matrix4_identity();
    EXPECT_FALSE(rdt_matrix4_backface_visible(&matrix));
    matrix.values[0] = -1.0f;
    EXPECT_FALSE(rdt_matrix4_backface_visible(&matrix));
    matrix = rdt_matrix4_identity();
    matrix.values[10] = -1.0f;
    EXPECT_TRUE(rdt_matrix4_backface_visible(&matrix));
    matrix = rdt_matrix4_identity();
    matrix.values[2] = matrix.values[8] = 2.0f;
    // m33 remains positive, but the transformed normal points away from the viewer.
    EXPECT_TRUE(rdt_matrix4_backface_visible(&matrix));
    matrix.values[0] = 4.0f;
    EXPECT_FALSE(rdt_matrix4_backface_visible(&matrix));
}

TEST(CssTransform, PaintProjectsNestedDepthOnceAndHonorsFlattening) {
    const bool cases[] = {false, true};
    for (bool preserve : cases) {
        ViewBlock camera = {}, scene = {}, plane = {};
        camera.set_synthetic(true); scene.set_synthetic(true); plane.set_synthetic(true);
        camera.width = camera.height = scene.width = scene.height = plane.width = plane.height = 100.0f;
        TransformProp camera_prop = TRANSFORM_PROP_DEFAULT;
        TransformProp scene_prop = TRANSFORM_PROP_DEFAULT;
        TransformProp plane_prop = TRANSFORM_PROP_DEFAULT;
        TransformFunction translation = {};
        translation.type = TRANSFORM_TRANSLATEZ;
        translation.params.translate3d.z = 100.0f;
        camera_prop.perspective = 500.0f;
        scene_prop.transform_style = preserve ? CSS_VALUE_PRESERVE_3D : CSS_VALUE_FLAT;
        scene_prop.functions = lam::shared(&translation);
        plane_prop.functions = lam::shared(&translation);
        camera.transform = lam::view_prop(&camera_prop);
        scene.transform = lam::view_prop(&scene_prop);
        plane.transform = lam::view_prop(&plane_prop);
        RasterRenderContext context = {};
        context.raster_scale = 1.0f;
        BlockBlot parent = {};
        RenderTransformScope camera_scope = render_state_push_transform(&context, &camera, &parent);
        RenderTransformScope scene_scope = render_state_push_transform(&context, &scene, &parent);
        RenderTransformScope plane_scope = render_state_push_transform(&context, &plane, &parent);
        // Two 100px depths in a 500px camera: scale 500/300 when preserved,
        // or 500/400 when the middle box flattens its descendants.
        float expected_scale = preserve ? 500.0f / 300.0f : 500.0f / 400.0f;
        EXPECT_NEAR(context.transform.e11, expected_scale, 0.00001f);
        EXPECT_NEAR(context.transform.e22, expected_scale, 0.00001f);
        EXPECT_NEAR(context.transform.e13, 50.0f * (1.0f - expected_scale), 0.00001f);
        render_state_pop_transform(&plane_scope);
        EXPECT_NEAR(context.transform.e11, 1.25f, 0.00001f);
        render_state_pop_transform(&scene_scope);
        render_state_pop_transform(&camera_scope);
        EXPECT_FALSE(context.has_transform);
        EXPECT_FALSE(context.has_transform_3d);
    }
}

class Css3dCompositionTest : public ::testing::Test {
protected:
    Arena* arena = nullptr;
    ScratchArena scratch = {};
    void SetUp() override {
        arena = arena_create(16384, 65536);
        ASSERT_NE(arena, nullptr);
        scratch_init(&scratch, arena);
    }
    void TearDown() override {
        scratch_release(&scratch);
        arena_destroy(arena);
    }
};

TEST_F(Css3dCompositionTest, ParallelPlanesKeepDepthOrderAndCoplanarPaintTies) {
    const bool reverse_cases[] = {false, true};
    for (bool reverse : reverse_cases) {
        ScratchScope scope(&scratch);
        Css3dPolygon input[3];
        const float depths[] = {80.0f, -80.0f, 80.0f};
        for (size_t i = 0; i < 3; i++) {
            RdtMatrix4 matrix = rdt_matrix4_translate(0.0f, 0.0f, depths[i]);
            ASSERT_TRUE(css3d_project_quad(&matrix, {0.0f, 0.0f, 100.0f, 100.0f}, i,
                reverse ? 2 - i : i, &scope, &input[i]));
        }
        lam::ArrayList<Css3dPolygon> ordered(MEM_CAT_RENDER, 0);
        ASSERT_TRUE(css3d_order_planes(input, 3, &scope, &ordered));
        ASSERT_EQ(ordered.size(), 3u);
        EXPECT_EQ(ordered[0].fragment, 1u);
        EXPECT_EQ(ordered[1].fragment, reverse ? 2u : 0u);
        EXPECT_EQ(ordered[2].fragment, reverse ? 0u : 2u);
    }
}

TEST_F(Css3dCompositionTest, IntersectingPlanesSplitAndKeepProjectiveTextureCoordinates) {
    const bool reverse_cases[] = {false, true};
    for (bool reverse : reverse_cases) {
        ScratchScope scope(&scratch);
        Css3dPolygon input[2];
        for (size_t i = 0; i < 2; i++) {
            RdtMatrix4 matrix = rdt_matrix4_identity();
            matrix.values[8] = i == 0 ? 1.0f : -1.0f;
            matrix.values[12] = 0.2f;
            ASSERT_TRUE(css3d_project_quad(&matrix, {-1.0f, -1.0f, 2.0f, 2.0f}, i, i,
                &scope, &input[reverse ? 1 - i : i]));
        }
        lam::ArrayList<Css3dPolygon> ordered(MEM_CAT_RENDER, 0);
        ASSERT_TRUE(css3d_order_planes(input, 2, &scope, &ordered));
        ASSERT_EQ(ordered.size(), 3u);
        const float probes[] = {-0.5f, 0.5f};
        for (float probe : probes) {
            float previous_depth = -INFINITY;
            size_t covering = 0;
            for (const Css3dPolygon& polygon : ordered) {
                float left = INFINITY, right = -INFINITY;
                for (size_t i = 0; i < polygon.count; i++) {
                    const Css3dVertex& vertex = polygon.vertices[i];
                    left = fminf(left, vertex.x / vertex.w);
                    right = fmaxf(right, vertex.x / vertex.w);
                    EXPECT_NEAR(vertex.x, vertex.u, 0.00001f);
                    EXPECT_NEAR(vertex.y, vertex.v, 0.00001f);
                    EXPECT_NEAR(vertex.w, 1.0f + 0.2f * vertex.u, 0.00001f);
                }
                if (probe > left && probe < right) {
                    float depth = polygon.fragment == 0 ? probe : -probe;
                    EXPECT_GT(depth, previous_depth);
                    previous_depth = depth;
                    covering++;
                }
            }
            EXPECT_EQ(covering, 2u);
        }
    }
}

TEST_F(Css3dCompositionTest, ViewerPlaneCrossingIsClippedBeforeProjection) {
    ScratchScope scope(&scratch);
    RdtMatrix4 matrix = rdt_matrix4_identity();
    matrix.values[12] = -0.5f;
    Css3dPolygon crossing;
    ASSERT_TRUE(css3d_project_quad(&matrix, {0.0f, 0.0f, 4.0f, 1.0f}, 0, 0, &scope, &crossing));
    ASSERT_EQ(crossing.count, 4u);
    for (size_t i = 0; i < crossing.count; i++) {
        const Css3dVertex& vertex = crossing.vertices[i];
        EXPECT_GT(vertex.w, 0.0f);
        EXPECT_LT(vertex.u, 2.0f);
        EXPECT_NEAR(vertex.x, vertex.u, 0.00001f);
    }
    matrix.values[15] = -1.0f;
    Css3dPolygon hidden;
    ASSERT_TRUE(css3d_project_quad(&matrix, {0.0f, 0.0f, 4.0f, 1.0f}, 0, 0, &scope, &hidden));
    EXPECT_EQ(hidden.count, 0u);
}

TEST_F(Css3dCompositionTest, ViewerClippingAtPageOffsetKeepsFiniteBoundsAndTerminatesPartition) {
    ScratchScope scope(&scratch);
    RdtMatrix4 rotation = rdt_matrix4_identity();
    float angle = 75.0f * math_pi_f() / 180.0f;
    rotation.values[0] = rotation.values[10] = cosf(angle);
    rotation.values[2] = sinf(angle); rotation.values[8] = -sinf(angle);
    RdtMatrix4 origin = rdt_matrix4_translate(550.0f, 330.0f, 450.0f);
    RdtMatrix4 offset = rdt_matrix4_translate(-550.0f, -330.0f, 0.0f);
    RdtMatrix4 local = rdt_matrix4_multiply(&rotation, &offset);
    local = rdt_matrix4_multiply(&origin, &local);
    RdtMatrix4 perspective = radiant::compute_parent_perspective_matrix_3d(500.0f, 550.0f, 330.0f);
    RdtMatrix4 matrix = rdt_matrix4_multiply(&perspective, &local);
    RdtMatrix paint = radiant::matrix4_project_to_2d(&matrix);
    Css3dPolygon polygon;
    ASSERT_TRUE(css3d_project_quad(&matrix, {470.0f, 250.0f, 160.0f, 160.0f}, 0, 0, &scope, &polygon));
    ASSERT_TRUE(css3d_clip_to_viewport(&polygon, &paint, {440.0f, 220.0f, 220.0f, 220.0f}, &scope));
    ASSERT_GE(polygon.count, 3u);
    for (size_t i = 0; i < polygon.count; i++) {
        float x = polygon.vertices[i].x / polygon.vertices[i].w;
        float y = polygon.vertices[i].y / polygon.vertices[i].w;
        EXPECT_GE(x, 439.9f); EXPECT_LE(x, 660.1f);
        EXPECT_GE(y, 219.9f); EXPECT_LE(y, 440.1f);
    }
    lam::ArrayList<Css3dPolygon> ordered(MEM_CAT_RENDER, 0);
    ASSERT_TRUE(css3d_order_planes(&polygon, 1, &scope, &ordered));
    EXPECT_EQ(ordered.size(), 1u);
    float left, top, right, bottom;
    EXPECT_FALSE(rdt_matrix_project_rect_bounds(&paint, 470.0f, 250.0f, 630.0f, 410.0f,
        &left, &top, &right, &bottom));
}

// ============================================================================
// Color Interpolation Tests
// ============================================================================

TEST(CssInterpolation, ColorLerp) {
    Color a = {0}; a.r = 0; a.g = 0; a.b = 0; a.a = 255;
    Color b = {0}; b.r = 255; b.g = 255; b.b = 255; b.a = 255;

    Color mid = css_interpolate_color(a, b, 0.5f);
    EXPECT_EQ(mid.r, 128);
    EXPECT_EQ(mid.g, 128);
    EXPECT_EQ(mid.b, 128);
    EXPECT_EQ(mid.a, 255);
}

TEST(CssInterpolation, ColorAtBoundaries) {
    Color a = {0}; a.r = 100; a.g = 50; a.b = 200; a.a = 255;
    Color b = {0}; b.r = 200; b.g = 150; b.b = 100; b.a = 128;

    Color at0 = css_interpolate_color(a, b, 0.0f);
    EXPECT_EQ(at0.r, 100);
    EXPECT_EQ(at0.g, 50);
    EXPECT_EQ(at0.b, 200);
    EXPECT_EQ(at0.a, 255);

    Color at1 = css_interpolate_color(a, b, 1.0f);
    EXPECT_EQ(at1.r, 200);
    EXPECT_EQ(at1.g, 150);
    EXPECT_EQ(at1.b, 100);
    EXPECT_EQ(at1.a, 128);
}

TEST(CssInterpolation, ColorRedToBlue) {
    Color red = {0}; red.r = 255; red.g = 0; red.b = 0; red.a = 255;
    Color blue = {0}; blue.r = 0; blue.g = 0; blue.b = 255; blue.a = 255;

    Color quarter = css_interpolate_color(red, blue, 0.25f);
    EXPECT_NEAR(quarter.r, 191, 1);
    EXPECT_EQ(quarter.g, 0);
    EXPECT_NEAR(quarter.b, 64, 1);
}

TEST(CssInterpolation, TransparentEndpointsPreservePremultipliedColor) {
    Color transparent = {0};
    Color blue = {0xFFFF0000};
    Color half = css_interpolate_color(transparent, blue, .5f);
    EXPECT_EQ(half.r, 0);
    EXPECT_EQ(half.g, 0);
    EXPECT_EQ(half.b, 255);
    EXPECT_EQ(half.a, 128);
}

// ============================================================================
// Keyframe Parsing Tests
// ============================================================================

class KeyframeParsingTest : public ::testing::Test {
protected:
    Pool* pool;
    DomDocument doc;
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;

    void SetUp() override {
        pool = pool_create();
        memset(&doc, 0, sizeof(doc));
        doc.document_pool = lam::own(pool);
        doc.node_arena = lam::own(arena_create_default());
    }
    void TearDown() override {
        if (doc.node_arena) arena_destroy(doc.node_arena);
        pool_destroy(pool);
    }
    void setupKeyframes(const char* content) {
        setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr, content);
    }
};

TEST_F(KeyframeParsingTest, SimpleOpacityFromTo) {
    setupKeyframes("fadeIn { from { opacity: 0; } to { opacity: 1; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(registry, nullptr);
    EXPECT_EQ(registry->count, 1);

    CssKeyframes* kf = keyframe_registry_find(registry, "fadeIn");
    ASSERT_NE(kf, nullptr);
    EXPECT_STREQ(kf->name, "fadeIn");
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_FLOAT_EQ(kf->stops[0].offset, 0.0f);
    EXPECT_FLOAT_EQ(kf->stops[1].offset, 1.0f);

    EXPECT_EQ(kf->stops[0].property_count, 1);
    EXPECT_EQ(kf->stops[0].properties[0].property_code, CSS_PROPERTY_OPACITY);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_FLOAT);
    EXPECT_FLOAT_EQ(kf->stops[0].properties[0].value.f, 0.0f);

    EXPECT_EQ(kf->stops[1].property_count, 1);
    EXPECT_EQ(kf->stops[1].properties[0].property_code, CSS_PROPERTY_OPACITY);
    EXPECT_FLOAT_EQ(kf->stops[1].properties[0].value.f, 1.0f);
}

TEST_F(KeyframeParsingTest, QuotedNamesAndLaterDefinitionsUseSharedNameGrammar) {
    setupKeyframes("\"quoted { name\" { from { opacity: 0; } to { opacity: 1; } }");
    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(registry, nullptr);
    ASSERT_EQ(registry->count, 1);
    CssKeyframes* keyframes = keyframe_registry_find(registry, "quoted { name");
    ASSERT_NE(keyframes, nullptr);
    CssKeyframes replacement = *keyframes;
    CssKeyframes* entries[] = {keyframes, &replacement};
    registry->entries = lam::own_arr(entries);
    registry->count = 2;
    EXPECT_EQ(keyframe_registry_find(registry, "quoted { name"), &replacement);
    setupKeyframes("Ease { from { opacity: 0; } to { opacity: 1; } }");
    registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(keyframe_registry_find(registry, "Ease"), nullptr);
    EXPECT_EQ(keyframe_registry_find(registry, "ease"), nullptr);
}

TEST_F(KeyframeParsingTest, PercentageStops) {
    setupKeyframes("pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "pulse");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 3);

    EXPECT_FLOAT_EQ(kf->stops[0].offset, 0.0f);
    EXPECT_FLOAT_EQ(kf->stops[1].offset, 0.5f);
    EXPECT_FLOAT_EQ(kf->stops[2].offset, 1.0f);

    EXPECT_FLOAT_EQ(kf->stops[1].properties[0].value.f, 0.5f);
}

TEST_F(KeyframeParsingTest, TransformKeyframes) {
    setupKeyframes("slideIn { from { transform: translateX(-100px); } to { transform: translateX(0px); } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "slideIn");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_EQ(kf->stops[0].properties[0].property_code, CSS_PROPERTY_TRANSFORM);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_TRANSFORM);
    TransformFunction* tf = kf->stops[0].properties[0].value.transform;
    ASSERT_NE(tf, nullptr);
    EXPECT_EQ(tf->type, TRANSFORM_TRANSLATEX);
    EXPECT_FLOAT_EQ(tf->params.translate.x, -100.0f);
}

TEST_F(KeyframeParsingTest, TransformKeyframesResolveCssAngleAndPercentUnits) {
    setupKeyframes("tail { from { transform: translateY(7%) rotate(1deg); } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "tail");
    ASSERT_NE(kf, nullptr);
    ASSERT_EQ(kf->stop_count, 1);

    TransformFunction* translate = kf->stops[0].properties[0].value.transform;
    ASSERT_NE(translate, nullptr);
    EXPECT_EQ(translate->type, TRANSFORM_TRANSLATEY);
    EXPECT_FLOAT_EQ(translate->params.translate.y, 0.0f);
    EXPECT_FLOAT_EQ(translate->translate_y_percent, 7.0f);

    TransformFunction* rotate = translate->next;
    ASSERT_NE(rotate, nullptr);
    EXPECT_EQ(rotate->type, TRANSFORM_ROTATE);
    EXPECT_NEAR(rotate->params.angle, acosf(-1.0f) / 180.0f, 0.00001f);
}

TEST_F(KeyframeParsingTest, ColorKeyframes) {
    setupKeyframes("colorShift { from { background-color: #ff0000; } to { background-color: #0000ff; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "colorShift");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_EQ(kf->stops[0].properties[0].property_code, CSS_PROPERTY_BACKGROUND_COLOR);
    EXPECT_EQ(kf->stops[0].properties[0].value_type, ANIM_VAL_COLOR);
    EXPECT_EQ(kf->stops[0].properties[0].value.color.r, 255);
    EXPECT_EQ(kf->stops[0].properties[0].value.color.g, 0);
    EXPECT_EQ(kf->stops[0].properties[0].value.color.b, 0);

    EXPECT_EQ(kf->stops[1].properties[0].value.color.r, 0);
    EXPECT_EQ(kf->stops[1].properties[0].value.color.g, 0);
    EXPECT_EQ(kf->stops[1].properties[0].value.color.b, 255);
}

TEST_F(KeyframeParsingTest, MultipleProperties) {
    setupKeyframes("fadeSlide { from { opacity: 0; transform: translateY(-20px); } to { opacity: 1; transform: translateY(0px); } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "fadeSlide");
    ASSERT_NE(kf, nullptr);
    EXPECT_EQ(kf->stop_count, 2);

    EXPECT_EQ(kf->stops[0].property_count, 2);
    EXPECT_EQ(kf->stops[1].property_count, 2);
}

TEST_F(KeyframeParsingTest, LengthValuesRejectInvalidUnitsAndExpressions) {
    const struct { CssPropertyCode property; const char* value; } invalid[] = {
        {CSS_PROPERTY_WIDTH, "100x"}, {CSS_PROPERTY_WIDTH, "2"},
        {CSS_PROPERTY_PADDING_TOP, "-1px"}, {CSS_PROPERTY_PADDING_LEFT, "unknown(2px)"},
        {CSS_PROPERTY_MARGIN_LEFT, "1furlong"}, {CSS_PROPERTY_LEFT, "calc(1px + wat)"},
        {CSS_PROPERTY_BORDER_TOP_WIDTH, "2%"}, {CSS_PROPERTY_BORDER_LEFT_WIDTH, "3furlong"}
    };
    for (const auto& sample : invalid) {
        CssAnimatedProp parsed = {};
        EXPECT_FALSE(css_animation_parse_property_value(sample.property, sample.value, &parsed, pool))
            << sample.property << ":" << sample.value;
    }
}

TEST_F(KeyframeParsingTest, RegistryFindMissing) {
    doc.stylesheets = NULL;
    doc.stylesheet_count = 0;

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(registry, nullptr);
    EXPECT_EQ(registry->count, 0);

    CssKeyframes* kf = keyframe_registry_find(registry, "nonExistent");
    EXPECT_EQ(kf, nullptr);
}

TEST_F(KeyframeParsingTest, LastValidDeclarationWinsWithinEachStop) {
    setupKeyframes("grow { from { width:10px; width:20px; width:2junk; }"
        "to { width:30px; width:40px; width:unknown(2px); } }");
    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(registry, nullptr);
    CssKeyframes* keyframes = keyframe_registry_find(registry, "grow");
    ASSERT_NE(keyframes, nullptr);
    ASSERT_EQ(keyframes->stop_count, 2);
    for (int index = 0; index < 2; index++) {
        ASSERT_EQ(keyframes->stops[index].property_count, 1);
        EXPECT_FLOAT_EQ(keyframes->stops[index].properties[0].value.length.value,
            index == 0 ? 20.0f : 40.0f);
    }
}

TEST_F(KeyframeParsingTest, ImportantEndpointsAreIgnoredAcrossValueTypes) {
    const struct { CssPropertyCode property; const char* value; } endpoints[] = {
        {CSS_PROPERTY_WIDTH, "20px !important"},
        {CSS_PROPERTY_OPACITY, ".5 !IMPORTANT"},
        {CSS_PROPERTY_COLOR, "red ! /*priority*/ ImPoRtAnT"},
        {CSS_PROPERTY_ASPECT_RATIO, "2 / 1 !important"},
        {CSS_PROPERTY_TRANSFORM, "translateX(20px) !important"},
        {CSS_PROPERTY_DISPLAY, "none !important"}
    };
    for (const auto& endpoint : endpoints) {
        CssAnimatedProp parsed = {};
        EXPECT_FALSE(css_animation_parse_property_value(endpoint.property,
            endpoint.value, &parsed, pool)) << endpoint.value;
    }
    setupKeyframes("priority { from { width:10px; width:20px !important; opacity:.2;"
        "opacity:.5 !IMPORTANT; animation-composition:add;"
        "animation-composition:replace!important; animation-composition:invalid; }"
        "to { width:30px !important; width:40px; animation-composition:accumulate;"
        "animation-composition:replace !IMPORTANT; } }");
    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* keyframes = keyframe_registry_find(registry, "priority");
    ASSERT_NE(keyframes, nullptr);
    ASSERT_EQ(keyframes->stop_count, 2);
    ASSERT_EQ(keyframes->stops[0].property_count, 2);
    EXPECT_FLOAT_EQ(keyframes->stops[0].properties[0].value.length.value, 10.0f);
    EXPECT_FLOAT_EQ(keyframes->stops[0].properties[1].value.f, .2f);
    EXPECT_EQ(keyframes->stops[0].properties[0].composite, CSS_ANIM_COMPOSITE_ADD);
    ASSERT_EQ(keyframes->stops[1].property_count, 1);
    EXPECT_FLOAT_EQ(keyframes->stops[1].properties[0].value.length.value, 40.0f);
    EXPECT_EQ(keyframes->stops[1].properties[0].composite, CSS_ANIM_COMPOSITE_ACCUMULATE);
}

TEST_F(KeyframeParsingTest, QuotedAndEscapedNamesUseDecodedIdentity) {
    setupKeyframes("\"a{b\" { from { opacity: 0; } to { opacity: 1; } }");
    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(keyframe_registry_find(registry, "a{b"), nullptr);
    setupKeyframes("\\52 everse { from { opacity: 0; } to { opacity: 1; } }");
    registry = keyframe_registry_create(&doc, pool);
    ASSERT_NE(keyframe_registry_find(registry, "Reverse"), nullptr);
}

// ============================================================================
// Animation Tick Tests
// ============================================================================

class AnimationTickTest : public ::testing::Test {
protected:
    Pool* pool;
    AnimationScheduler* scheduler;
    DomDocument doc;
    DomDocument* event_document;
    DocState document_state;
    UiContext ui;
    LayoutContext layout;

    void SetUp() override {
        timing_init_presets();
        animation_events = {};
        event_document = nullptr;
        pool = pool_create();
        scheduler = animation_scheduler_create(pool);
        memset(&doc, 0, sizeof(doc));
        doc.document_pool = lam::own(pool);
        doc.node_arena = lam::own(arena_create_default());
        document_state = {};
        document_state.animation_scheduler = scheduler;
        doc.state = lam::up(&document_state);
        ui = {};
        ui.document = lam::up(&doc);
        layout = {};
        layout.pool = lam::up(pool);
        layout.ui_context = lam::up(&ui);
    }
    void TearDown() override {
        animation_scheduler_destroy(scheduler);
        if (event_document) dom_document_destroy(event_document);
        if (doc.node_arena) arena_destroy(doc.node_arena);
        pool_destroy(pool);
    }

    struct MockElement {
        uint8_t buf[4096];
        InlineProp in_line;
    };

    DomElement* createMockElement(MockElement* mock) {
        memset(mock, 0, sizeof(*mock));
        DomElement* element = (DomElement*)mock->buf;
        element->node_type = DOM_NODE_ELEMENT;
        element->doc = lam::up(&doc);
        ((ViewSpan*)element)->in_line = lam::view_ref(&mock->in_line);
        return element;
    }

    void setTransformContext(DomElement* element, TransformProp* transform, FontProp* font) {
        // transform bounds consume a laid-out block view, as the render fixtures do.
        element->view_type = RDT_VIEW_BLOCK;
        element->font = lam::view_prop(font);
        element->transform = lam::view_prop(transform);
        element->width = 40.0f;
        element->height = 10.0f;
        layout.view = lam::up(static_cast<View*>(element));
        layout.elmt = lam::up(element);
        layout.font.style = lam::up(font);
        layout.font.current_font_size = font->font_size;
    }

    CssAnimProp defaultAnimProp(const char* name, float duration) {
        CssAnimProp ap;
        memset(&ap, 0, sizeof(ap));
        ap.name = lam::up(name);
        ap.duration = duration;
        ap.iteration_count = 1;
        ap.direction = ANIM_DIR_NORMAL;
        ap.fill_mode = ANIM_FILL_FORWARDS;
        ap.play_state = ANIM_PLAY_RUNNING;
        ap.timing.type = TIMING_LINEAR;
        return ap;
    }

    CssKeyframes* parsedKeyframes(const char* content, const char* name) {
        CssStylesheet sheet;
        CssRule rule;
        CssRule* rule_ptr;
        CssStylesheet* sheet_ptr;
        setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr,
            content);
        KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
        doc.services.keyframe_registry = registry;
        doc.stylesheets = nullptr;
        doc.stylesheet_count = 0;
        return keyframe_registry_find(registry, name);
    }

    CssKeyframes* opacityKeyframes() {
        return parsedKeyframes("schedulerFade { from { opacity: 0; } to { opacity: 1; } }",
            "schedulerFade");
    }

    void setAnimationStyle(DomElement* element, const char* source) {
        element->specified_style = lam::shared(style_tree_create(pool));
        size_t count = 0;
        CssDeclaration** declarations = css_parse_declaration_list_text(source, strlen(source), pool, &count);
        ASSERT_NE(declarations, nullptr);
        for (size_t i = 0; i < count; i++) {
            ASSERT_TRUE(dom_element_apply_declaration(element, declarations[i]));
        }
        // these fixtures supply a committed cascade without a stylesheet loader.
        element->set_styles_resolved(true);
        element->set_needs_style_recompute(false);
    }

    void registerProperty(const char* name, const char* syntax, const char* initial, bool inherits = true) {
        CssPropertyRegistration registration = {};
        registration.name = name;
        registration.inherits = inherits;
        ASSERT_TRUE(css_parse_property_syntax(syntax, pool, &registration));
        CssDeclaration* declaration = css_parse_property_declaration(name, strlen(name), initial, strlen(initial), pool);
        ASSERT_NE(declaration, nullptr);
        registration.initial_value = declaration->value;
        ASSERT_TRUE(css_register_document_property(&doc, &registration, strlen(name)));
    }

    double customNumber(DomElement* element, const char* name) {
        const CssValue* value = css_compute_element_custom_property(pool, element, name);
        EXPECT_NE(value, nullptr);
        EXPECT_EQ(value ? value->type : CSS_VALUE_TYPE_KEYWORD, CSS_VALUE_TYPE_NUMBER);
        return value && value->type == CSS_VALUE_TYPE_NUMBER ? value->data.number.value : NAN;
    }

    void setPhysicalSideTargets(BoundaryProp* boundary, BorderProp* border,
                                PositionProp* position, float margin,
                                float border_width, float padding_inset, Color color) {
        for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
            border->width.values[side] = border_width;
            border->colors[side] = color;
            border->styles[side] = CSS_VALUE_SOLID;
            boundary->margin.values[side] = margin;
            boundary->margin.types[side] = CSS_VALUE__UNDEF;
            boundary->padding.values[side] = padding_inset;
            position->inset_values[side] = padding_inset;
            position->inset_present[side] = true;
            position->inset_percents[side] = NAN;
        }
        boundary->flow_margin = boundary->margin;
        boundary->has_flow_margin = true;
    }
};

TEST_F(AnimationTickTest, PausedEffectSamplesAndResumesFromFrozenTime) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssAnimProp config = defaultAnimProp("schedulerFade", 1.0f);
    config.delay = -.5f;
    config.play_state = ANIM_PLAY_PAUSED;
    AnimationInstance* instance = css_animation_create(scheduler, element, &config,
        opacityKeyframes(), 0.0, pool);
    ASSERT_NE(instance, nullptr);
    animation_scheduler_tick(scheduler, 0.0, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    mock.in_line.opacity = 0.0f;
    animation_scheduler_tick(scheduler, 5.0, nullptr, true);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    animation_instance_resume(instance, 5.0);
    animation_scheduler_tick(scheduler, 5.25, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .75f);
}

TEST_F(AnimationTickTest, BackgroundPositionStepsPauseAndMixedUnitsReachPaintOffsets) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    BoundaryProp boundary = {};
    BackgroundProp background = {};
    element->bound = lam::view_prop(&boundary);
    boundary.background = lam::own(&background);
    ASSERT_NE(parsedKeyframes("sheet{from{background-position-x:0px;background-position-y:0%}"
        "to{background-position-x:var(--end);background-position-y:100px}}", "sheet"), nullptr);
    setAnimationStyle(element, "--end:-120px;animation:sheet 1s steps(4) -.375s both paused");
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, -120.0f), -30.0f);
    EXPECT_FLOAT_EQ(background_position_offset(&background, false, 80.0f), 25.0f);
    animation_scheduler_tick(scheduler, 20.0, nullptr, true);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, -120.0f), -30.0f);
    AnimationInstance* instance = scheduler->first;
    ASSERT_NE(instance, nullptr);
    animation_instance_resume(instance, 20.0);
    animation_scheduler_tick(scheduler, 20.25, nullptr);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, -120.0f), -60.0f);
    EXPECT_FALSE(instance->layout_changed);
    while (scheduler->first) animation_scheduler_cancel(scheduler, scheduler->first);

    ASSERT_NE(parsedKeyframes("mix{from{background-position-x:0%}to{background-position-x:40px}}", "mix"), nullptr);
    setAnimationStyle(element, "animation:mix 1s linear -.5s both paused");
    css_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, 200.0f), 20.0f);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, 400.0f, 2.0f), 40.0f);
}

TEST_F(AnimationTickTest, BackgroundPositionPercentageRemainsRelativeToImageFreeSpace) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    BoundaryProp boundary = {};
    BackgroundProp background = {};
    element->bound = lam::view_prop(&boundary);
    boundary.background = lam::own(&background);
    ASSERT_NE(parsedKeyframes("position{from{background-position-x:100%}to{background-position-x:40px}}", "position"), nullptr);
    setAnimationStyle(element, "animation:position 1s linear -.5s both paused");
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, 200.0f), 120.0f);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, -200.0f), -80.0f);
    EXPECT_FLOAT_EQ(background_position_offset(&background, true, 400.0f, 2.0f), 240.0f);
}

TEST_F(AnimationTickTest, IndividualTransformsAnimateTogetherAndRetainIndependentSnapshots) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    TransformProp transform = {};
    FontProp font = {};
    font.font_size = 16.0f;
    setTransformContext(element, &transform, &font);
    transform.individual[0].type = TRANSFORM_TRANSLATE;
    transform.individual[0].params.translate.x = 6.0f;
    transform.individual[0].translate_x_percent = NAN;
    transform.individual[0].translate_y_percent = NAN;
    ASSERT_NE(parsedKeyframes("individual{from{translate:0px 0px;rotate:0deg;scale:1}"
        "to{translate:100% 20px;rotate:180deg;scale:3 5}}", "individual"), nullptr);
    setAnimationStyle(element, "animation:individual 1s linear -.5s both paused");
    css_animation_resolve(element, &layout);
    EXPECT_EQ(transform.individual[0].type, TRANSFORM_TRANSLATE);
    EXPECT_FLOAT_EQ(transform_translate_component(transform.individual[0].params.translate.x,
        transform.individual[0].translate_x_percent, 40.0f), 20.0f);
    EXPECT_FLOAT_EQ(transform.individual[0].params.translate.y, 10.0f);
    EXPECT_NEAR(transform.individual[1].params.angle, M_PI / 2, 0.00001);
    EXPECT_FLOAT_EQ(transform.individual[2].params.scale.x, 2.0f);
    EXPECT_FLOAT_EQ(transform.individual[2].params.scale.y, 3.0f);
    ASSERT_NE(scheduler->first, nullptr);
    for (int i = 0; i < 200; i++) css_animation_tick(scheduler->first, 0.5f);
    EXPECT_FLOAT_EQ(transform.individual[0].params.translate.y, 10.0f);
    animation_scheduler_cancel(scheduler, scheduler->first);
    EXPECT_FLOAT_EQ(transform.individual[0].params.translate.x, 6.0f);
    EXPECT_EQ(transform.individual[1].type, TRANSFORM_NONE);
    EXPECT_EQ(transform.individual[2].type, TRANSFORM_NONE);
}

TEST_F(AnimationTickTest, DiscreteBackgroundImagesResolveStylesheetUrlsAndRestoreOnCancel) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    BoundaryProp boundary = {};
    BackgroundProp background = {};
    background.image = pool_strdup(pool, "authored.png");
    element->bound = lam::view_prop(&boundary);
    boundary.background = lam::own(&background);
    CssKeyframes* keyframes = parsedKeyframes("images{0%{background-image:url(red.png)}"
        "50%{background-image:url(blue.png)}100%{background-image:none}}", "images");
    ASSERT_NE(keyframes, nullptr);
    keyframes->source_file = "file:///fixtures/skins/scene.css";
    setAnimationStyle(element, "animation:images 1s steps(1) -.25s both paused");
    layout.doc = lam::up(&doc);
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    ASSERT_NE(background.image, nullptr);
    EXPECT_NE(strstr(background.image, "/skins/red.png"), nullptr);
    AnimationInstance* instance = scheduler->first;
    ASSERT_NE(instance, nullptr);
    css_animation_tick(instance, .75f);
    ASSERT_NE(background.image, nullptr);
    EXPECT_NE(strstr(background.image, "/skins/blue.png"), nullptr);
    const char* sample = background.image;
    for (int i = 0; i < 200; i++) css_animation_tick(instance, .75f);
    EXPECT_EQ(background.image, sample);
    css_animation_tick(instance, 1.0f);
    EXPECT_EQ(background.image, nullptr);
    animation_scheduler_cancel(scheduler, instance);
    EXPECT_STREQ(background.image, "authored.png");
}

TEST_F(AnimationTickTest, FilterListsPadIdentityInterpolateMathAndRestoreOnCancel) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    FilterProp filter = {};
    FilterFunction authored = {};
    authored.type = FILTER_BRIGHTNESS;
    authored.params.amount = .8f;
    filter.functions = lam::own(&authored);
    element->set_filter_prop(&filter);
    ASSERT_NE(parsedKeyframes("glow{from{filter:none}to{filter:brightness(calc(2 * 2)) blur(10px)}}", "glow"), nullptr);
    setAnimationStyle(element, "animation:glow 1s linear -.5s both paused");
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    ASSERT_NE(filter.functions, nullptr);
    EXPECT_EQ(filter.functions->type, FILTER_BRIGHTNESS);
    EXPECT_FLOAT_EQ(filter.functions->params.amount, 2.5f);
    ASSERT_NE(filter.functions->next, nullptr);
    EXPECT_FLOAT_EQ(filter.functions->next->params.blur_radius, 5.0f);
    ASSERT_NE(scheduler->first, nullptr);
    for (int i = 0; i < 200; i++) css_animation_tick(scheduler->first, .75f);
    EXPECT_FLOAT_EQ(filter.functions->params.amount, 3.25f);
    EXPECT_FLOAT_EQ(filter.functions->next->params.blur_radius, 7.5f);
    EXPECT_TRUE(filter.functions_borrowed);
    EXPECT_FALSE(scheduler->first->layout_changed);
    animation_scheduler_cancel(scheduler, scheduler->first);
    ASSERT_NE(filter.functions, nullptr);
    EXPECT_FLOAT_EQ(filter.functions->params.amount, .8f);
    EXPECT_EQ(filter.functions->next, nullptr);
    EXPECT_FALSE(filter.functions_borrowed);
}

TEST_F(AnimationTickTest, FilterListsUseDiscreteMismatchAndIdentityForShorterTail) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    FilterProp filter = {};
    element->set_filter_prop(&filter);
    ASSERT_NE(parsedKeyframes("filters{0%{filter:brightness(.5)}50%{filter:brightness(1) saturate(3)}"
        "100%{filter:contrast(2)}}", "filters"), nullptr);
    setAnimationStyle(element, "animation:filters 1s linear -.25s both paused");
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    ASSERT_NE(filter.functions, nullptr);
    EXPECT_FLOAT_EQ(filter.functions->params.amount, .75f);
    ASSERT_NE(filter.functions->next, nullptr);
    EXPECT_EQ(filter.functions->next->type, FILTER_SATURATE);
    EXPECT_FLOAT_EQ(filter.functions->next->params.amount, 2.0f);
    css_animation_tick(scheduler->first, .7f);
    EXPECT_EQ(filter.functions->type, FILTER_BRIGHTNESS);
    css_animation_tick(scheduler->first, .8f);
    EXPECT_EQ(filter.functions->type, FILTER_CONTRAST);
    EXPECT_EQ(filter.functions->next, nullptr);
    animation_scheduler_cancel(scheduler, scheduler->first);
    EXPECT_EQ(filter.functions, nullptr);
}

TEST_F(AnimationTickTest, RegisteredCustomAnimationInheritsReachesFilterAndCancelsWithoutGrowth) {
    MockElement parent_mock, child_mock;
    DomElement* parent = createMockElement(&parent_mock);
    DomElement* child = createMockElement(&child_mock);
    child->parent = lam::up(static_cast<DomNode*>(parent));
    registerProperty("--light", "<number>", "1");
    ASSERT_NE(parsedKeyframes("light{from{--light:calc(.1 * 2)}to{--light:1}}", "light"), nullptr);
    setAnimationStyle(parent, "--light:.4;animation:light 1s linear -.5s both paused");
    layout.view = lam::up(static_cast<View*>(parent));
    layout.elmt = lam::up(parent);
    css_animation_resolve(parent, &layout);
    EXPECT_NEAR(customNumber(parent, "--light"), .6, .00001);
    EXPECT_NEAR(customNumber(child, "--light"), .6, .00001);
    CssDeclaration* declaration = css_parse_property_declaration("filter", 6, "brightness(var(--light))", 24, pool);
    ASSERT_NE(declaration, nullptr);
    layout.view = lam::up(static_cast<View*>(child));
    layout.elmt = lam::up(child);
    FilterFunction* filter = resolve_filter_value(&layout, CSS_PROPERTY_FILTER,
        resolve_var_function(&layout, declaration->value), pool);
    ASSERT_NE(filter, nullptr);
    EXPECT_NEAR(filter->params.amount, .6, .00001);
    AnimationInstance* instance = scheduler->first;
    ASSERT_NE(instance, nullptr);
    PoolStats before = {}, after = {};
    pool_get_detailed_stats(pool, &before);
    for (int i = 0; i < 200; i++) css_animation_tick(instance, i % 2 ? .25f : .75f);
    pool_get_detailed_stats(pool, &after);
    EXPECT_LE(after.live_bytes, before.live_bytes + 1024u);
    EXPECT_NEAR(customNumber(parent, "--light"), .4, .00001);
    EXPECT_TRUE(instance->layout_changed);
    animation_scheduler_cancel(scheduler, instance);
    EXPECT_NEAR(customNumber(parent, "--light"), .4, .00001);
}

TEST_F(AnimationTickTest, CustomAnimationKeepsNamesPriorityAndUnregisteredDiscreteValues) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    registerProperty("--Pulse", "<number>", "0");
    registerProperty("--pulse", "<number>", "0");
    ASSERT_NE(parsedKeyframes("custom{from{--Pulse:0;--pulse:10;--locked:0;--raw:10}"
        "to{--Pulse:4;--pulse:30;--locked:1;--raw:20}}", "custom"), nullptr);
    setAnimationStyle(element, "--locked:9!important;animation:custom 1s linear -.25s both paused");
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    EXPECT_DOUBLE_EQ(customNumber(element, "--Pulse"), 1);
    EXPECT_DOUBLE_EQ(customNumber(element, "--pulse"), 15);
    EXPECT_DOUBLE_EQ(customNumber(element, "--locked"), 9);
    EXPECT_DOUBLE_EQ(customNumber(element, "--raw"), 10);
    css_animation_tick(scheduler->first, .75f);
    EXPECT_DOUBLE_EQ(customNumber(element, "--Pulse"), 3);
    EXPECT_DOUBLE_EQ(customNumber(element, "--pulse"), 25);
    EXPECT_DOUBLE_EQ(customNumber(element, "--raw"), 20);
    EXPECT_DOUBLE_EQ(customNumber(element, "--locked"), 9);
    animation_scheduler_cancel(scheduler, scheduler->first);
    EXPECT_DOUBLE_EQ(customNumber(element, "--Pulse"), 0);
    EXPECT_EQ(css_compute_element_custom_property(pool, element, "--raw"), nullptr);
}

TEST_F(AnimationTickTest, RegisteredNeutralEndpointRecapturesAuthoredBaseAfterRestyle) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    registerProperty("--neutral", "<number>", "3");
    ASSERT_NE(parsedKeyframes("neutral{to{--neutral:11}}", "neutral"), nullptr);
    setAnimationStyle(element, "animation:neutral 1s linear -.5s both paused");
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    EXPECT_DOUBLE_EQ(customNumber(element, "--neutral"), 7);
    for (int i = 0; i < 5; i++) css_animation_resolve(element, &layout);
    EXPECT_DOUBLE_EQ(customNumber(element, "--neutral"), 7);
    animation_scheduler_cancel(scheduler, scheduler->first);
    EXPECT_DOUBLE_EQ(customNumber(element, "--neutral"), 3);
}

TEST_F(AnimationTickTest, CommaKeyframeSelectorsPreserveSourceOrderAndTiming) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssKeyframes* frames = parsedKeyframes("group{FROM,100%{opacity:.2}"
        "25%,75%{opacity:.8;animation-timing-function:steps(1)}"
        "75%{opacity:.6}50{opacity:0}101%,50%{opacity:0}}", "group");
    ASSERT_NE(frames, nullptr);
    ASSERT_EQ(frames->stop_count, 5);
    setAnimationStyle(element, "animation:group 1s linear -.5s both paused");
    layout.view = lam::up(static_cast<View*>(element));
    layout.elmt = lam::up(element);
    css_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);
    css_animation_tick(scheduler->first, .75f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .6f);
    css_animation_tick(scheduler->first, 1.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .2f);
}

TEST_F(AnimationTickTest, RegisteredDimensionsInterpolateCanonicalLengthsAndPercentages) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    registerProperty("--offset", "<length-percentage>", "0px");
    registerProperty("--count", "<integer>", "0");
    CssDeclaration* a = css_parse_property_declaration("--offset", 8, "20px", 4, pool);
    CssDeclaration* b = css_parse_property_declaration("--offset", 8, "50%", 3, pool);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    const CssValue* left = css_compute_custom_property_value(pool, element, "--offset", a->value);
    const CssValue* right = css_compute_custom_property_value(pool, element, "--offset", b->value);
    const CssValue* mixed = css_interpolate_custom_property_value(pool, element, "--offset", left, right, .5f);
    CssMathEvaluationContext evaluation = {};
    evaluation.preserve_percentages = true;
    CssMathResult result = css_math_evaluate(mixed, &evaluation);
    ASSERT_EQ(result.type, CSS_MATH_LENGTH_PERCENT);
    EXPECT_DOUBLE_EQ(result.value, 10);
    EXPECT_DOUBLE_EQ(result.percentage, 25);
    a = css_parse_property_declaration("--count", 7, "-2", 2, pool);
    b = css_parse_property_declaration("--count", 7, "3", 1, pool);
    left = css_compute_custom_property_value(pool, element, "--count", a->value);
    right = css_compute_custom_property_value(pool, element, "--count", b->value);
    mixed = css_interpolate_custom_property_value(pool, element, "--count", left, right, .5f);
    result = css_math_evaluate(mixed, &evaluation);
    EXPECT_DOUBLE_EQ(result.value, 1);
}

TEST_F(AnimationTickTest, FractionalIterationEndRestoresFilledEffectAfterRelayout) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssAnimProp config = defaultAnimProp("schedulerFade", 2.0f);
    config.iteration_count = .5;
    AnimationInstance* instance = css_animation_create(scheduler, element, &config,
        opacityKeyframes(), 0.0, pool);
    ASSERT_NE(instance, nullptr);
    animation_scheduler_tick(scheduler, 1.0, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    EXPECT_EQ(instance->play_state, ANIM_PLAY_FINISHED);
    mock.in_line.opacity = 0.0f;
    animation_scheduler_tick(scheduler, 2.0, nullptr, true);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    EXPECT_EQ(scheduler->count, 1);
}

TEST_F(AnimationTickTest, BackwardsFillUsesDirectionAndZeroIterationsUseStart) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssAnimProp config = defaultAnimProp("schedulerFade", 1.0f);
    config.delay = 1.0f;
    config.direction = ANIM_DIR_REVERSE;
    config.fill_mode = ANIM_FILL_BOTH;
    config.iteration_count = 0.0;
    AnimationInstance* instance = css_animation_create(scheduler, element, &config,
        opacityKeyframes(), 0.0, pool);
    ASSERT_NE(instance, nullptr);
    animation_scheduler_tick(scheduler, 0.0, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 1.0f);
    animation_scheduler_tick(scheduler, 1.0, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 1.0f);
    EXPECT_EQ(instance->play_state, ANIM_PLAY_FINISHED);
}

TEST_F(AnimationTickTest, RestyleUpdatesTimingPreservesStartAndCancelsRemovedNames) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    ASSERT_NE(opacityKeyframes(), nullptr);
    setAnimationStyle(element, "animation:schedulerFade 1s linear -.5s both paused");
    css_animation_resolve(element, &layout);
    ASSERT_EQ(scheduler->count, 1);
    AnimationInstance* instance = scheduler->first;
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    scheduler->current_time = 2.0;
    mock.in_line.opacity = 0.0f;
    setAnimationStyle(element, "animation:schedulerFade 2s linear -.5s both paused");
    css_animation_resolve(element, &layout);
    EXPECT_EQ(scheduler->first, instance);
    EXPECT_DOUBLE_EQ(instance->start_time, 0.0);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .25f);
    setAnimationStyle(element, "animation:none");
    css_animation_resolve(element, &layout);
    EXPECT_EQ(scheduler->count, 0);
    doc.state = nullptr;
}

TEST_F(AnimationTickTest, ComponentPriorityMapsLogicalAliasesAndBorderShorthands) {
    const struct { const char* style; CssPropertyCode component; CssPropertyCode winner; } cases[] = {
        {"margin-left:5px; margin-inline-start:2px!important", CSS_PROPERTY_MARGIN_LEFT,
            CSS_PROPERTY_MARGIN_INLINE_START},
        {"direction:rtl; margin-left:5px; margin-inline-start:2px!important",
            CSS_PROPERTY_MARGIN_LEFT, CSS_PROPERTY_MARGIN_LEFT},
        {"direction:rtl; margin-right:5px; margin-inline-start:2px!important",
            CSS_PROPERTY_MARGIN_RIGHT, CSS_PROPERTY_MARGIN_INLINE_START},
        {"writing-mode:vertical-rl; padding-right:5px; padding-block-start:2px!important",
            CSS_PROPERTY_PADDING_RIGHT, CSS_PROPERTY_PADDING_BLOCK_START},
        {"writing-mode:vertical-rl; direction:rtl; padding-bottom:5px; padding-inline-start:2px!important",
            CSS_PROPERTY_PADDING_BOTTOM, CSS_PROPERTY_PADDING_INLINE_START},
        {"writing-mode:vertical-rl; height:40px; inline-size:20px!important",
            CSS_PROPERTY_HEIGHT, CSS_PROPERTY_INLINE_SIZE},
        {"writing-mode:vertical-rl; min-height:40px; min-inline-size:20px!important",
            CSS_PROPERTY_MIN_HEIGHT, CSS_PROPERTY_MIN_INLINE_SIZE},
        {"writing-mode:vertical-rl; max-width:40px; max-block-size:20px!important",
            CSS_PROPERTY_MAX_WIDTH, CSS_PROPERTY_MAX_BLOCK_SIZE},
        {"left:5px; inset-inline:2px!important", CSS_PROPERTY_LEFT, CSS_PROPERTY_INSET_INLINE},
        {"border-left-width:5px; border-inline-start:2px solid red!important",
            CSS_PROPERTY_BORDER_LEFT_WIDTH, CSS_PROPERTY_BORDER_INLINE_START},
        {"border-left-color:blue; border-inline-color:red!important",
            CSS_PROPERTY_BORDER_LEFT_COLOR, CSS_PROPERTY_BORDER_INLINE_COLOR},
        {"border-left-width:5px; border-left-style:solid!important",
            CSS_PROPERTY_BORDER_LEFT_WIDTH, CSS_PROPERTY_BORDER_LEFT_WIDTH},
        {"background-color:red; background:green!important",
            CSS_PROPERTY_BACKGROUND_COLOR, CSS_PROPERTY_BACKGROUND},
        {"width:40px; all:initial!important", CSS_PROPERTY_WIDTH, CSS_PROPERTY_ALL}
    };
    for (const auto& sample : cases) {
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        setAnimationStyle(element, sample.style);
        CssDeclaration* winner = layout_cascaded_physical_declaration(element, sample.component);
        ASSERT_NE(winner, nullptr) << sample.style;
        EXPECT_EQ(winner->property_code, sample.winner) << sample.style;
    }
}

TEST_F(AnimationTickTest, ImportantPriorityRefreshesForCssAndWebEffects) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssKeyframes* keyframes = opacityKeyframes();
    ASSERT_NE(keyframes, nullptr);
    mock.in_line.opacity = .8f;
    setAnimationStyle(element, "opacity:.8!important;animation:schedulerFade 1s linear -.5s both paused");
    css_animation_resolve(element, &layout);
    ASSERT_EQ(scheduler->count, 1);
    AnimationInstance* instance = scheduler->first;
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);
    EXPECT_TRUE(((CssAnimState*)instance->state)->event_started);
    setAnimationStyle(element, "opacity:.8;animation:schedulerFade 1s linear -.5s both paused");
    css_animation_resolve(element, &layout);
    EXPECT_EQ(scheduler->first, instance);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    setAnimationStyle(element, "opacity:.8!important;animation:schedulerFade 1s linear -.5s both paused");
    mock.in_line.opacity = .8f;
    css_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);
    animation_scheduler_tick(scheduler, .75, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);

    CssWebAnimationState* web = css_web_animation_create(element, keyframes, 1000.0, nullptr, pool);
    ASSERT_NE(web, nullptr);
    css_web_animation_set_current_time(web, 500.0);
    css_web_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);
    setAnimationStyle(element, "opacity:.8");
    css_web_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
}

TEST_F(AnimationTickTest, TransitionsRetainPriorityAboveImportantDeclarations) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    setAnimationStyle(element, "opacity:.2!important;transition:opacity 1s linear");
    mock.in_line.opacity = .2f;
    css_transition_resolve(element, &layout);
    setAnimationStyle(element, "opacity:.8!important;transition:opacity 1s linear");
    mock.in_line.opacity = .8f;
    css_transition_resolve(element, &layout);
    ASSERT_EQ(scheduler->count, 1);
    EXPECT_EQ(scheduler->first->type, ANIM_CSS_TRANSITION);
    animation_scheduler_tick(scheduler, .5, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    animation_scheduler_tick(scheduler, 1.0, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);
    EXPECT_EQ(scheduler->count, 0);
}

TEST(AnimationTiming, StepPositionsUseDefinedJumpBoundaries) {
    TimingFunction timing = {};
    timing.type = TIMING_STEPS;
    timing.steps.count = 4;
    timing.steps.position = STEP_JUMP_START;
    EXPECT_FLOAT_EQ(timing_function_eval(&timing, 0.0f), .25f);
    EXPECT_FLOAT_EQ(timing_function_eval(&timing, .25f), .5f);
    timing.steps.position = STEP_JUMP_BOTH;
    EXPECT_FLOAT_EQ(timing_function_eval(&timing, 0.0f), .2f);
    EXPECT_FLOAT_EQ(timing_function_eval(&timing, .25f), .4f);
    timing.steps.position = STEP_JUMP_NONE;
    EXPECT_FLOAT_EQ(timing_function_eval(&timing, .25f), 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(timing_function_eval(&timing, .75f), 1.0f);
    EXPECT_TRUE(css_animation_parse_timing_function_text("steps(4,jump-none)", &timing));
    EXPECT_EQ(timing.steps.position, STEP_JUMP_NONE);
    EXPECT_FALSE(css_animation_parse_timing_function_text("steps(1,jump-none)", &timing));
    EXPECT_FALSE(css_animation_parse_timing_function_text("cubic-bezier(-1,0,1,1)", &timing));
    EXPECT_FALSE(css_animation_parse_timing_function_text("linear trailing", &timing));
}

TEST_F(AnimationTickTest, TransitionListsUseLastMatchRepeatAndPreserveUnknownSlots) {
    const struct { const char* style; float opacity; } cases[] = {
        {"transition:opacity 1s linear -.5s;transition-duration:2s", .25f},
        {"transition:all 1s linear -.5s,opacity 2s linear -.5s", .25f},
        {"transition-property:width,unknown-target,opacity;transition-duration:1s,2s,4s;"
         "transition-delay:-.5s;transition-timing-function:linear", .125f},
        {"transition-property:width,height,min-width,max-width,min-height,max-height,"
         "color,background-color,aspect-ratio,opacity;transition-duration:1s,2s;"
         "transition-delay:-.5s;transition-timing-function:linear", .25f},
        {"transition:none;transition-duration:1s;transition-delay:-.5s", 1.0f},
        {"transition:opacity 2s linear -.5s;transition:opacity 1px", .25f},
        {"transition:opacity -.5s 2s linear", .25f},
        {"--motion:opacity 2s linear -.5s;transition:var(--motion)", .25f}
    };
    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.style);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        setAnimationStyle(element, test_case.style);
        css_transition_resolve(element, &layout);
        mock.in_line.opacity = 1.0f;
        css_transition_resolve(element, &layout);
        EXPECT_FLOAT_EQ(mock.in_line.opacity, test_case.opacity);
        while (scheduler->first) animation_scheduler_cancel(scheduler, scheduler->first);
    }
    doc.state = nullptr;
}

TEST_F(AnimationTickTest, TransitionRestyleRetargetsCancelsAndReleasesCompletedEffects) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    setAnimationStyle(element, "transition:opacity 1s linear");
    css_transition_resolve(element, &layout);
    mock.in_line.opacity = 1.0f;
    css_transition_resolve(element, &layout);
    animation_scheduler_tick(scheduler, .25, nullptr);
    ASSERT_FLOAT_EQ(mock.in_line.opacity, .25f);
    mock.in_line.opacity = .5f;
    // a CSS animation restyle must not overwrite the new transition target.
    css_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    css_transition_resolve(element, &layout);
    animation_scheduler_tick(scheduler, .5, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .3125f);
    animation_scheduler_tick(scheduler, 1.25, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .5f);
    EXPECT_EQ(scheduler->count, 0);
    mock.in_line.opacity = 0.0f;
    css_transition_resolve(element, &layout);
    animation_scheduler_tick(scheduler, 1.5, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .375f);
    mock.in_line.opacity = 0.0f;
    setAnimationStyle(element, "transition:none");
    css_transition_resolve(element, &layout);
    EXPECT_EQ(scheduler->count, 0);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);
}

TEST_F(AnimationTickTest, TransitionComputedListsKeepAuthoredLengthsAndShorthandCascade) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    setAnimationStyle(element,
        "--motion:opacity 1s linear -.5s,width 2s ease;transition:var(--motion);"
        "transition-duration:250ms,3s,5s;transition-property:width,unknown-target,opacity");
    const struct { CssPropertyCode property; const char* expected; } cases[] = {
        {CSS_PROPERTY_TRANSITION_DURATION, "0.25s, 3s, 5s"},
        {CSS_PROPERTY_TRANSITION_DELAY, "-0.5s, 0s"},
        {CSS_PROPERTY_TRANSITION_PROPERTY, "width, unknown-target, opacity"},
        {CSS_PROPERTY_TRANSITION_TIMING_FUNCTION, "linear, ease"}
    };
    for (const auto& test_case : cases) {
        char text[256];
        ASSERT_TRUE(css_prop_serialize_computed(element, test_case.property, 0, text, sizeof(text)));
        EXPECT_STREQ(text, test_case.expected);
    }
}

TEST_F(AnimationTickTest, TransitionDelayHoldsStartAndZeroDurationUsesCombinedTime) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    setAnimationStyle(element, "transition:opacity 0s linear .5s");
    css_transition_resolve(element, &layout);
    mock.in_line.opacity = 1.0f;
    css_transition_resolve(element, &layout);
    ASSERT_EQ(scheduler->count, 1);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);
    animation_scheduler_tick(scheduler, .49, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);
    animation_scheduler_tick(scheduler, .5, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 1.0f);
    EXPECT_EQ(scheduler->count, 0);
    setAnimationStyle(element, "transition:opacity 1s linear -2s");
    mock.in_line.opacity = 0.0f;
    css_transition_resolve(element, &layout);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);
    EXPECT_EQ(scheduler->count, 0);
}

TEST_F(AnimationTickTest, OpacityAnimation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp prop_from = {};
    prop_from.property_code = CSS_PROPERTY_OPACITY;
    prop_from.value_type = ANIM_VAL_FLOAT;
    prop_from.value.f = 0.0f;

    CssAnimatedProp prop_to = {};
    prop_to.property_code = CSS_PROPERTY_OPACITY;
    prop_to.value_type = ANIM_VAL_FLOAT;
    prop_to.value.f = 1.0f;

    CssKeyframeStop stops[2];
    stops[0] = {0.0f, lam::own_arr(&prop_from), 1, NULL};
    stops[1] = {1.0f, lam::own_arr(&prop_to), 1, NULL};

    CssKeyframes kf = {lam::up("testFade"), lam::own_arr(stops), 2};

    CssAnimProp ap = defaultAnimProp("testFade", 1.0f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->type, ANIM_CSS_ANIMATION);

    css_animation_tick(inst, 0.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);

    css_animation_tick(inst, 0.5f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.5f);

    css_animation_tick(inst, 1.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 1.0f);

    Runtime runtime = {};
    runtime_init(&runtime);
    // roots unwind before runtime cleanup, including assertion failures (D5.3.3).
    struct EventRuntimeCleanup {
        Runtime* runtime;
        DomDocument** document;
        ~EventRuntimeCleanup() {
            dom_events_reset();
            dom_set_document(nullptr);
            if (*document) (*document)->js.runtime = nullptr;
            runtime_cleanup(runtime);
        }
    } cleanup = {&runtime, &event_document};
    ASSERT_FALSE(item_is_error(js_interp_execute_source(&runtime, "null;", 5,
        "css-animation-events.js", nullptr)));
    Input* input = Input::create(pool);
    ASSERT_NE(input, nullptr);
    // production event wrappers require the document's lifecycle and style owners.
    event_document = dom_document_create(input);
    ASSERT_NE(event_document, nullptr);
    // samples use the test clock; draining JS events must not advance it by wall time.
    document_state.animation_scheduler = nullptr;
    event_document->state = lam::up(&document_state);
    event_document->js.runtime = &runtime;
    ui.document = lam::up(event_document);
    element = DomElement::create(event_document, "div", nullptr);
    ASSERT_NE(element, nullptr);
    element->in_line = lam::view_ref(&mock.in_line);
    element->set_styles_resolved(true);
    event_document->root = lam::up(element);
    dom_set_document(event_document);
    RootFrame roots(2);
    Rooted<Item> target(roots, dom_wrap_element(element));
    Rooted<Item> callback(roots, js_new_native_function(record_animation_event));
    const char* types[] = {"animationstart", "animationend", "animationiteration", "animationcancel"};
    for (const char* type : types)
        ASSERT_FALSE(item_is_error(dom_add_event_listener(target.get(), js_name_item(type),
            callback.get(), ItemNull)));

    animation_events = {};
    ap.delay = 1.0f;
    ap.fill_mode = ANIM_FILL_BOTH;
    AnimationInstance* delayed = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(delayed, nullptr);
    ((CssAnimState*)delayed->state)->ui_context = &ui;
    animation_instance_sample(delayed, 0.0);
    js_event_loop_drain();
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);
    EXPECT_EQ(animation_events.starts, 0u);
    animation_instance_sample(delayed, 1.5);
    js_event_loop_drain();
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.5f);
    EXPECT_EQ(animation_events.starts, 1u);
    animation_instance_pause(delayed, 1.5);
    animation_scheduler_cancel(scheduler, delayed);
    js_event_loop_drain();
    EXPECT_EQ(animation_events.cancels, 1u);
    EXPECT_DOUBLE_EQ(animation_events.elapsed, 0.5);

    animation_events = {};
    ap.duration = 2.0f;
    ap.delay = -1.5f;
    AnimationInstance* negative = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(negative, nullptr);
    ((CssAnimState*)negative->state)->ui_context = &ui;
    animation_instance_sample(negative, 0.0);
    js_event_loop_drain();
    EXPECT_EQ(animation_events.starts, 1u);
    EXPECT_DOUBLE_EQ(animation_events.elapsed, 1.5);
    animation_scheduler_cancel(scheduler, negative);
    js_event_loop_drain();

    animation_events = {};
    ap.duration = 0.0f;
    ap.delay = 0.0f;
    ap.fill_mode = ANIM_FILL_NONE;
    AnimationInstance* instant = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(instant, nullptr);
    ((CssAnimState*)instant->state)->ui_context = &ui;
    animation_instance_sample(instant, 0.0);
    css_animation_finish(instant);
    js_event_loop_drain();
    EXPECT_EQ(animation_events.starts, 1u);
    EXPECT_EQ(animation_events.ends, 1u);
    EXPECT_EQ(animation_events.iterations, 0u);
    EXPECT_DOUBLE_EQ(animation_events.elapsed, 0.0);

    for (const char* type : types)
        dom_remove_event_listener(target.get(), js_name_item(type), callback.get(), ItemNull);
    event_document->js_has_dom_realm = false;
    // pointerup collides with animationstart; doom_blur with animationiteration.
    // A prefilter hit alone must not retain tasks for either unobserved event.
    TemplateHandlerEntry collision_handlers[2] = {};
    collision_handlers[0].event_name = "pointerup";
    collision_handlers[0].next = &collision_handlers[1];
    collision_handlers[1].event_name = "doom_blur";
    TemplateEntry collision_entry = {};
    collision_entry.handlers = collision_handlers;
    collision_entry.handler_event_mask = UINT64_MAX;
    TemplateRegistry collision_registry = {};
    collision_registry.first = &collision_entry;
    collision_registry.author_event_mask = UINT64_MAX;
    TemplateRegistry* saved_registry = g_template_registry;
    struct RestoreRegistry {
        TemplateRegistry* saved;
        ~RestoreRegistry() { g_template_registry = saved; }
    } restore_registry = {saved_registry};
    g_template_registry = &collision_registry;
    EXPECT_TRUE(template_registry_has_author_handler(g_template_registry, "pointerup"));
    EXPECT_FALSE(template_registry_has_author_handler(g_template_registry, "animationstart"));
    ap.duration = .25f;
    ap.iteration_count = -1;
    AnimationInstance* unobserved = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(unobserved, nullptr);
    ((CssAnimState*)unobserved->state)->ui_context = &ui;
    animation_instance_sample(unobserved, 1.0);
    js_event_loop_drain();
    size_t before_bytes = 0, before_count = 0;
    pool_get_stats(context->pool, &before_bytes, &before_count);
    for (unsigned i = 1; i <= 2048; i++) {
        animation_instance_sample(unobserved, 1.0 + i * .25);
        js_event_loop_drain();
    }
    size_t after_bytes = 0, after_count = 0;
    pool_get_stats(context->pool, &after_bytes, &after_count);
    // D4.5.1v4: unobserved Lambda-document events retain no timer shapes.
    EXPECT_EQ(after_bytes, before_bytes);
    EXPECT_EQ(after_count, before_count);
}

TEST_F(AnimationTickTest, ColorAnimation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp prop_from = {};
    memset(&prop_from, 0, sizeof(prop_from));
    prop_from.property_code = CSS_PROPERTY_COLOR;
    prop_from.value_type = ANIM_VAL_COLOR;
    prop_from.value.color.r = 255; prop_from.value.color.a = 255;

    CssAnimatedProp prop_to = {};
    memset(&prop_to, 0, sizeof(prop_to));
    prop_to.property_code = CSS_PROPERTY_COLOR;
    prop_to.value_type = ANIM_VAL_COLOR;
    prop_to.value.color.b = 255; prop_to.value.color.a = 255;

    CssKeyframeStop stops[2];
    stops[0] = {0.0f, lam::own_arr(&prop_from), 1, NULL};
    stops[1] = {1.0f, lam::own_arr(&prop_to), 1, NULL};

    CssKeyframes kf = {lam::up("colorAnim"), lam::own_arr(stops), 2};

    CssAnimProp ap = defaultAnimProp("colorAnim", 1.0f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    css_animation_tick(inst, 0.5f);
    EXPECT_EQ(mock.in_line.color.r, 128);
    EXPECT_EQ(mock.in_line.color.g, 0);
    EXPECT_EQ(mock.in_line.color.b, 128);
    EXPECT_EQ(mock.in_line.color.a, 255);
}

TEST_F(AnimationTickTest, NeutralOpacityAndColorEndpointsKeepUnderlyingValues) {
    const struct {
        const char* rule;
        float opacity;
        Color color;
        Color background;
    } cases[] = {
        {"neutral{to{opacity:1}}", .6f, {0xFF008000}, {0xFF008000}},
        {"neutral{from{opacity:0}}", .1f, {0xFF008000}, {0xFF008000}},
        {"neutral{50%{opacity:1}}", 1.0f, {0xFF008000}, {0xFF008000}},
        {"neutral{to{color:blue}}", .2f, {0xFF804000}, {0xFF008000}},
        {"neutral{from{color:red}}", .2f, {0xFF004080}, {0xFF008000}},
        {"neutral{to{background-color:blue}}", .2f, {0xFF008000}, {0xFF804000}},
        {"neutral{from{background-color:red}}", .2f, {0xFF008000}, {0xFF004080}},
        {"neutral{from{opacity:0}to{background-color:blue}}", .1f,
            {0xFF008000}, {0xFF804000}}
    };
    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.rule);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        mock.in_line.opacity = .2f;
        mock.in_line.color = Color{0xFF008000};
        mock.in_line.has_color = true;
        BoundaryProp boundary = {};
        BackgroundProp background = {};
        background.color = Color{0xFF008000};
        boundary.background = lam::own(&background);
        element->bound = lam::view_prop(&boundary);
        CssAnimProp config = defaultAnimProp("neutral", 1.0f);
        AnimationInstance* instance = css_animation_create(scheduler, element, &config,
            parsedKeyframes(test_case.rule, "neutral"), 0.0, pool);
        ASSERT_NE(instance, nullptr);
        for (int repeat = 0; repeat < 2; repeat++) {
            css_animation_tick(instance, .5f);
            EXPECT_FLOAT_EQ(mock.in_line.opacity, test_case.opacity);
            EXPECT_EQ(mock.in_line.color.c, test_case.color.c);
            EXPECT_EQ(background.color.c, test_case.background.c);
        }
        animation_scheduler_cancel(scheduler, instance);
    }
}

TEST_F(AnimationTickTest, MissingStopsUsePropertyIntervalsAndInteriorNeutralEndpoints) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    BlockProp sizing = {};
    sizing.given_width = 80.0f;
    element->blk = lam::view_prop(&sizing);
    mock.in_line.opacity = .8f;
    CssAnimProp config = defaultAnimProp("sparse", 1.0f);
    setAnimationStyle(element, "animation:sparse 1s linear forwards");
    AnimationInstance* instance = css_animation_create(scheduler, element, &config,
        parsedKeyframes("sparse{0%{opacity:0}25%{width:40px}75%{width:60px}100%{opacity:1}}",
            "sparse"), 0.0, pool);
    ASSERT_NE(instance, nullptr);
    css_animation_resolve(element, &layout);
    const struct { float progress, opacity, width; } samples[] = {
        {.125f, .125f, 60.0f}, {.5f, .5f, 50.0f}, {.875f, .875f, 70.0f}
    };
    for (const auto& sample : samples) {
        css_animation_tick(instance, sample.progress);
        EXPECT_FLOAT_EQ(mock.in_line.opacity, sample.opacity);
        EXPECT_FLOAT_EQ(sizing.given_width, sample.width);
    }
}

TEST_F(AnimationTickTest, CssEasingAppliesWithinEachPropertyInterval) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssAnimProp config = defaultAnimProp("interval", 1.0f);
    config.timing = TIMING_EASE_IN;
    AnimationInstance* instance = css_animation_create(scheduler, element, &config,
        parsedKeyframes("interval{from{opacity:0}50%{opacity:.8;animation-timing-function:ease-out}"
            "to{opacity:1}}", "interval"), 0.0, pool);
    ASSERT_NE(instance, nullptr);
    animation_scheduler_tick(scheduler, .25, nullptr);
    EXPECT_NEAR(mock.in_line.opacity, .8f * timing_function_eval(&TIMING_EASE_IN, .5f), .00001f);
    animation_scheduler_tick(scheduler, .5, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);
    animation_scheduler_tick(scheduler, .75, nullptr);
    EXPECT_NEAR(mock.in_line.opacity, .8f + .2f * timing_function_eval(&TIMING_EASE_OUT, .5f), .00001f);
}

TEST_F(AnimationTickTest, LengthCompositionUsesAuthoredEndpointsBeforeInterpolation) {
    const struct { const char* rule; float expected; } cases[] = {
        {"compose{to{width:20px;animation-composition:add}}", 50.0f},
        {"compose{from{width:20px;animation-composition:add}}", 50.0f},
        {"compose{from{width:10px;animation-composition:add}to{width:20px}}", 35.0f}
    };
    for (const auto& sample : cases) {
        SCOPED_TRACE(sample.rule);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        BlockProp sizing = {};
        sizing.given_width = 40.0f;
        element->blk = lam::view_prop(&sizing);
        CssAnimProp config = defaultAnimProp("compose", 1.0f);
        setAnimationStyle(element, "animation:compose 1s linear forwards");
        AnimationInstance* instance = css_animation_create(scheduler, element, &config,
            parsedKeyframes(sample.rule, "compose"), 0.0, pool);
        ASSERT_NE(instance, nullptr);
        css_animation_resolve(element, &layout);
        css_animation_tick(instance, .5f);
        EXPECT_FLOAT_EQ(sizing.given_width, sample.expected);
        animation_scheduler_cancel(scheduler, instance);
    }
}

TEST_F(AnimationTickTest, EqualOffsetsCascadePropertyAndTimingDescriptors) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssAnimProp config = defaultAnimProp("cascade", 1.0f);
    config.timing = TIMING_EASE_IN;
    AnimationInstance* instance = css_animation_create(scheduler, element, &config,
        parsedKeyframes("cascade{from{opacity:0}50%{opacity:.7}50%{opacity:.8}"
            "50%{animation-timing-function:ease-out;animation-timing-function:linear!important}"
            "to{opacity:1}}", "cascade"), 0.0, pool);
    ASSERT_NE(instance, nullptr);
    animation_scheduler_tick(scheduler, .5, nullptr);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, .8f);
    animation_scheduler_tick(scheduler, .75, nullptr);
    EXPECT_NEAR(mock.in_line.opacity, .8f + .2f * timing_function_eval(&TIMING_EASE_OUT, .5f), .00001f);
}

TEST_F(AnimationTickTest, WebEasingPrecedesPropertyIntervalSampling) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    CssWebAnimationState* effect = css_web_animation_create(element,
        parsedKeyframes("web{from{opacity:0}50%{opacity:.8}to{opacity:1}}", "web"),
        1000.0, &TIMING_EASE_IN, pool);
    ASSERT_NE(effect, nullptr);
    css_web_animation_set_current_time(effect, 500.0);
    css_web_animation_resolve(element, &layout);
    EXPECT_NEAR(mock.in_line.opacity, 1.6f * timing_function_eval(&TIMING_EASE_IN, .5f), .00001f);
}

TEST_F(AnimationTickTest, NeutralTransformsCopyUnderlyingListsAndPadWithIdentity) {
    const struct { bool has_base, ends_none; } cases[] = {
        {false, false}, {true, false}, {false, true}, {true, true}
    };
    for (const auto& sample : cases) {
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        TransformProp transform = {};
        TransformFunction source = {};
        source.type = TRANSFORM_TRANSLATEX;
        source.params.translate.x = 10.0f;
        source.translate_x_percent = source.translate_y_percent = NAN;
        if (sample.has_base) transform.functions = lam::shared(&source);
        element->transform = lam::view_prop(&transform);
        CssAnimProp config = defaultAnimProp("neutralTransform", 1.0f);
        AnimationInstance* instance = css_animation_create(scheduler, element, &config,
            parsedKeyframes(sample.ends_none
                ? "neutralTransform{from{transform:translateX(20px) scale(3)}to{transform:none}}"
                : "neutralTransform{to{transform:translateX(20px) scale(3)}}",
                "neutralTransform"), 0.0, pool);
        ASSERT_NE(instance, nullptr);
        // retained snapshots must survive mutation or replacement of view-owned functions.
        source.params.translate.x = 500.0f;
        css_animation_tick(instance, .5f);
        ASSERT_NE(transform.functions, nullptr);
        EXPECT_FLOAT_EQ(transform.functions->params.translate.x,
            sample.has_base && !sample.ends_none ? 15.0f : 10.0f);
        ASSERT_NE(transform.functions->next, nullptr);
        EXPECT_FLOAT_EQ(transform.functions->next->params.scale.x, 2.0f);
        EXPECT_FLOAT_EQ(transform.functions->next->params.scale.y, 2.0f);
        EXPECT_EQ(transform.functions_owner, TRANSFORM_FUNCTIONS_DOCUMENT_POOL);
        animation_scheduler_cancel(scheduler, instance);
    }
}

TEST_F(AnimationTickTest, TypedTransformEndpointsKeepUnitsAndPercentageComponents) {
    const struct { const char* rule; float x, scale; } cases[] = {
        {"from{transform:translateX(0%)}to{transform:translateX(100%)}", 20.0f, 1.0f},
        {"from{transform:translateX(0em)}to{transform:translateX(4em)}", 20.0f, 1.0f},
        {"from{transform:translateX(0px)}to{transform:translateX(var(--end))}", 20.0f, 1.0f},
        {"from{transform:scale(50%)}to{transform:scale(150%)}", 0.0f, 1.0f},
        {"from{transform:translateX(10px)}to{transform:translateX(100%)}", 25.0f, 1.0f},
        {"from{transform:translateX(1in)}to{transform:translateX(2in)}", 144.0f, 1.0f}
    };
    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.rule);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        TransformProp transform = {};
        FontProp font = {};
        font.font_size = 10.0f;
        setTransformContext(element, &transform, &font);
        StrBuf* content = strbuf_new();
        strbuf_append_format(content, "typedTransform{%s}", test_case.rule);
        ASSERT_NE(parsedKeyframes(content->str, "typedTransform"), nullptr);
        strbuf_free(content);
        setAnimationStyle(element, "--end:40px;animation:typedTransform 1s linear -.5s both paused");
        css_animation_resolve(element, &layout);
        ASSERT_NE(transform.functions, nullptr);
        RdtMatrix matrix = radiant::compute_transform_matrix(transform.functions,
            element->width, element->height, 0.0f, 0.0f);
        EXPECT_FLOAT_EQ(matrix.e13, test_case.x);
        EXPECT_FLOAT_EQ(matrix.e11, test_case.scale);
        while (scheduler->first) animation_scheduler_cancel(scheduler, scheduler->first);
    }
}

TEST_F(AnimationTickTest, TransformMathSpatialPrimitivesAndMatricesSampleComputedValues) {
    const struct {
        const char* from; const char* to;
        float x, y, xx, yy, zz, xy, yx, yz, zy;
    } cases[] = {
        {"scale(calc(0%))", "scale(calc(100%))", 0,0,.5f,.5f,1,0,0,0,0},
        {"rotate(calc(0deg))", "rotate(calc(.25turn))", 0,0,.7071068f,.7071068f,1,-.7071068f,.7071068f,0,0},
        {"translate3d(0px,0px,0px)", "translate3d(40px,20px,10px)", 20,10,1,1,1,0,0,0,0},
        {"scale3d(1,1,1)", "scale3d(3,3,3)", 0,0,2,2,2,0,0,0,0},
        {"rotateX(0deg)", "rotateX(90deg)", 0,0,1,.7071068f,.7071068f,0,0,-.7071068f,.7071068f},
        {"translateX(10px)", "translateY(20px)", 5,10,1,1,1,0,0,0,0},
        {"matrix(1,0,0,1,0,0)", "matrix(3,0,0,3,0,0)", 0,0,2,2,1,0,0,0,0},
        {"rotate(0deg)", "translateX(40px)", 20,0,1,1,1,0,0,0,0},
        {"translateX(calc(1em + 1in))", "translateX(calc(3em + 2in))", 164,0,1,1,1,0,0,0,0},
        {"scaleX(1)", "scaleY(3)", 0,0,1,2,1,0,0,0,0},
        {"rotate(0deg) translateX(0px)", "translateX(40px) scale(3)", 20,0,2,2,1,0,0,0,0}
    };
    for (const auto& sample : cases) {
        SCOPED_TRACE(sample.to);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        TransformProp transform = {};
        FontProp font = {};
        font.font_size = 10.0f;
        setTransformContext(element, &transform, &font);
        StrBuf* content = strbuf_new();
        strbuf_append_format(content, "spatial{from{transform:%s}to{transform:%s}}", sample.from, sample.to);
        ASSERT_NE(parsedKeyframes(content->str, "spatial"), nullptr);
        strbuf_free(content);
        setAnimationStyle(element, "animation:spatial 1s linear -.5s both paused");
        css_animation_resolve(element, &layout);
        RdtMatrix4 matrix = radiant::compute_transform_matrix_3d(transform.functions, 40.0f, 10.0f, 0.0f, 0.0f);
        const int indices[] = {3,7,0,5,10,1,4,6,9};
        const float expected[] = {sample.x,sample.y,sample.xx,sample.yy,sample.zz,
            sample.xy,sample.yx,sample.yz,sample.zy};
        for (int index = 0; index < 9; index++)
            EXPECT_NEAR(matrix.values[indices[index]], expected[index], .00001f);
        if (strcmp(sample.to, "translate3d(40px,20px,10px)") == 0)
            EXPECT_FLOAT_EQ(matrix.values[11], 5.0f);
        EXPECT_NEAR(scheduler->last->bounds[0], sample.x + fminf(0.0f, sample.xx * 40.0f) +
            fminf(0.0f, sample.xy * 10.0f), .00001f);
        EXPECT_NEAR(scheduler->last->bounds[1], sample.y + fminf(0.0f, sample.yx * 40.0f) +
            fminf(0.0f, sample.yy * 10.0f), .00001f);
        EXPECT_NEAR(scheduler->last->bounds[2], fabsf(sample.xx) * 40.0f + fabsf(sample.xy) * 10.0f, .00001f);
        EXPECT_NEAR(scheduler->last->bounds[3], fabsf(sample.yx) * 40.0f + fabsf(sample.yy) * 10.0f, .00001f);
        while (scheduler->first) animation_scheduler_cancel(scheduler, scheduler->first);
    }
}

TEST_F(AnimationTickTest, SpatialRotationPerspectiveAndMatrixPairsInterpolateTheirOwnFunctions) {
    const struct { const char* from; const char* to; const char* expected; } cases[] = {
        {"rotate3d(1,1,0,0deg)", "rotate3d(1,1,0,90deg)", "rotate3d(1,1,0,45deg)"},
        {"rotateX(90deg)", "rotateY(90deg)", "matrix3d(.66666667,.33333333,-.66666667,0,.33333333,.66666667,.66666667,0,.66666667,-.66666667,.33333333,0,0,0,0,1)"},
        {"rotate3d(1,0,0,30deg)", "rotate3d(2,0,0,90deg)", "rotateX(60deg)"},
        {"rotate(0deg)", "rotate3d(0,0,1,360deg)", "rotate(180deg)"},
        {"none", "rotate3d(1,1,0,360deg)", "rotate3d(1,1,0,180deg)"},
        {"rotateX(0deg)", "rotate3d(2,0,0,360deg)", "rotateX(180deg)"},
        {"rotate3d(.00000001,.00000001,0,0deg)", "rotate3d(.00000001,.00000001,0,90deg)", "rotate3d(1,1,0,45deg)"},
        {"rotate3d(1e30,1e30,0,0deg)", "rotate3d(1e30,1e30,0,90deg)", "rotate3d(1,1,0,45deg)"},
        {"rotate3d(0,0,0,90deg)", "rotateX(90deg)", "rotateX(45deg)"},
        {"rotate3d(-1,0,0,30deg)", "rotate3d(-2,0,0,90deg)", "rotateX(-60deg)"},
        {"rotateX(30deg)", "rotate3d(-1,0,0,90deg)", "rotateX(-30deg)"},
        {"perspective(100px)", "perspective(200px)", "perspective(133.333333px)"},
        {"perspective(none)", "perspective(200px)", "perspective(400px)"},
        {"none", "perspective(200px)", "perspective(400px)"},
        {"perspective(0px)", "perspective(200px)", "perspective(1.99004975px)"},
        {"perspective(.5px)", "perspective(200px)", "perspective(1.99004975px)"},
        {"matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)", "matrix3d(1,0,0,0,0,1,0,0,0,0,3,0,0,0,0,1)", "scale3d(1,1,2)"},
        {"rotateX(0deg)", "translateZ(40px)", "translateZ(20px)"},
        {"matrix(1,0,0,1,0,0) rotate(0deg)", "matrix(3,0,0,3,0,0) rotate(360deg)", "scale(2) rotate(180deg)"},
        {"perspective(100px) rotateX(0deg)", "perspective(200px) rotateX(360deg)", "perspective(133.333333px) rotateX(180deg)"},
        {"rotateX(90deg) rotate(0deg)", "rotateY(90deg) rotate(360deg)", "matrix3d(.66666667,.33333333,-.66666667,0,.33333333,.66666667,.66666667,0,.66666667,-.66666667,.33333333,0,0,0,0,1) rotate(180deg)"}
    };
    for (const auto& sample : cases) {
        SCOPED_TRACE(sample.to);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        TransformProp transform = {};
        FontProp font = {};
        setTransformContext(element, &transform, &font);
        StrBuf* content = strbuf_new();
        strbuf_append_format(content, "spatialPair{from{transform:%s}to{transform:%s}}", sample.from, sample.to);
        ASSERT_NE(parsedKeyframes(content->str, "spatialPair"), nullptr);
        strbuf_free(content);
        setAnimationStyle(element, "animation:spatialPair 1s linear -.5s both paused");
        css_animation_resolve(element, &layout);
        ASSERT_NE(transform.functions, nullptr);
        CssDeclaration* expected_declaration = css_parse_property_declaration("transform", 9,
            sample.expected, strlen(sample.expected), pool);
        ASSERT_NE(expected_declaration, nullptr);
        TransformFunction* expected_functions = resolve_transform_value(&layout, expected_declaration->value, pool);
        ASSERT_NE(expected_functions, nullptr);
        RdtMatrix4 expected = radiant::compute_transform_matrix_3d(expected_functions, 40.0f, 10.0f, 0.0f, 0.0f);
        RdtMatrix4 actual = radiant::compute_transform_matrix_3d(transform.functions, 40.0f, 10.0f, 0.0f, 0.0f);
        for (int coefficient = 0; coefficient < 16; coefficient++)
            EXPECT_NEAR(actual.values[coefficient], expected.values[coefficient], .00001f) << coefficient;
        radiant::destroy_transform_list(pool, expected_functions);
        while (scheduler->first) animation_scheduler_cancel(scheduler, scheduler->first);
    }
}

TEST_F(AnimationTickTest, PercentageMathAndMatrixSuffixesFollowTheUsedReferenceBoxWithoutGrowingSamples) {
    const struct { const char* from; const char* to; float x, y, enlarged_x, enlarged_y; } cases[] = {
        {"translateX(calc(10px + 25%))", "translateX(calc(30px + 75%))", 40,0,60,0},
        {"translateX(min(100%,30px))", "translateX(max(50%,10px))", 25,0,35,0},
        {"translate3d(0px,0px,0px)", "translate3d(calc(50% + 1em),calc(50% + 1em),0px)", 15,7.5f,25,15},
        {"rotate(0deg)", "translateX(100%)", 20,0,40,0},
        {"translateX(calc(100% - 1em)) rotate(0deg)", "scale(2) translateX(50%)", 35,0,75,0},
        {"rotateX(0deg)", "translate3d(100%,50%,40px)", 20,2.5f,40,10}
    };
    for (const auto& sample : cases) {
        SCOPED_TRACE(sample.to);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        TransformProp transform = {};
        FontProp font = {};
        font.font_size = 10.0f;
        setTransformContext(element, &transform, &font);
        // effects resolve before initial layout has assigned the element's dimensions.
        element->width = element->height = 0.0f;
        StrBuf* content = strbuf_new();
        strbuf_append_format(content, "referenceBox{from{transform:%s}to{transform:%s}}", sample.from, sample.to);
        ASSERT_NE(parsedKeyframes(content->str, "referenceBox"), nullptr);
        strbuf_free(content);
        setAnimationStyle(element, "animation:referenceBox 1s linear -.5s both paused");
        css_animation_resolve(element, &layout);
        ASSERT_NE(scheduler->last, nullptr);
        RdtMatrix4 matrix = radiant::compute_transform_matrix_3d(transform.functions, 40.0f, 10.0f, 0.0f, 0.0f);
        EXPECT_FLOAT_EQ(matrix.values[3], sample.x);
        EXPECT_FLOAT_EQ(matrix.values[7], sample.y);
        matrix = radiant::compute_transform_matrix_3d(transform.functions, 80.0f, 40.0f, 0.0f, 0.0f);
        EXPECT_FLOAT_EQ(matrix.values[3], sample.enlarged_x);
        EXPECT_FLOAT_EQ(matrix.values[7], sample.enlarged_y);
        Pool* snapshot_pool = pool_create();
        ASSERT_NE(snapshot_pool, nullptr);
        TransformFunction* snapshot = radiant::clone_transform_list(snapshot_pool, transform.functions);
        ASSERT_NE(snapshot, nullptr);
        PoolStats before, after;
        pool_get_detailed_stats(pool, &before);
        for (int tick = 0; tick < 200; tick++) css_animation_tick(scheduler->last, .5f);
        pool_get_detailed_stats(pool, &after);
        EXPECT_EQ(after.live_bytes, before.live_bytes);
        EXPECT_EQ(after.allocation_count - after.free_count, before.allocation_count - before.free_count);
        while (scheduler->first) animation_scheduler_cancel(scheduler, scheduler->first);
        matrix = radiant::compute_transform_matrix_3d(snapshot, 80.0f, 40.0f, 0.0f, 0.0f);
        EXPECT_FLOAT_EQ(matrix.values[3], sample.enlarged_x);
        EXPECT_FLOAT_EQ(matrix.values[7], sample.enlarged_y);
        radiant::destroy_transform_list(snapshot_pool, snapshot);
        pool_get_detailed_stats(snapshot_pool, &after);
        EXPECT_EQ(after.live_bytes, 0u);
        pool_destroy(snapshot_pool);
    }
}

TEST_F(AnimationTickTest, ComputedTransformMathSnapshotsSurviveTheirSourcePool) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    TransformProp transform = {};
    FontProp font = {};
    font.font_size = 10.0f;
    setTransformContext(element, &transform, &font);
    Pool* source_pool = pool_create();
    ASSERT_NE(source_pool, nullptr);
    const char* value = "translateX(calc(1em + 50%))";
    CssDeclaration* declaration = css_parse_property_declaration("transform", 9, value, strlen(value), source_pool);
    ASSERT_NE(declaration, nullptr);
    TransformFunction* source = resolve_transform_value(&layout, declaration->value, source_pool);
    ASSERT_NE(source, nullptr);
    TransformFunction* snapshot = radiant::clone_transform_list(pool, source);
    ASSERT_NE(snapshot, nullptr);
    pool_destroy(source_pool);
    RdtMatrix4 matrix = radiant::compute_transform_matrix_3d(snapshot, 80.0f, 10.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(matrix.values[3], 50.0f);
    radiant::destroy_transform_list(pool, snapshot);
}

TEST_F(AnimationTickTest, SingularMatrixSuffixSelectsTheEntireTransformDiscretely) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    TransformProp transform = {};
    FontProp font = {};
    font.font_size = 10.0f;
    setTransformContext(element, &transform, &font);
    ASSERT_NE(parsedKeyframes("singular{from{transform:rotate(0deg) matrix(0,0,0,0,0,0)}"
        "to{transform:rotate(90deg) matrix(1,0,0,1,10,20)}}", "singular"), nullptr);
    setAnimationStyle(element, "animation:singular 1s linear -.5s both paused");
    css_animation_resolve(element, &layout);
    ASSERT_NE(scheduler->last, nullptr);
    const float progresses[] = {.25f, .5f, .75f};
    for (float progress : progresses) {
        SCOPED_TRACE(progress);
        css_animation_tick(scheduler->last, progress);
        RdtMatrix4 sampled = radiant::compute_transform_matrix_3d(transform.functions,
            40.0f, 10.0f, 0.0f, 0.0f);
        EXPECT_NEAR(sampled.values[0], 0.0f, .00001f);
        EXPECT_NEAR(sampled.values[5], 0.0f, .00001f);
        EXPECT_NEAR(sampled.values[1], progress < .5f ? 0.0f : -1.0f, .00001f);
        EXPECT_NEAR(sampled.values[4], progress < .5f ? 0.0f : 1.0f, .00001f);
        EXPECT_NEAR(sampled.values[3], progress < .5f ? 0.0f : -20.0f, .00001f);
        EXPECT_NEAR(sampled.values[7], progress < .5f ? 0.0f : 10.0f, .00001f);
    }
}

TEST_F(AnimationTickTest, TransformValueCachesRefreshPerElementAndOwnInheritedLists) {
    CssKeyframes* keyframes = parsedKeyframes(
        "transformContext{from{transform:translateX(1em)}to{transform:var(--motion)}}", "transformContext");
    ASSERT_NE(keyframes, nullptr);
    MockElement mocks[2];
    FontProp fonts[2] = {};
    TransformProp transforms[2] = {};
    DomElement* elements[2] = {};
    AnimationInstance* instances[2] = {};
    for (int index = 0; index < 2; index++) {
        elements[index] = createMockElement(&mocks[index]);
        elements[index]->font = lam::view_prop(&fonts[index]);
        elements[index]->transform = lam::view_prop(&transforms[index]);
        fonts[index].font_size = index == 0 ? 10.0f : 20.0f;
        layout.view = lam::up(static_cast<View*>(elements[index]));
        layout.elmt = lam::up(elements[index]);
        layout.font.style = lam::up(&fonts[index]);
        layout.font.current_font_size = fonts[index].font_size;
        setAnimationStyle(elements[index],
            "--motion:translateX(4em);animation:transformContext 1s linear -.5s both paused");
        css_animation_resolve(elements[index], &layout);
        instances[index] = scheduler->last;
        ASSERT_NE(transforms[index].functions, nullptr);
        EXPECT_FLOAT_EQ(transforms[index].functions->params.translate.x, index == 0 ? 25.0f : 50.0f);
    }
    CssAnimState* state = (CssAnimState*)instances[0]->state;
    CssAnimValueSample* retained = state->value_samples;
    ASSERT_EQ(state->value_sample_count, 2);
    fonts[0].font_size = 12.0f;
    layout.view = lam::up(static_cast<View*>(elements[0]));
    layout.elmt = lam::up(elements[0]);
    layout.font.style = lam::up(&fonts[0]);
    layout.font.current_font_size = fonts[0].font_size;
    setAnimationStyle(elements[0],
        "--motion:translateX(6em);animation:transformContext 1s linear -.5s both paused");
    css_animation_resolve(elements[0], &layout);
    EXPECT_EQ(state->value_samples.get(), retained);
    EXPECT_FLOAT_EQ(transforms[0].functions->params.translate.x, 42.0f);
    css_animation_tick(instances[1], .5f);
    EXPECT_FLOAT_EQ(transforms[1].functions->params.translate.x, 50.0f);
    // invalid computed syntax defaults to none, rather than exposing an earlier declaration.
    setAnimationStyle(elements[0],
        "--motion:translateX(1deg);animation:transformContext 1s linear -.5s both paused");
    css_animation_resolve(elements[0], &layout);
    EXPECT_FLOAT_EQ(transforms[0].functions->params.translate.x, 6.0f);
    elements[0]->parent = lam::up(static_cast<DomNode*>(elements[1]));
    ASSERT_NE(parsedKeyframes("transformInherited{from{transform:translateX(1em)}"
        "to{transform:inherit}}", "transformInherited"), nullptr);
    setAnimationStyle(elements[0], "animation:transformInherited 1s linear -.5s both paused");
    css_animation_resolve(elements[0], &layout);
    instances[0] = scheduler->last;
    state = (CssAnimState*)instances[0]->state;
    ASSERT_NE(state->value_samples[1].computed.value.transform, nullptr);
    EXPECT_NE(state->value_samples[1].computed.value.transform, transforms[1].functions);
    EXPECT_FLOAT_EQ(transforms[0].functions->params.translate.x, 31.0f);
    transforms[1].functions->params.translate.x = 500.0f;
    css_animation_tick(instances[0], .5f);
    EXPECT_FLOAT_EQ(transforms[0].functions->params.translate.x, 31.0f);
    EXPECT_NE(keyframes->stops[0].properties[0].expression, nullptr);
    EXPECT_NE(keyframes->stops[1].properties[0].expression, nullptr);
}

TEST_F(AnimationTickTest, PhysicalSidesApplyColorAndLengthSamplesToConsumerFields) {
    const CssPropertyCode families[][4] = {
        {CSS_PROPERTY_BORDER_TOP_COLOR, CSS_PROPERTY_BORDER_RIGHT_COLOR,
         CSS_PROPERTY_BORDER_BOTTOM_COLOR, CSS_PROPERTY_BORDER_LEFT_COLOR},
        {CSS_PROPERTY_BORDER_TOP_WIDTH, CSS_PROPERTY_BORDER_RIGHT_WIDTH,
         CSS_PROPERTY_BORDER_BOTTOM_WIDTH, CSS_PROPERTY_BORDER_LEFT_WIDTH},
        {CSS_PROPERTY_TOP, CSS_PROPERTY_RIGHT, CSS_PROPERTY_BOTTOM, CSS_PROPERTY_LEFT},
        {CSS_PROPERTY_MARGIN_TOP, CSS_PROPERTY_MARGIN_RIGHT,
         CSS_PROPERTY_MARGIN_BOTTOM, CSS_PROPERTY_MARGIN_LEFT},
        {CSS_PROPERTY_PADDING_TOP, CSS_PROPERTY_PADDING_RIGHT,
         CSS_PROPERTY_PADDING_BOTTOM, CSS_PROPERTY_PADDING_LEFT}
    };
    for (size_t family = 0; family < sizeof(families) / sizeof(families[0]); family++) {
        for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
            MockElement mock;
            DomElement* element = createMockElement(&mock);
            BoundaryProp boundary = {};
            BorderProp border = {};
            PositionProp position = {};
            element->bound = lam::view_prop(&boundary);
            boundary.border = lam::own(&border);
            element->position = lam::view_prop(&position);
            CssAnimatedProp from = {};
            CssAnimatedProp to = {};
            from.property_code = to.property_code = families[family][side];
            from.value_type = to.value_type = family == 0 ? ANIM_VAL_COLOR : ANIM_VAL_LENGTH;
            if (family == 0) {
                from.value.color.r = to.value.color.b = 255;
                from.value.color.a = to.value.color.a = 255;
            } else {
                from.value.length.value = family == 1 ? 2.0f : family == 3 ? -10.0f : 0.0f;
                to.value.length.value = family == 1 ? 10.0f : family == 3 ? 30.0f : 20.0f;
            }
            CssKeyframeStop stops[] = {
                {0.0f, lam::own_arr(&from), 1, nullptr},
                {1.0f, lam::own_arr(&to), 1, nullptr}
            };
            CssKeyframes keyframes = {lam::up("physicalSide"), lam::own_arr(stops), 2};
            CssAnimProp config = defaultAnimProp("physicalSide", 1.0f);
            AnimationInstance* instance = css_animation_create(
                scheduler, element, &config, &keyframes, 0.0, pool);
            ASSERT_NE(instance, nullptr);
            css_animation_tick(instance, .5f);
            if (family == 0) {
                EXPECT_EQ(border.colors[side].r, 128) << from.property_code;
                EXPECT_EQ(border.colors[side].b, 128) << from.property_code;
                EXPECT_FALSE(instance->layout_changed);
            } else {
                float value = family == 1 ? border.width.values[side]
                    : family == 2 ? position.inset_values[side]
                    : family == 3 ? boundary.margin.values[side]
                    : boundary.padding.values[side];
                EXPECT_FLOAT_EQ(value, family == 1 ? 6.0f : 10.0f) << from.property_code;
                EXPECT_TRUE(instance->layout_changed) << from.property_code;
                if (family == 2) {
                    EXPECT_TRUE(position.inset_present[side]);
                    EXPECT_TRUE(isnan(position.inset_percents[side]));
                }
                css_animation_tick(instance, -1.0f);
                value = family == 1 ? border.width.values[side]
                    : family == 2 ? position.inset_values[side]
                    : family == 3 ? boundary.margin.values[side]
                    : boundary.padding.values[side];
                EXPECT_FLOAT_EQ(value, family == 2 ? -20.0f : family == 3 ? -50.0f : 0.0f)
                    << from.property_code;
            }
            animation_scheduler_remove(scheduler, instance);
        }
    }
}

TEST_F(AnimationTickTest, PhysicalSideTransitionsGrowTracksAndKeepRetargetSnapshots) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    BoundaryProp boundary = {};
    BorderProp border = {};
    PositionProp position = {};
    element->bound = lam::view_prop(&boundary);
    boundary.border = lam::own(&border);
    element->position = lam::view_prop(&position);
    Color red = {};
    red.r = red.a = 255;
    Color blue = {};
    blue.b = blue.a = 255;
    setAnimationStyle(element, "transition:all 1s linear");
    setPhysicalSideTargets(&boundary, &border, &position, -10.0f, 2.0f, 0.0f, red);
    css_transition_resolve(element, &layout);
    CssTransitionElemState* tracks = element->transition_state_prop();
    ASSERT_NE(tracks, nullptr);
    EXPECT_GE(tracks->track_count, 20);
    EXPECT_GE(tracks->track_capacity, tracks->track_count);
    EXPECT_EQ(scheduler->count, 0);

    setPhysicalSideTargets(&boundary, &border, &position, 30.0f, 10.0f, 20.0f, blue);
    css_transition_resolve(element, &layout);
    ASSERT_EQ(scheduler->count, 20);
    animation_scheduler_tick(scheduler, .5, nullptr);
    EXPECT_TRUE(scheduler->needs_layout);
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        EXPECT_EQ(border.colors[side].r, 128);
        EXPECT_EQ(border.colors[side].b, 128);
        EXPECT_FLOAT_EQ(border.width.values[side], 6.0f);
        EXPECT_FLOAT_EQ(boundary.margin.values[side], 10.0f);
        EXPECT_FLOAT_EQ(boundary.flow_margin.values[side], 10.0f);
        EXPECT_FLOAT_EQ(boundary.padding.values[side], 10.0f);
        EXPECT_FLOAT_EQ(position.inset_values[side], 10.0f);
    }

    // a new style pass restores targets before resampling the retained effects.
    setPhysicalSideTargets(&boundary, &border, &position, 30.0f, 10.0f, 20.0f, blue);
    border.width.left = 18.0f;
    css_transition_resolve(element, &layout);
    ASSERT_EQ(scheduler->count, 20);
    animation_scheduler_tick(scheduler, .75, nullptr);
    EXPECT_FLOAT_EQ(border.width.left, 9.0f);
    EXPECT_FLOAT_EQ(border.width.right, 8.0f);
    animation_scheduler_tick(scheduler, 2.0, nullptr);
    EXPECT_EQ(scheduler->count, 0);
    EXPECT_FLOAT_EQ(border.width.left, 18.0f);
    EXPECT_FLOAT_EQ(border.width.right, 10.0f);
}

TEST_F(AnimationTickTest, TypedOpacityAndColorsResolveBeforeInterpolation) {
    const struct { const char* rule; const char* style; float opacity; Color color; } cases[] = {
        {"from{opacity:0%}to{opacity:100%}", "", .5f, {0xFF008000}},
        {"from{opacity:var(--low)}to{opacity:var(--high)}", "--low:.2;--high:.8", .5f, {0xFF008000}},
        {"from{opacity:calc(.2 + .1)}to{opacity:calc(.8 - .1)}", "", .5f, {0xFF008000}},
        {"from{opacity:calc(10% + 20%)}to{opacity:clamp(0%,90%,70%)}", "", .5f, {0xFF008000}},
        {"from{opacity:-1}to{opacity:1}", "", .5f, {0xFF008000}},
        {"from{opacity:0}to{opacity:2}", "", .5f, {0xFF008000}},
        {"from{color:var(--start)}to{color:var(--end)}", "--start:red;--end:blue", .2f, {0xFF800080}},
        {"from{color:currentColor}to{color:blue}", "", .2f, {0xFF800000}},
        {"from{color:rgb(255 0 0 / .5)}to{color:rgb(0 0 255 / .5)}", "", .2f, {0x80800080}}
    };
    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.rule);
        MockElement mock;
        DomElement* element = createMockElement(&mock);
        mock.in_line.opacity = .2f;
        mock.in_line.color = Color{0xFF008000};
        mock.in_line.has_color = true;
        StrBuf* content = strbuf_new();
        strbuf_append_format(content, "typed{%s}", test_case.rule);
        ASSERT_NE(parsedKeyframes(content->str, "typed"), nullptr);
        strbuf_free(content);
        StrBuf* style = strbuf_new();
        strbuf_append_format(style, "%s;animation:typed 1s linear -.5s both paused", test_case.style);
        setAnimationStyle(element, style->str);
        strbuf_free(style);
        layout.view = lam::up(static_cast<View*>(element));
        layout.elmt = lam::up(element);
        css_animation_resolve(element, &layout);
        EXPECT_FLOAT_EQ(mock.in_line.opacity, test_case.opacity);
        EXPECT_EQ(mock.in_line.color.c, test_case.color.c);
        while (scheduler->first) animation_scheduler_cancel(scheduler, scheduler->first);
    }
}

TEST_F(AnimationTickTest, TypedValueCachesStayPerElementAndRefreshVariables) {
    CssKeyframes* keyframes = parsedKeyframes(
        "context{from{opacity:var(--low);color:var(--start)}"
        "to{opacity:var(--high);color:var(--end)}}", "context");
    ASSERT_NE(keyframes, nullptr);
    MockElement mocks[2];
    DomElement* elements[2] = {};
    AnimationInstance* instances[2] = {};
    const char* styles[] = {
        "--low:.2;--high:.8;--start:red;--end:blue;animation:context 1s linear -.5s both paused",
        "--low:10%;--high:30%;--start:green;--end:white;animation:context 1s linear -.5s both paused"
    };
    for (int index = 0; index < 2; index++) {
        elements[index] = createMockElement(&mocks[index]);
        setAnimationStyle(elements[index], styles[index]);
        layout.view = lam::up(static_cast<View*>(elements[index]));
        layout.elmt = lam::up(elements[index]);
        css_animation_resolve(elements[index], &layout);
        instances[index] = scheduler->last;
        ASSERT_NE(instances[index], nullptr);
    }
    EXPECT_FLOAT_EQ(mocks[0].in_line.opacity, .5f);
    EXPECT_FLOAT_EQ(mocks[1].in_line.opacity, .2f);
    EXPECT_EQ(mocks[0].in_line.color.c, 0xFF800080u);
    EXPECT_EQ(mocks[1].in_line.color.c, 0xFF80C080u);
    CssAnimState* state = (CssAnimState*)instances[0]->state;
    CssAnimValueSample* retained = state->value_samples;
    ASSERT_EQ(state->value_sample_count, 4);
    setAnimationStyle(elements[0],
        "--low:calc(10% + 10%);--high:.4;--start:black;--end:blue;"
        "animation:context 1s linear -.5s both paused");
    layout.view = lam::up(static_cast<View*>(elements[0]));
    layout.elmt = lam::up(elements[0]);
    css_animation_resolve(elements[0], &layout);
    EXPECT_EQ(state->value_samples.get(), retained);
    EXPECT_FLOAT_EQ(mocks[0].in_line.opacity, .3f);
    EXPECT_EQ(mocks[0].in_line.color.c, 0xFF800000u);
    css_animation_tick(instances[1], .5f);
    EXPECT_FLOAT_EQ(mocks[1].in_line.opacity, .2f);
    EXPECT_EQ(mocks[1].in_line.color.c, 0xFF80C080u);
    EXPECT_NE(keyframes->stops[0].properties[0].expression, nullptr);
    EXPECT_NE(keyframes->stops[0].properties[1].expression, nullptr);
}

TEST_F(AnimationTickTest, TypedValuesDefaultAfterInvalidSubstitutionAndInheritParent) {
    MockElement parent_mock, child_mock;
    DomElement* parent = createMockElement(&parent_mock);
    DomElement* child = createMockElement(&child_mock);
    child->parent = lam::up(static_cast<DomNode*>(parent));
    parent_mock.in_line.opacity = .3f;
    parent_mock.in_line.color = Color{0xFF008000};
    parent_mock.in_line.has_color = true;
    CssKeyframes* keyframes = parsedKeyframes(
        "defaults{from{opacity:var(--invalid);color:var(--invalid)}"
        "to{opacity:inherit;color:blue}}", "defaults");
    ASSERT_NE(keyframes, nullptr);
    setAnimationStyle(child, "--invalid:1px;animation:defaults 1s linear -.5s both paused");
    layout.view = lam::up(static_cast<View*>(child));
    layout.elmt = lam::up(child);
    css_animation_resolve(child, &layout);
    EXPECT_FLOAT_EQ(child_mock.in_line.opacity, .65f);
    EXPECT_EQ(child_mock.in_line.color.c, 0xFF804000u);
}

TEST_F(AnimationTickTest, TypedLengthsResolvePerElementAndRefreshAfterRelayout) {
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;
    setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr,
        "units { from { width:2em; padding-top:10%; top:10%; }"
        "to { width:calc(4em + 20%); padding-top:30%; top:30%; } }");
    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    doc.services.keyframe_registry = registry;
    doc.stylesheets = nullptr;
    doc.stylesheet_count = 0;
    CssKeyframes* keyframes = keyframe_registry_find(registry, "units");
    ASSERT_NE(keyframes, nullptr);

    MockElement mocks[2];
    BlockProp blocks[2] = {};
    FontProp fonts[2] = {};
    BoundaryProp boundaries[2] = {};
    PositionProp positions[2] = {};
    DomElement* elements[2] = {};
    AnimationInstance* instances[2] = {};
    BlockContext parent = {};
    parent.content_width = 200.0f;
    parent.content_height = parent.given_height = 100.0f;
    layout.block.parent = lam::up(&parent);
    layout.root_font_size = 12.0f;
    for (int index = 0; index < 2; index++) {
        elements[index] = createMockElement(&mocks[index]);
        elements[index]->blk = lam::view_prop(&blocks[index]);
        elements[index]->font = lam::view_prop(&fonts[index]);
        elements[index]->bound = lam::view_prop(&boundaries[index]);
        elements[index]->position = lam::view_prop(&positions[index]);
        blocks[index].given_width = 20.0f;
        fonts[index].font_size = (float)(index + 1) * 10.0f;
        layout.view = lam::up(static_cast<View*>(elements[index]));
        layout.elmt = lam::up(elements[index]);
        layout.font.style = lam::up(&fonts[index]);
        layout.font.current_font_size = fonts[index].font_size;
        setAnimationStyle(elements[index], "animation:units 1s linear -.5s both paused");
        css_animation_resolve(elements[index], &layout);
        instances[index] = scheduler->last;
        ASSERT_NE(instances[index], nullptr);
        EXPECT_FLOAT_EQ(blocks[index].given_width, index == 0 ? 50.0f : 80.0f);
        EXPECT_FLOAT_EQ(boundaries[index].padding.top, 40.0f);
        EXPECT_FLOAT_EQ(positions[index].top, 20.0f);
    }
    css_animation_tick(instances[0], .5f);
    EXPECT_FLOAT_EQ(blocks[0].given_width, 50.0f);
    EXPECT_EQ(keyframes->stops[0].properties[0].expression->data.length.unit, CSS_UNIT_EM);
    CssAnimState* state = (CssAnimState*)instances[0]->state;
    CssAnimValueSample* retained_samples = state->value_samples;
    ASSERT_EQ(state->value_sample_count, 6);

    // refresh one effect without changing the shared keyframes or the other effect's cache.
    parent.content_width = 400.0f;
    fonts[0].font_size = 30.0f;
    layout.view = lam::up(static_cast<View*>(elements[0]));
    layout.elmt = lam::up(elements[0]);
    layout.font.style = lam::up(&fonts[0]);
    layout.font.current_font_size = 30.0f;
    css_animation_resolve(elements[0], &layout);
    EXPECT_FLOAT_EQ(blocks[0].given_width, 130.0f);
    EXPECT_FLOAT_EQ(boundaries[0].padding.top, 80.0f);
    EXPECT_EQ(state->value_samples.get(), retained_samples);
    css_animation_tick(instances[1], .5f);
    EXPECT_FLOAT_EQ(blocks[1].given_width, 80.0f);
}

TEST_F(AnimationTickTest, WebLengthEffectsInvalidateWithoutDomMutations) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    BlockProp block = {};
    element->blk = lam::view_prop(&block);
    block.given_width = 40.0f;
    CssAnimatedProp properties[2] = {};
    ASSERT_TRUE(css_animation_parse_property_value(
        CSS_PROPERTY_WIDTH, "20px", &properties[0], pool));
    ASSERT_TRUE(css_animation_parse_property_value(
        CSS_PROPERTY_WIDTH, "100px", &properties[1], pool));
    CssKeyframeStop stops[] = {
        {0.0f, lam::own_arr(&properties[0]), 1, nullptr},
        {1.0f, lam::own_arr(&properties[1]), 1, nullptr}
    };
    CssKeyframes keyframes = {lam::up("web"), lam::own_arr(stops), 2};
    element->set_styles_resolved(true);
    CssWebAnimationState* state = css_web_animation_create(
        element, &keyframes, 1000.0, nullptr, pool);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(document_state.needs_reflow);
    EXPECT_FALSE(element->styles_resolved());
    css_web_animation_set_current_time(state, 500.0);
    css_web_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(block.given_width, 60.0f);

    doc_state_clear_reflow(&document_state);
    element->set_styles_resolved(true);
    css_web_animation_set_current_time(state, 750.0);
    EXPECT_TRUE(document_state.needs_reflow);
    EXPECT_FALSE(element->styles_resolved());
    css_web_animation_resolve(element, &layout);
    EXPECT_FLOAT_EQ(block.given_width, 80.0f);
    EXPECT_EQ(doc.js.mutation_count, 0);
    EXPECT_EQ(doc.js.mutation_sequence, 0u);
    doc_state_clear_reflow(&document_state);
    css_web_animation_set_current_time(state, 750.0);
    EXPECT_FALSE(document_state.needs_reflow);
}

TEST_F(AnimationTickTest, TransformAnimationMarksDocumentOwnedList) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);
    TransformProp transform = {};
    element->transform = lam::view_prop(&transform);

    TransformFunction keyframe_function = {};
    keyframe_function.type = TRANSFORM_TRANSLATEX;
    keyframe_function.params.translate.x = 24.0f;
    CssAnimatedProp property = {};
    property.property_code = CSS_PROPERTY_TRANSFORM;
    property.value_type = ANIM_VAL_TRANSFORM;
    property.value.transform = &keyframe_function;
    CssKeyframeStop stop = {0.0f, lam::own_arr(&property), 1, NULL};
    CssKeyframes keyframes = {lam::up("slide"), lam::own_arr(&stop), 1};

    CssAnimProp animation = defaultAnimProp("slide", 1.0f);
    AnimationInstance* instance = css_animation_create(
        scheduler, element, &animation, &keyframes, 0.0, pool);
    ASSERT_NE(instance, nullptr);

    css_animation_tick(instance, 0.0f);

    ASSERT_NE(transform.functions, nullptr);
    EXPECT_FLOAT_EQ(transform.functions->params.translate.x, 24.0f);
    EXPECT_EQ(transform.functions_owner, TRANSFORM_FUNCTIONS_DOCUMENT_POOL);
}

TEST_F(AnimationTickTest, ThreeStopInterpolation) {
    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimatedProp props[3] = {};
    for (int i = 0; i < 3; i++) {
        props[i].property_code = CSS_PROPERTY_OPACITY;
        props[i].value_type = ANIM_VAL_FLOAT;
    }
    props[0].value.f = 1.0f;
    props[1].value.f = 0.5f;
    props[2].value.f = 1.0f;

    CssKeyframeStop stops[3];
    stops[0] = {0.0f, lam::own_arr(&props[0]), 1, NULL};
    stops[1] = {0.5f, lam::own_arr(&props[1]), 1, NULL};
    stops[2] = {1.0f, lam::own_arr(&props[2]), 1, NULL};

    CssKeyframes kf = {lam::up("pulse"), lam::own_arr(stops), 3};

    CssAnimProp ap = defaultAnimProp("pulse", 2.0f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, &kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    // t=0.25 -> between stop 0 and 1, local_t=0.5
    css_animation_tick(inst, 0.25f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.75f);

    // t=0.75 -> between stop 1 and 2, local_t=0.5
    css_animation_tick(inst, 0.75f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.75f);
}

// ============================================================================
// Full Pipeline Test (parsing + tick)
// ============================================================================

TEST_F(AnimationTickTest, ParseAndTickOpacity) {
    CssStylesheet sheet;
    CssRule rule;
    CssRule* rule_ptr;
    CssStylesheet* sheet_ptr;
    setup_keyframes_sheet(&doc, &sheet, &rule, &rule_ptr, &sheet_ptr,
        "fadeIn { from { opacity: 0; } to { opacity: 1; } }");

    KeyframeRegistry* registry = keyframe_registry_create(&doc, pool);
    CssKeyframes* kf = keyframe_registry_find(registry, "fadeIn");
    ASSERT_NE(kf, nullptr);

    MockElement mock;
    DomElement* element = createMockElement(&mock);

    CssAnimProp ap = defaultAnimProp("fadeIn", 0.5f);
    AnimationInstance* inst = css_animation_create(scheduler, element, &ap, kf, 0.0, pool);
    ASSERT_NE(inst, nullptr);

    css_animation_tick(inst, 0.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 0.0f);

    css_animation_tick(inst, 0.33f);
    EXPECT_NEAR(mock.in_line.opacity, 0.33f, 0.01f);

    css_animation_tick(inst, 0.67f);
    EXPECT_NEAR(mock.in_line.opacity, 0.67f, 0.01f);

    css_animation_tick(inst, 1.0f);
    EXPECT_FLOAT_EQ(mock.in_line.opacity, 1.0f);
}

TEST(CssTransformInterpolation, AffineEndpointsRemainExactAndSingularProductsDoNotDecompose) {
    RdtMatrix4 from = rdt_matrix4_identity(), to = rdt_matrix4_identity();
    from.values[0] = -.75f; from.values[4] = .25f;
    from.values[1] = .3f; from.values[5] = 1.4f;
    from.values[3] = 10.0f; from.values[7] = 20.0f;
    to.values[0] = .8f; to.values[4] = .6f;
    to.values[1] = -.2f; to.values[5] = 1.1f;
    to.values[3] = 30.0f; to.values[7] = 40.0f;
    const float progresses[] = {0.0f, 1.0f};
    for (float progress : progresses) {
        RdtMatrix4 sampled;
        ASSERT_TRUE(radiant::interpolate_transform_matrix_2d(&from, &to, progress, &sampled));
        const RdtMatrix4* expected = progress == 0.0f ? &from : &to;
        for (int index = 0; index < 16; index++)
            EXPECT_NEAR(sampled.values[index], expected->values[index], .00001f);
    }
    RdtMatrix4 sampled;
    from.values[0] = from.values[4] = 0.0f;
    EXPECT_FALSE(radiant::interpolate_transform_matrix_2d(&from, &to, .5f, &sampled));
}

TEST(CssTransformInterpolation, SpatialEndpointsPreservePerspectiveShearAndReflections) {
    RdtMatrix4 from = {{-.75f,.3f,.2f,10, .25f,1.4f,-.4f,20, .2f,.5f,.9f,30, .001f,-.002f,-.003f,1}};
    RdtMatrix4 to = {{.8f,-.2f,.3f,30, .6f,1.1f,.2f,40, -.4f,.7f,1.5f,50, -.003f,.002f,-.001f,1}};
    const float progresses[] = {0.0f, 1.0f};
    for (float progress : progresses) {
        RdtMatrix4 sampled;
        ASSERT_TRUE(radiant::interpolate_transform_matrix(&from, &to, progress, &sampled));
        const RdtMatrix4* expected = progress == 0.0f ? &from : &to;
        for (int coefficient = 0; coefficient < 16; coefficient++)
            EXPECT_NEAR(sampled.values[coefficient], expected->values[coefficient], .00005f) << coefficient;
    }
    RdtMatrix4 half_turn = {{0,-1,0,0, -1,0,0,0, 0,0,-1,0, 0,0,0,1}};
    RdtMatrix4 identity = rdt_matrix4_identity(), sampled;
    ASSERT_TRUE(radiant::interpolate_transform_matrix(&identity, &half_turn, .5f, &sampled));
    EXPECT_NEAR(sampled.values[0], .5f, .00001f);
    EXPECT_NEAR(sampled.values[1], -.5f, .00001f);
    EXPECT_NEAR(sampled.values[2], .7071068f, .00001f);
    EXPECT_NEAR(sampled.values[8], -.7071068f, .00001f);
    from.values[15] = 0.0f;
    EXPECT_FALSE(radiant::interpolate_transform_matrix(&from, &to, .5f, &sampled));
    from = rdt_matrix4_identity();
    from.values[10] = 0.0f;
    EXPECT_FALSE(radiant::interpolate_transform_matrix(&from, &to, .5f, &sampled));
}
