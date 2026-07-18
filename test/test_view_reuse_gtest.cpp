#include <gtest/gtest.h>

#include "../radiant/view.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_style_node.hpp"

class ViewReuseTest : public ::testing::Test {
protected:
    ViewTree tree = {};

    void SetUp() override {
        tree.prop_pool = pool_create();
        ASSERT_NE(tree.prop_pool, nullptr);
        view_tree_canonical_init(&tree);
        ASSERT_NE(tree.canonical_prop_arena, nullptr);
    }

    void TearDown() override {
        view_tree_canonical_destroy(&tree);
        pool_destroy(tree.prop_pool);
        tree.prop_pool = nullptr;
    }

    DomElement* element() {
        DomElement* value = DomElement::create_in(tree.prop_pool);
        EXPECT_NE(value, nullptr);
        return value;
    }
};

TEST_F(ViewReuseTest, ExplicitHashAndEqualityCoverEveryInlineField) {
    InlineProp left = INLINE_PROP_DEFAULT;
    InlineProp right = left;
    EXPECT_TRUE(inline_prop_equal(&left, &right));
    EXPECT_EQ(inline_prop_hash(&left), inline_prop_hash(&right));

    right.svg_stroke_width = 3.5f;
    right.has_svg_stroke_width = true;
    EXPECT_FALSE(inline_prop_equal(&left, &right));
    EXPECT_NE(inline_prop_hash(&left), inline_prop_hash(&right));
}

TEST_F(ViewReuseTest, EqualParentAndChildPromoteThenCowAtEnsureGate) {
    DomElement* parent = element();
    DomElement* child = element();
    parent->ensure_inline(&tree)->color.c = 0xff123456u;
    parent->in_line->has_color = true;
    child->ensure_inline(&tree)->color = parent->in_line->color;
    child->in_line->has_color = true;

    view_tree_commit_inline_prop(&tree, child, parent);
    ASSERT_TRUE(parent->inline_prop_shared());
    ASSERT_TRUE(child->inline_prop_shared());
    ASSERT_EQ(parent->in_line, child->in_line);
    ASSERT_TRUE(arena_owns(tree.canonical_prop_arena, parent->in_line));

    InlineProp* canonical = parent->in_line;
    InlineProp* mutable_child = child->ensure_inline(&tree);
    ASSERT_NE(mutable_child, canonical);
    EXPECT_FALSE(child->inline_prop_shared());
    EXPECT_EQ(mutable_child->color.c, canonical->color.c);
    mutable_child->opacity = 0.25f;
    EXPECT_EQ(canonical->opacity, INLINE_PROP_DEFAULT.opacity);
    EXPECT_EQ(tree.canonical_stats.inline_cows, 1u);
}

TEST_F(ViewReuseTest, CanonicalIndexReusesAnExistingExactValue) {
    DomElement* parent1 = element();
    DomElement* child1 = element();
    DomElement* parent2 = element();
    DomElement* child2 = element();
    parent1->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;
    child1->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;
    parent2->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;
    child2->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;

    view_tree_commit_inline_prop(&tree, child1, parent1);
    InlineProp* first = parent1->in_line;
    view_tree_commit_inline_prop(&tree, child2, parent2);
    EXPECT_EQ(parent2->in_line, first);
    EXPECT_EQ(child2->in_line, first);
    EXPECT_EQ(tree.inline_canonical_count, 1u);
    EXPECT_EQ(tree.canonical_stats.inline_hits, 1u);
}

TEST_F(ViewReuseTest, CanonicalCapFallsBackToOwnedStorage) {
    tree.canonical_prop_cap_bytes = 0;
    DomElement* parent = element();
    DomElement* child = element();
    parent->ensure_inline(&tree)->visibility = VIS_HIDDEN;
    child->ensure_inline(&tree)->visibility = VIS_HIDDEN;
    InlineProp* parent_owned = parent->in_line;
    InlineProp* child_owned = child->in_line;

    view_tree_commit_inline_prop(&tree, child, parent);
    EXPECT_EQ(parent->in_line, parent_owned);
    EXPECT_EQ(child->in_line, child_owned);
    EXPECT_FALSE(parent->inline_prop_shared());
    EXPECT_FALSE(child->inline_prop_shared());
    EXPECT_EQ(tree.canonical_stats.cap_fallbacks, 1u);
}

class DomRetirementTest : public ::testing::Test {
protected:
    Input input = {};
    DomDocument doc;

    void SetUp() override {
        ASSERT_TRUE(doc.init(&input));
    }

    void TearDown() override {
        doc.destroy();
    }

    DomElement* root() {
        DomElement* value = DomElement::create(&doc, "root", nullptr);
        EXPECT_NE(value, nullptr);
        doc.root = value;
        return value;
    }
};

TEST_F(DomRetirementTest, UnpinnedDetachedNodeRetiresAndRejectsStaleRef) {
    DomElement* parent = root();
    DomElement* child = DomElement::create(&doc, "child", nullptr);
    ASSERT_TRUE(parent->append_child(child));
    DomNodeRef ref = dom_node_ref(child);

    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);

    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retired_nodes, 1u);
    EXPECT_EQ(stats.retired_primary_bytes, sizeof(DomElement));
    EXPECT_EQ(stats.stale_ref_rejections, 1u);
}

TEST_F(DomRetirementTest, PinBlocksRetirementUntilReleased) {
    DomElement* parent = root();
    DomElement* child = DomElement::create(&doc, "child", nullptr);
    ASSERT_TRUE(parent->append_child(child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));

    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
}

TEST_F(DomRetirementTest, ReinsertionCancelsDetachedCandidate) {
    DomElement* parent = root();
    DomElement* child = DomElement::create(&doc, "child", nullptr);
    ASSERT_TRUE(parent->append_child(child));
    DomNodeRef ref = dom_node_ref(child);

    ASSERT_TRUE(parent->remove_child(child));
    ASSERT_TRUE(parent->append_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);
}

TEST_F(DomRetirementTest, PinnedDescendantBlocksBottomUpSubtreeRetirement) {
    DomElement* outer = root();
    DomElement* branch = DomElement::create(&doc, "branch", nullptr);
    DomElement* leaf = DomElement::create(&doc, "leaf", nullptr);
    ASSERT_TRUE(outer->append_child(branch));
    ASSERT_TRUE(branch->append_child(leaf));
    DomNodeRef leaf_ref = dom_node_ref(leaf);
    ASSERT_TRUE(dom_node_pin(&doc, leaf_ref, DOM_NODE_PIN_RANGE));

    ASSERT_TRUE(outer->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    ASSERT_TRUE(dom_node_unpin(&doc, leaf_ref, DOM_NODE_PIN_RANGE));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
}

TEST_F(DomRetirementTest, GeneratedTextPayloadIsFreedWithItsNode) {
    DomElement* parent = root();
    DomText* text = DomText::create_copy("before", 6, parent);
    ASSERT_NE(text, nullptr);
    String* replacement = dom_document_create_string(&doc, "after", 5);
    ASSERT_NE(replacement, nullptr);
    ASSERT_TRUE(dom_text_adopt_document_string(text, &doc, replacement));
    ASSERT_TRUE(parent->append_child(text));
    PoolStats before = {};
    pool_get_detailed_stats(doc.document_pool, &before);

    ASSERT_TRUE(parent->remove_child(text));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    PoolStats after = {};
    pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_GT(after.free_count, before.free_count);
}

TEST_F(DomRetirementTest, NodeArenaGrowthPlateausAcrossTenThousandRetirements) {
    DomElement* parent = root();
    for (int i = 0; i < 128; i++) {
        DomElement* child = DomElement::create(&doc, "child", nullptr);
        ASSERT_TRUE(parent->append_child(child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats warm = {};
    arena_get_stats(doc.node_arena, &warm);

    uint32_t last_id = 0;
    for (int i = 0; i < 10000; i++) {
        DomElement* child = DomElement::create(&doc, "child", nullptr);
        uint32_t node_id = static_cast<DomNode*>(child)->id;
        ASSERT_GT(node_id, last_id);
        last_id = node_id;
        ASSERT_TRUE(parent->append_child(child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats after = {};
    arena_get_stats(doc.node_arena, &after);
    EXPECT_EQ(after.fresh_growth_bytes, warm.fresh_growth_bytes);
    EXPECT_GE(after.bump_back_count - warm.bump_back_count, 10000u);
}

TEST_F(DomRetirementTest, MoreThanMutationRecordCapRetiresAfterPinsRelease) {
    DomElement* parent = root();
    DomNodeRef refs[DOM_JS_MUTATION_RECORD_CAP * 4] = {};
    for (int i = 0; i < DOM_JS_MUTATION_RECORD_CAP * 4; i++) {
        DomElement* child = DomElement::create(&doc, "held", nullptr);
        refs[i] = dom_node_ref(child);
        DomNodePinReason reason = (DomNodePinReason)(i % DOM_NODE_PIN_REASON_COUNT);
        ASSERT_TRUE(dom_node_pin(&doc, refs[i], reason));
        ASSERT_TRUE(parent->append_child(child));
        ASSERT_TRUE(parent->remove_child(child));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    for (int i = 0; i < DOM_JS_MUTATION_RECORD_CAP * 4; i++) {
        DomNodePinReason reason = (DomNodePinReason)(i % DOM_NODE_PIN_REASON_COUNT);
        ASSERT_TRUE(dom_node_unpin(&doc, refs[i], reason));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), DOM_JS_MUTATION_RECORD_CAP * 4u);
}

TEST_F(DomRetirementTest, VariableTextSizesReuseArenaBlocksAfterWarmup) {
    DomElement* parent = root();
    char text[513];
    memset(text, 'x', sizeof(text));
    for (size_t len = 1; len <= 512; len++) {
        DomText* node = DomText::create_copy(text, len, parent);
        ASSERT_NE(node, nullptr);
        ASSERT_TRUE(parent->append_child(node));
        ASSERT_TRUE(parent->remove_child(node));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats warm = {};
    arena_get_stats(doc.node_arena, &warm);
    for (int cycle = 0; cycle < 20; cycle++) {
        for (size_t len = 1; len <= 512; len++) {
            DomText* node = DomText::create_copy(text, len, parent);
            ASSERT_NE(node, nullptr);
            ASSERT_TRUE(parent->append_child(node));
            ASSERT_TRUE(parent->remove_child(node));
            ASSERT_EQ(dom_retire_sweep(&doc), 1u);
        }
    }
    ArenaStats after = {};
    arena_get_stats(doc.node_arena, &after);
    EXPECT_EQ(after.fresh_growth_bytes, warm.fresh_growth_bytes);
    EXPECT_GT(after.bump_back_count, warm.bump_back_count);
}

TEST(DomRetirementOwnerArenaTest, FatLambdaNodeReturnsToItsInputArena) {
    Pool* input_pool = pool_create();
    ASSERT_NE(input_pool, nullptr);
    Arena* input_arena = arena_create_default(input_pool);
    ASSERT_NE(input_arena, nullptr);
    Input input = {};
    input.arena = input_arena;
    DomDocument doc;
    ASSERT_TRUE(doc.init(&input));

    DomElement* root = DomElement::create(&doc, "root", nullptr);
    ASSERT_NE(root, nullptr);
    doc.root = root;
    DomElement* storage = DomElement::create_in(input_arena);
    ASSERT_NE(storage, nullptr);
    DomElement* child = DomElement::create_in(
        storage, &doc, "child", nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(root->append_child(child));
    ASSERT_TRUE(root->remove_child(child));

    ArenaStats before = {};
    arena_get_stats(input_arena, &before);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    ArenaStats after = {};
    arena_get_stats(input_arena, &after);
    EXPECT_EQ(after.free_count, before.free_count + 1u);

    doc.destroy();
    arena_destroy(input_arena);
    pool_destroy(input_pool);
}

class StyleEpochTest : public ::testing::Test {
protected:
    Input input = {};
    DomDocument doc;
    CssEngine engine = {};
    DomElement* document_root = nullptr;

    void SetUp() override {
        ASSERT_TRUE(doc.init(&input));
        engine.context.viewport_width = 1280.0;
        engine.context.viewport_height = 720.0;
        engine.context.device_pixel_ratio = 2.0;
        document_root = DomElement::create(&doc, "root", nullptr);
        ASSERT_NE(document_root, nullptr);
        doc.root = document_root;
    }

    void TearDown() override {
        doc.destroy();
    }

    DomElement* append(const char* tag) {
        DomElement* child = DomElement::create(&doc, tag, nullptr);
        EXPECT_NE(child, nullptr);
        EXPECT_TRUE(document_root->append_child(child));
        return child;
    }

    CssRule* rule(CssPropertyId property, CssValue* value,
                  uint32_t source_order = 1) {
        CssDeclaration* declaration = css_declaration_create(
            property, value, {}, CSS_ORIGIN_AUTHOR, doc.document_pool);
        EXPECT_NE(declaration, nullptr);
        declaration->source_order = source_order;
        declaration->value_text = "snapshot";
        declaration->value_text_len = 8;

        CssRule* result = (CssRule*)pool_calloc(doc.document_pool, sizeof(CssRule));
        EXPECT_NE(result, nullptr);
        result->type = CSS_RULE_STYLE;
        result->pool = doc.document_pool;
        result->origin = CSS_ORIGIN_AUTHOR;
        result->source_order = source_order;
        result->data.style_rule.declarations = (CssDeclaration**)pool_alloc(
            doc.document_pool, sizeof(CssDeclaration*));
        EXPECT_NE(result->data.style_rule.declarations, nullptr);
        result->data.style_rule.declarations[0] = declaration;
        result->data.style_rule.declaration_count = 1;
        return result;
    }

    void apply(DomElement* root, CssRule* first, DomElement* a,
               DomElement* b = nullptr, bool global_change = false) {
        ASSERT_TRUE(style_epoch_cascade_begin(
            &doc, root, &engine, global_change));
        ASSERT_EQ(dom_element_apply_rule(a, first, {}), 1);
        if (b) ASSERT_EQ(dom_element_apply_rule(b, first, {}), 1);
        style_epoch_cascade_end(&doc);
    }
};

TEST_F(StyleEpochTest, ExactRecipesBindOneCanonicalTreeWithoutElementClones) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));

    apply(document_root, width, first, second);

    ASSERT_TRUE(first->specified_style_shared());
    ASSERT_TRUE(second->specified_style_shared());
    EXPECT_EQ(first->specified_style, second->specified_style);
    CssDeclaration* canonical = dom_element_get_specified_value(
        first, CSS_PROPERTY_WIDTH);
    ASSERT_NE(canonical, nullptr);
    EXPECT_TRUE(canonical->owns_payload);
    EXPECT_NE(canonical->value,
        width->data.style_rule.declarations[0]->value);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_GE(stats.hit_count, 1u);
    EXPECT_EQ(stats.bound_element_refs, 3u);
    EXPECT_GT(stats.current_reserved_bytes, 0u);
}

TEST_F(StyleEpochTest, InvalidLaterDeclarationPreservesLastValidValue) {
    DomElement* child = append("child");
    CssRule* valid_height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* invalid_height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, -1.0, CSS_UNIT_PX), 2);

    ASSERT_TRUE(style_epoch_cascade_begin(
        &doc, document_root, &engine, false));
    EXPECT_EQ(dom_element_apply_rule(child, valid_height, {}), 1);
    EXPECT_EQ(dom_element_apply_rule(child, invalid_height, {}), 0);
    style_epoch_cascade_end(&doc);

    CssDeclaration* winner = dom_element_get_specified_value(
        child, CSS_PROPERTY_HEIGHT);
    ASSERT_NE(winner, nullptr);
    ASSERT_NE(winner->value, nullptr);
    EXPECT_EQ(winner->value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_DOUBLE_EQ(winner->value->data.length.value, 40.0);
}

TEST_F(StyleEpochTest, ForcedHashCollisionStillRequiresExactRecipeEquality) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    DomElement* third = append("third");
    DomElement* fourth = append("fourth");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);
    style_epoch_debug_force_hash_collision(&doc, true);

    ASSERT_TRUE(style_epoch_cascade_begin(&doc, document_root, &engine, false));
    ASSERT_EQ(dom_element_apply_rule(first, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(second, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(third, height, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(fourth, height, {}), 1);
    style_epoch_cascade_end(&doc);

    EXPECT_EQ(first->specified_style, second->specified_style);
    EXPECT_EQ(third->specified_style, fourth->specified_style);
    EXPECT_NE(first->specified_style, third->specified_style);
    EXPECT_NE(dom_element_get_specified_value(first, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_EQ(dom_element_get_specified_value(first, CSS_PROPERTY_HEIGHT), nullptr);
    EXPECT_NE(dom_element_get_specified_value(third, CSS_PROPERTY_HEIGHT), nullptr);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_GT(stats.collision_count, 0u);
}

TEST_F(StyleEpochTest, InlineMutationAndPropertyRemovalCowIndependently) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    apply(document_root, width, first, second);
    StyleTree* canonical = second->specified_style;

    CssSpecificity inline_specificity = {};
    inline_specificity.inline_style = 1;
    CssDeclaration* height = css_declaration_create(
        CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 25.0, CSS_UNIT_PX),
        inline_specificity, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_TRUE(dom_element_apply_declaration(first, height));
    EXPECT_FALSE(first->specified_style_shared());
    EXPECT_EQ(second->specified_style, canonical);
    EXPECT_NE(dom_element_get_specified_value(first, CSS_PROPERTY_HEIGHT), nullptr);
    EXPECT_EQ(dom_element_get_specified_value(second, CSS_PROPERTY_HEIGHT), nullptr);

    ASSERT_TRUE(dom_element_remove_property(second, CSS_PROPERTY_WIDTH));
    EXPECT_FALSE(second->specified_style_shared());
    EXPECT_EQ(dom_element_get_specified_value(second, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_NE(dom_element_get_specified_value(first, CSS_PROPERTY_WIDTH), nullptr);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_EQ(stats.cow_count, 2u);
}

TEST_F(StyleEpochTest, OldAndNewEpochsCoexistThenReleaseAtLastBinding) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    apply(document_root, width, first, second);
    uint64_t old_epoch = style_epoch_current_id(&doc);

    style_epoch_unbind_element(second);
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);
    apply(second, height, second, nullptr, true);
    EXPECT_GT(style_epoch_current_id(&doc), old_epoch);
    StyleEpochStats coexist = {};
    style_epoch_get_stats(&doc, &coexist);
    EXPECT_GT(coexist.retired_referenced_reserved_bytes, 0u);

    style_epoch_unbind_element(first);
    style_epoch_unbind_element(document_root);
    StyleEpochStats released = {};
    style_epoch_get_stats(&doc, &released);
    EXPECT_EQ(released.retired_referenced_reserved_bytes, 0u);
    EXPECT_GE(released.released_epoch_count, 1u);
    EXPECT_TRUE(second->specified_style_shared());
}

TEST_F(StyleEpochTest, MediaEnvironmentChangeAdvancesButLocalRematchDoesNot) {
    DomElement* child = append("child");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    apply(document_root, width, child);
    uint64_t initial_epoch = style_epoch_current_id(&doc);

    style_epoch_unbind_element(child);
    apply(child, width, child);
    EXPECT_EQ(style_epoch_current_id(&doc), initial_epoch);

    style_epoch_unbind_element(child);
    engine.context.viewport_width = 640.0;
    apply(child, width, child);
    EXPECT_GT(style_epoch_current_id(&doc), initial_epoch);
}

TEST_F(StyleEpochTest, CowValueSnapshotSurvivesSourceMutationAndEpochRelease) {
    DomElement* child = append("child");
    CssValue* source_value = css_value_create_string(doc.document_pool, "epoch-value");
    CssRule* content = rule(CSS_PROPERTY_CONTENT, source_value);
    apply(document_root, content, child);
    ASSERT_TRUE(style_epoch_ensure_owned(child));
    CssDeclaration* owned = dom_element_get_specified_value(
        child, CSS_PROPERTY_CONTENT);
    ASSERT_NE(owned, nullptr);
    ASSERT_NE(owned->value, source_value);
    ASSERT_NE(owned->value->data.string, source_value->data.string);

    char* mutable_source = (char*)source_value->data.string;
    mutable_source[0] = 'X';
    style_epoch_unbind_element(document_root);
    style_epoch_mark_global_change(&doc);
    ASSERT_TRUE(style_epoch_cascade_begin(&doc, child, &engine, false));
    style_epoch_cascade_end(&doc);

    EXPECT_STREQ(owned->value->data.string, "epoch-value");
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_GE(stats.released_epoch_count, 1u);
}
