#include <gtest/gtest.h>

#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/css_resolved_style.hpp"
#include "../radiant/grid_placement.hpp"
#include "../radiant/grid_sizing_algorithm.hpp"
#include "../radiant/layout_table.hpp"
#include "../radiant/page_document.hpp"
#include "../radiant/page_fo_expression.hpp"
#include "../radiant/render.hpp"
#include "../radiant/event.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/input/css/css_parser.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lib/mem_grow.hpp"
#include "../lib/strbuf.h"
#include "../lib/tagged.hpp"
#include "../lib/font/font_internal.h"

DomElement* build_dom_tree_from_element(Element*, DomDocument*, DomElement*);
HashMap* element_dom_map_create(void);
DomElement* element_dom_map_lookup(HashMap*, Element*);

class RadiantCoreTest : public ::testing::Test {
protected:
    ViewTree tree = {};

    void SetUp() override {
        tree.prop_pool = lam::own(pool_create());
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

TEST_F(RadiantCoreTest, TeardownReleasesFormPayloadBelowNonRenderingParents) {
    DomElement* owner = element();
    FormControlProp forms[2] = {};
    NameId tags[] = {MARKUP_NAME_IMG, MARKUP_NAME_OBJECT};
    for (size_t i = 0; i < 2; i++) {
        DomElement* parent = element();
        DomElement* child = element();
        parent->tag_id = tags[i];
        parent->display.inner = RDT_DISPLAY_REPLACED;
        forms[i].custom_validity_msg = mem_strdup("fallback control", MEM_CAT_LAYOUT);
        ASSERT_NE(forms[i].custom_validity_msg, nullptr);
        child->form = lam::view_prop(&forms[i]);
        ASSERT_TRUE(parent->DomNode::append_child(child));
        ASSERT_TRUE(owner->DomNode::append_child(parent));
    }
    tree.root = lam::up(owner);
    tree.destroy();
    for (FormControlProp& form : forms) {
        EXPECT_EQ(form.custom_validity_msg, nullptr);
        mem_free(form.custom_validity_msg);
    }
}

TEST_F(RadiantCoreTest, RetainedResetPreservesScriptCreatedFormProperties) {
    DomElement* control = element();
    FormControlProp form = {};
    form.control_type = FORM_CONTROL_TEXT;
    form.custom_validity_msg = mem_strdup("invalid value", MEM_CAT_LAYOUT);
    ASSERT_NE(form.custom_validity_msg, nullptr);
    control->form = lam::view_prop(&form);
    control->set_role_kind(DomElement::ROLE_FORM);
    tree.root = lam::up(control);

    view_pool_reset_retained_subtree(&tree, control);
    EXPECT_EQ(control->form, &form);
    EXPECT_EQ(control->form_control(), &form);
    EXPECT_STREQ(form.custom_validity_msg, "invalid value");
    tree.destroy();
    EXPECT_EQ(form.custom_validity_msg, nullptr);
    mem_free(form.custom_validity_msg);
}

TEST(TextIntrinsicWrap, BreakAllRetainsPunctuationAndMaxContentWidth) {
    LayoutContext context = {};
    TextIntrinsicWidths normal = measure_text_intrinsic_widths(&context,
        "ABCDEF", 6);
    TextIntrinsicWidths broken = measure_text_intrinsic_widths(&context,
        "ABCDEF", 6, CSS_VALUE_NONE, CSS_VALUE_NONE, CSS_VALUE_NORMAL,
        CSS_VALUE_NORMAL, CSS_VALUE_BREAK_ALL);
    EXPECT_FLOAT_EQ(normal.min_content, normal.max_content);
    EXPECT_FLOAT_EQ(broken.min_content * 6.0f, broken.max_content);
    EXPECT_FLOAT_EQ(broken.max_content, normal.max_content);

    TextIntrinsicWidths punctuation = measure_text_intrinsic_widths(&context,
        "A)B", 3, CSS_VALUE_NONE, CSS_VALUE_NONE, CSS_VALUE_NORMAL,
        CSS_VALUE_NORMAL, CSS_VALUE_BREAK_ALL);
    EXPECT_FLOAT_EQ(punctuation.min_content, punctuation.max_content);
    EXPECT_FALSE(layout_text_allows_letter_break('A', ')', CSS_VALUE_AUTO, false, false));
    EXPECT_FALSE(layout_text_allows_letter_break('A', 0x200D, CSS_VALUE_AUTO, false, false));
    EXPECT_TRUE(layout_text_allows_letter_break('A', 'B', CSS_VALUE_AUTO, false, false));
}

TEST(TextIntrinsicWrap, BreakAllKeepsShapedMaxContentAdvance) {
    FontContextConfig config = {}; config.pixel_ratio = 1;
    FontContext* fonts = font_context_create(&config);
    ASSERT_NE(fonts, nullptr);
    FontStyleDesc descriptor = {"Arial", 20, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    FontHandle* handle = font_resolve(fonts, &descriptor);
    ASSERT_NE(handle, nullptr);
    FontProp font = FONT_PROP_DEFAULT;
    font.font_handle = lam::counted(handle); font.font_size = 20;
    UiContext ui = {}; ui.font_ctx = lam::up(fonts);
    LayoutContext context = {}; context.ui_context = lam::up(&ui);
    context.font = {lam::up(&font), 20};
    const char* cases[] = {"السلام", "لا", "office", "ÁB", "AVAV"};
    for (const char* text : cases) {
        SCOPED_TRACE(text);
        TextIntrinsicWidths normal = measure_text_intrinsic_widths(&context, text, strlen(text));
        const CssEnum wrapping[] = {CSS_VALUE_NORMAL, CSS_VALUE_ANYWHERE, CSS_VALUE_BREAK_WORD};
        for (CssEnum wrap : wrapping) {
            TextIntrinsicWidths broken = measure_text_intrinsic_widths(&context, text, strlen(text),
                CSS_VALUE_NONE, CSS_VALUE_NONE, CSS_VALUE_NORMAL, wrap, CSS_VALUE_BREAK_ALL);
            EXPECT_NEAR(broken.max_content, normal.max_content, 0.01f);
        }
    }
    font_handle_release(handle);
    font_context_destroy(fonts);
}

TEST_F(RadiantCoreTest, ExplicitHashAndEqualityCoverEveryInlineField) {
    InlineProp left = INLINE_PROP_DEFAULT;
    InlineProp right = left;
    EXPECT_TRUE(inline_prop_equal(&left, &right));
    EXPECT_EQ(inline_prop_hash(&left), inline_prop_hash(&right));

    right.svg_stroke_width = 3.5f;
    right.has_svg_stroke_width = true;
    EXPECT_FALSE(inline_prop_equal(&left, &right));
    EXPECT_NE(inline_prop_hash(&left), inline_prop_hash(&right));
}

TEST_F(RadiantCoreTest, EqualParentAndChildPromoteThenCowAtEnsureGate) {
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

TEST_F(RadiantCoreTest, CanonicalIndexReusesAnExistingExactValue) {
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

TEST_F(RadiantCoreTest, LengthMathSubstitutesVariablesBeforeResolvingLeaves) {
    UiContext ui = {};
    LayoutContext context = {};
    context.pool = lam::up(tree.prop_pool.get());
    context.ui_context = lam::up(&ui);
    context.width = 200.0f;
    const struct { const char* expression; float expected; } cases[] = {
        {"var(--missing, 40px)", 40.0f},
        {"calc(var(--missing, 12px) + 8px)", 20.0f},
        {"min(100px, var(--missing, 32px))", 32.0f},
        {"calc(var(--missing, 12px) + 10%)", 32.0f},
        {"calc(var(--missing, var(--inner, 12px)) * 2)", 24.0f},
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.expression);
        CssDeclaration* declaration = css_parse_property_declaration("width", 5,
            entry.expression, strlen(entry.expression), tree.prop_pool);
        ASSERT_NE(declaration, nullptr);
        EXPECT_FLOAT_EQ(resolve_length_value(&context, CSS_PROPERTY_WIDTH,
            declaration->value), entry.expected);
    }
}

TEST_F(RadiantCoreTest, RetainedLeadingRecomputesFromTheCurrentParentCascade) {
    DomElement* parent = element();
    DomElement* child = element();
    child->parent = lam::up(parent);
    child->specified_style = lam::shared(style_tree_create(tree.prop_pool));
    ASSERT_NE(child->specified_style, nullptr);
    parent->ensure_font(&tree)->font_size = 16.0f;
    parent->ensure_block(&tree)->line_height = lam::shared(
        css_value_create_number(tree.prop_pool, 1.4));
    child->ensure_block(&tree)->line_height = lam::shared(
        css_value_create_number(tree.prop_pool, 1.2));
    UiContext ui = {};
    LayoutContext context = {};
    context.pool = lam::up(tree.prop_pool.get());
    context.selected_view_tree = lam::up(&tree);
    context.ui_context = lam::up(&ui);
    context.view = lam::up(static_cast<View*>(child));
    context.font.style = lam::up(parent->font.get());
    context.font.current_font_size = 16.0f;

    resolve_css_styles(child, &context);
    ASSERT_NE(child->block()->line_height, nullptr);
    EXPECT_EQ(child->block()->line_height->type, CSS_VALUE_TYPE_NUMBER);
    EXPECT_DOUBLE_EQ(child->block()->line_height->data.number.value, 1.4);
    CssValue* previous = (CssValue*)parent->block()->line_height.get();
    parent->blk->line_height = lam::shared(
        css_value_create_number(tree.prop_pool, 1.6));
    // a paint-only CSSOM edit can retire the parent's declaration before reflow.
    css_value_destroy_owned(previous, tree.prop_pool);
    ASSERT_EQ(child->block()->line_height->type, CSS_VALUE_TYPE_NUMBER);
    EXPECT_DOUBLE_EQ(child->block()->line_height->data.number.value, 1.4);
    resolve_css_styles(child, &context);
    EXPECT_DOUBLE_EQ(child->block()->line_height->data.number.value, 1.6);

    // HTML control defaults install normal before the author cascade runs.
    child->blk->line_height = lam::shared(css_line_height_normal_value());
    resolve_css_styles(child, &context);
    EXPECT_EQ(child->block()->line_height->type, CSS_VALUE_TYPE_KEYWORD);
    EXPECT_EQ(child->block()->line_height->data.keyword, CSS_VALUE_NORMAL);
}

TEST_F(RadiantCoreTest, AnonymousTableRepairPreservesSourceInheritance) {
    DomDocument doc;
    doc.view_tree = lam::own(&tree);
    LayoutContext context = {};
    context.doc = lam::up(&doc);
    context.pool = lam::up(tree.prop_pool.get());
    context.selected_view_tree = lam::up(&tree);
    DomElement* source = element();
    source->doc = lam::up(&doc);
    source->display = {CSS_VALUE_TABLE_CELL, CSS_VALUE_TABLE_CELL};
    source->set_styles_resolved(true);
    CssValue* leading = css_value_create_number(tree.prop_pool, 1.4);
    source->ensure_block(&tree)->line_height = lam::shared(leading);
    DomElement* retained_wrapper = nullptr;
    const char* values[] = {"inherit", "var(--missing, inherit)"};
    for (const char* value : values) {
        SCOPED_TRACE(value);
        DomElement* child = element();
        child->doc = lam::up(&doc);
        child->specified_style = lam::shared(style_tree_create(tree.prop_pool));
        CssDeclaration* declaration = css_parse_property_declaration(
            "display", 7, value, strlen(value), tree.prop_pool);
        ASSERT_NE(declaration, nullptr);
        ASSERT_TRUE(style_tree_apply_declaration(child->specified_style, declaration));
        child->parent = lam::up(source);
        source->first_child = lam::own(static_cast<DomNode*>(child));
        source->last_child = lam::up(static_cast<DomNode*>(child));
        ASSERT_EQ(resolve_display_value(child).inner, CSS_VALUE_TABLE_CELL);
        ASSERT_TRUE(wrap_orphaned_table_children(&context, source));
        DomElement* wrapper = source->first_child->as_element();
        retained_wrapper = wrapper;
        ASSERT_TRUE(wrapper->is_table_fixup());
        EXPECT_EQ(dom_source_parent(child), source);
        EXPECT_EQ(resolve_display_value(child).inner, CSS_VALUE_TABLE_CELL);
        wrapper->set_styles_resolved(false);
        EXPECT_EQ(resolve_display_value(wrapper).inner, CSS_VALUE_TABLE);
        EXPECT_EQ(resolve_display_value(child).inner, CSS_VALUE_TABLE_CELL);
        ASSERT_NE(wrapper->block()->line_height, nullptr);
        EXPECT_NE(wrapper->block()->line_height.get(), leading);
        EXPECT_DOUBLE_EQ(wrapper->block()->line_height->data.number.value, 1.4);
        source->first_child = nullptr;
        source->last_child = nullptr;
    }
    css_value_destroy_owned(leading, tree.prop_pool);
    source->blk->line_height = nullptr;
    EXPECT_DOUBLE_EQ(retained_wrapper->block()->line_height->data.number.value, 1.4);
    doc.view_tree = nullptr;
}

TEST_F(RadiantCoreTest, IntrinsicGridUsesResolvedTracksInsteadOfAuthoredLineNames) {
    DomElement* grid_element = element();
    grid_element->view_type = RDT_VIEW_BLOCK;
    grid_element->display.outer = CSS_VALUE_BLOCK;
    grid_element->display.inner = CSS_VALUE_GRID;
    grid_element->set_styles_resolved(true);
    grid_element->specified_style = lam::shared(style_tree_create(tree.prop_pool));
    const char* template_text = "[start] 80px [end]";
    CssDeclaration* declaration = css_parse_property_declaration(
        "grid-template-columns", strlen("grid-template-columns"),
        template_text, strlen(template_text), tree.prop_pool);
    ASSERT_NE(declaration, nullptr);
    ASSERT_TRUE(style_tree_apply_declaration(grid_element->specified_style, declaration));
    GridProp* grid = (GridProp*)pool_calloc(tree.prop_pool, sizeof(GridProp));
    ASSERT_NE(grid, nullptr);
    grid_element->ensure_embed(&tree)->grid = lam::own(grid);
    grid->grid_template_columns = lam::own(create_grid_track_list(tree.prop_pool, 3));
    ASSERT_NE(grid->grid_template_columns, nullptr);
    GridTrackList* tracks = grid->grid_template_columns;
    tracks->track_count = 1;
    tracks->tracks[0] = create_grid_track_size(tree.prop_pool, GRID_TRACK_SIZE_LENGTH, 80.0f);
    ASSERT_NE(tracks->tracks[0], nullptr);
    // Spare capacity must never become authored line-name columns during measurement.
    GridTrackSize* spare = create_grid_track_size(tree.prop_pool, GRID_TRACK_SIZE_LENGTH, 400.0f);
    ASSERT_NE(spare, nullptr);
    tracks->tracks[1] = tracks->tracks[2] = spare;
    LayoutContext context = {};
    context.pool = lam::up(tree.prop_pool.get());
    context.selected_view_tree = lam::up(&tree);
    context.profiler.enabled = true;
    Arena* scratch = arena_create_default();
    ASSERT_NE(scratch, nullptr);
    scratch_init(&context.scratch, scratch);
    IntrinsicSizes sizes = measure_element_intrinsic_widths(&context, grid_element);
    EXPECT_FLOAT_EQ(sizes.min_content, 80.0f);
    EXPECT_FLOAT_EQ(sizes.max_content, 80.0f);
    IntrinsicSizes repeated = measure_element_intrinsic_widths(&context, grid_element);
    EXPECT_FLOAT_EQ(repeated.min_content, sizes.min_content);
    EXPECT_FLOAT_EQ(repeated.max_content, sizes.max_content);
    EXPECT_EQ(context.profiler.intrinsic_cache_hits, 1u);
    scratch_release(&context.scratch);
    arena_destroy(scratch);
}

TEST(GridStorage, SparseOccupancyRetainsNegativeExpansionAndOverwrites) {
    using namespace radiant::grid;
    // a dense 10000-by-10000 allocation used to exceed the independent cell cap.
    CellOccupancyMatrix matrix(TrackCounts(0, 10000, 0), TrackCounts(0, 10000, 0));
    matrix.mark_area_as(AbsoluteAxis::Horizontal, grid_clamp_line_span(-10000, -9999),
                        grid_clamp_line_span(9999, 10000), CellOccupancyState::DefinitelyPlaced);
    matrix.mark_area_as(AbsoluteAxis::Horizontal, grid_clamp_line_span(9999, 10000),
                        grid_clamp_line_span(-4, -3), CellOccupancyState::AutoPlaced);
    ASSERT_TRUE(matrix.good());
    EXPECT_EQ(matrix.cols(), 20000u);
    EXPECT_EQ(matrix.rows(), 10004u);
    EXPECT_EQ(matrix.get(10003, 0), CellOccupancyState::DefinitelyPlaced);
    EXPECT_EQ(matrix.get(0, 19999), CellOccupancyState::AutoPlaced);
    OriginZeroLine found;
    ASSERT_TRUE(matrix.last_of_type(AbsoluteAxis::Horizontal, OriginZeroLine(9999),
                                    CellOccupancyState::DefinitelyPlaced, found));
    EXPECT_EQ(found.value, -10000);
    ASSERT_TRUE(matrix.last_of_type(AbsoluteAxis::Vertical, OriginZeroLine(9999),
                                    CellOccupancyState::AutoPlaced, found));
    EXPECT_EQ(found.value, -4);
    matrix.set(10003, 0, CellOccupancyState::Unoccupied);
    EXPECT_TRUE(matrix.line_area_is_unoccupied(AbsoluteAxis::Horizontal,
        grid_clamp_line_span(-10000, -9999), grid_clamp_line_span(9999, 10000)));
    EXPECT_FALSE(matrix.track_area_is_unoccupied(AbsoluteAxis::Horizontal, 19999, 20000, 0, 1));
    EXPECT_EQ(matrix.get(0, 19999), CellOccupancyState::AutoPlaced);
}

TEST(GridGrammar, PlacementAndTrackListsRejectInvalidDeclarationsBeforeCascade) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    const char* valid[] = {"grid-column-start:Foo 2 span", "grid-column-start:SPAN Foo 2",
        "grid-column-start:calc(1.6)", "grid-column-start:span calc(0)", "grid-column-start:2147483648",
        "grid-area:Foo / Bar", "grid-column:2 / span 3", "grid-area:var(--area)",
        "grid-template-columns:[Red] repeat(2,[blue] 10px [tail]) [end]"};
    for (const char* text : valid) { SCOPED_TRACE(text); EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr); }
    const char* invalid[] = {"grid-column-start:Foo span 2", "grid-column-start:span", "grid-column-start:2.0",
        "grid-column-start:0", "grid-column-start:span -1", "grid-column-start:Foo Bar", "grid-column-start:auto 2",
        "grid-column-start:calc(0)", "grid-column:2 /", "grid-column:2 / 3 / 4", "grid-area:Foo / / 3",
        "grid-template-columns:[a][b] 10px", "grid-template-columns:10px [a][b]",
        "grid-template-columns:repeat(2,[a][b] 10px)"};
    for (const char* text : invalid) { SCOPED_TRACE(text); EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr); }
    pool_destroy(pool);
}

TEST(GridGrammar, RepeatMathRoundsAtComputationAndChecksLiteralGrammar) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    const char* valid[] = {"2", "calc(1 + 1)", "calc(1.5)", "calc(-2)", "calc(0 / 0)",
        "calc(infinity)", "calc(-infinity)", "min(2,3)", "clamp(1,2.6,4)",
        "calc(2 * 10px / 5px)", "calc(2em / 1em)", "calc(sign(100em - 1px))"};
    for (const char* amount : valid) {
        SCOPED_TRACE(amount);
        StringBuf* text = stringbuf_new(pool);
        stringbuf_append_format(text,"repeat(%s, [a] 10px [b] 20px [c])",amount);
        EXPECT_NE(css_parse_property_declaration("grid-template-columns",21,
            text->str->chars,text->length,pool),nullptr);
    }
    const char* invalid[] = {"2.0", "2e0", "0", "-2", "calc(1px)", "calc(2%)", "calc(1 + 1px)"};
    for (const char* amount : invalid) {
        SCOPED_TRACE(amount);
        StringBuf* text = stringbuf_new(pool);
        stringbuf_append_format(text,"repeat(%s, 10px)",amount);
        EXPECT_EQ(css_parse_property_declaration("grid-template-columns",21,
            text->str->chars,text->length,pool),nullptr);
    }
    struct Case { const char* amount; double result; };
    const Case cases[] = {{"2",2},{"calc(1.5)",2},{"calc(0.5)",1},{"calc(-2)",1},
        {"calc(0 / 0)",1},{"calc(infinity)",10001},{"calc(-infinity)",1},
        {"calc(2 * 10px / 5px)",4},{"clamp(1,2.6,4)",3}};
    CssMathEvaluationContext context = {};
    for (const Case& item : cases) {
        SCOPED_TRACE(item.amount);
        const CssValue* value = css_parse_component_value_text(item.amount,strlen(item.amount),pool);
        double result = 0;
        ASSERT_TRUE(css_compute_integer_value(value,&context,1,10001,&result));
        EXPECT_EQ(result,item.result);
    }
    pool_destroy(pool);
}

TEST(LineClampGrammar, RejectsNonIntegerTokensAndRetainsCalculatedCounts) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    const char* properties[] = {"line-clamp", "-webkit-line-clamp"};
    const char* valid[] = {"none", "1", "2", "inherit", "initial", "unset", "revert-layer",
        "calc(1.5)", "calc(-1)", "calc(20px / 1em)", "min(1, 2)",
        "calc(infinity)", "calc(0 / 0)", "var(--count)"};
    const char* invalid[] = {"0", "-1", "2.0", "2e0", "2.5", "2px", "2%", "auto",
        "2 3", "none 2", "calc(1px)", "calc(1%)", "calc(1 + 1px)", "initial 2"};
    for (const char* property : properties) {
        SCOPED_TRACE(property);
        for (const char* value : valid) {
            SCOPED_TRACE(value);
            EXPECT_NE(css_parse_property_declaration(property, strlen(property), value, strlen(value), pool), nullptr);
        }
        for (const char* value : invalid) {
            SCOPED_TRACE(value);
            EXPECT_EQ(css_parse_property_declaration(property, strlen(property), value, strlen(value), pool), nullptr);
        }
        CssDeclaration* declaration = css_parse_property_declaration(property, strlen(property),
            "min(1, 2)", 9, pool);
        ASSERT_NE(declaration, nullptr);
        CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
        css_format_value_with_property(formatter, declaration->value, declaration->property_code);
        EXPECT_STREQ(stringbuf_to_string(formatter->output)->chars, "calc(1)");
    }
    pool_destroy(pool);
}

TEST(LegacyBoxGrammar, OrientationAndDisplayRejectInvalidTokens) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    const char* valid[] = {"horizontal", "vertical", "inline-axis", "block-axis",
        "inherit", "initial", "unset", "revert-layer", "var(--axis)"};
    const char* invalid[] = {"normal", "row", "column", "inline", "block", "0",
        "vertical horizontal", "initial vertical"};
    for (const char* value : valid) {
        SCOPED_TRACE(value);
        EXPECT_NE(css_parse_property_declaration("-webkit-box-orient",18,value,strlen(value),pool),nullptr);
    }
    for (const char* value : invalid) {
        SCOPED_TRACE(value);
        EXPECT_EQ(css_parse_property_declaration("-webkit-box-orient",18,value,strlen(value),pool),nullptr);
    }
    const char* displays[] = {"-webkit-box", "-webkit-inline-box"};
    for (const char* value : displays) {
        SCOPED_TRACE(value);
        EXPECT_NE(css_parse_property_declaration("display",7,value,strlen(value),pool),nullptr);
    }
    pool_destroy(pool);
}

TEST(GridGrammar, PlacementMathRetainsExpressionsAcrossProjectionAndOwnerChanges) {
    Pool* source = pool_create(), *owner = pool_create();
    const char* text = "calc(20px / 1em) / span calc(1.6)";
    CssDeclaration* declaration = css_parse_property_declaration("grid-column",11,text,strlen(text),source);
    ASSERT_NE(declaration,nullptr);
    CssGridLineReference lines[4] = {};
    ASSERT_TRUE(css_grid_parse_placement(declaration->value,CSS_PROPERTY_GRID_COLUMN,lines));
    ASSERT_NE(lines[1].number_expression,nullptr); ASSERT_NE(lines[3].number_expression,nullptr);
    String* specified = css_grid_serialize_placement(owner,lines,CSS_PROPERTY_GRID_COLUMN);
    ASSERT_NE(specified,nullptr); EXPECT_STREQ(specified->chars,text);
    const CssValue* start = css_grid_placement_longhand(owner,declaration->value,
        CSS_PROPERTY_GRID_COLUMN,CSS_PROPERTY_GRID_COLUMN_START);
    pool_destroy(source);
    ASSERT_NE(start,nullptr);
    double font = 10;
    auto leaf = [](void* data,const CssValue* value,double* result) {
        if (value->type != CSS_VALUE_TYPE_LENGTH) return false;
        if (value->data.length.unit == CSS_UNIT_EM) {
            *result = *(double*)data * value->data.length.value; return true;
        }
        return css_absolute_length_to_px(value->data.length.unit,value->data.length.value,result);
    };
    CssMathEvaluationContext math = {leaf,&font,1.0,false};
    ASSERT_TRUE(css_grid_parse_placement(start,CSS_PROPERTY_GRID_COLUMN_START,lines,&math));
    EXPECT_EQ(lines[1].number,2); EXPECT_EQ(lines[1].number_expression,nullptr);
    font = 20;
    ASSERT_TRUE(css_grid_parse_placement(start,CSS_PROPERTY_GRID_COLUMN_START,lines,&math));
    EXPECT_EQ(lines[1].number,1);
    const char* zero = "calc(20px / 1em - 1)";
    const CssValue* deferred_zero = css_parse_component_value_text(zero,strlen(zero),owner);
    ASSERT_TRUE(css_grid_parse_placement(deferred_zero,CSS_PROPERTY_GRID_COLUMN_START,lines,&math));
    EXPECT_EQ(lines[1].number,1);
    const char* invalid[] = {"initial Foo", "span unset", "inherit 2", "revert-layer Foo"};
    for (const char* text : invalid) {
        SCOPED_TRACE(text);
        EXPECT_EQ(css_parse_property_declaration("grid-column-start",17,text,strlen(text),owner),nullptr);
    }
    String* retained = css_serialize_integer_math(owner,start);
    ASSERT_NE(retained,nullptr); EXPECT_STREQ(retained->chars,"calc(20px / 1em)");
    pool_destroy(owner);
}

TEST(GridGrammar, AreaRowsValidateDecodedRectanglesAndCanonicalizeCells) {
    Pool* pool = pool_create(); ASSERT_NE(pool, nullptr);
    struct Case { const char* source; const char* canonical; size_t rows, columns; };
    const Case valid[] = {
        {"\"a   ...b\" \"a . b\"", "\"a . b\" \"a . b\"", 2, 3},
        {"\"1st é 😀\"", "\"1st é 😀\"", 1, 3},
        {"\"a\\000020b\"", "\"a b\"", 1, 2},
        {"\"a\\00000ab\"", "\"a b\"", 1, 2},
        {"\"a\\61\"", "\"aa\"", 1, 1},
        {"\"A a\"", "\"A a\"", 1, 2},
        {"\".... ..\"", "\". .\"", 1, 2},
        {"none", "none", 0, 0}
    };
    const char* property = "grid-template-areas";
    for (const Case& item : valid) {
        SCOPED_TRACE(item.source);
        CssDeclaration* declaration = css_parse_property_declaration(property, strlen(property),
            item.source, strlen(item.source), pool);
        ASSERT_NE(declaration, nullptr);
        CssGridAreaDimensions dimensions = {};
        ASSERT_TRUE(css_grid_visit_template_areas(declaration->value, &dimensions));
        EXPECT_EQ(dimensions.rows, item.rows);
        EXPECT_EQ(dimensions.columns, item.columns);
        EXPECT_STREQ(css_serialize_declaration_value(declaration, pool), item.canonical);
    }
    const char* invalid[] = {"\"a a\" \"a .\"", "\"a . a\"", "\"a a\" \"b\"",
        "\"\"", "\"  \"", "\"a@b\"", "\"a,b\"", "\"a\" none", "a", "\"a\", \"b\""};
    for (const char* text : invalid) {
        SCOPED_TRACE(text);
        EXPECT_EQ(css_parse_property_declaration(property, strlen(property), text, strlen(text), pool), nullptr);
    }
    CssValue rows[2] = {}, list = {};
    CssValue* values[] = {&rows[0], &rows[1]};
    rows[0].type = rows[1].type = CSS_VALUE_TYPE_STRING;
    rows[0].data.string = "a a"; rows[1].data.string = "a .";
    list.type = CSS_VALUE_TYPE_LIST; list.data.list = {values, 2, false};
    CssGridAreaDimensions dimensions{99, 99};
    size_t visits = 0;
    EXPECT_FALSE(css_grid_visit_template_areas(&list, &dimensions,
        [](const CssGridAreaBounds&, void* context) { (*(size_t*)context)++; return true; }, &visits));
    EXPECT_EQ(visits, 0u);
    EXPECT_EQ(dimensions.rows, 0u);
    EXPECT_EQ(dimensions.columns, 0u);
    pool_destroy(pool);
}

TEST(GridGrammar, DefinitionFormsShareTracksAndOwnProjectedAreaNames) {
    Pool* source = pool_create(); Pool* owner = pool_create();
    ASSERT_NE(source, nullptr); ASSERT_NE(owner, nullptr);
    struct Case { const char* value; const char* canonical; };
    const Case common[] = {{"none", "none"}, {"none / none", "none"}, {"none / 20px", "none / 20px"},
        {"10px / none", "10px / none"}, {"10px / 20px", "10px / 20px"}, {"\"a b\"", "\"a b\""},
        {"\"a b\" 10px \"c d\" 20px / 30px 40px", "\"a b\" 10px \"c d\" 20px / 30px 40px"},
        {"[top] \"a a\" 10px [mid] [next] \"b b\" [end] / [l] 20px [m] 30px [r]",
            "[top] \"a a\" 10px [mid next] \"b b\" [end] / [l] 20px [m] 30px [r]"},
        {"repeat(2,10px) / minmax(0,1fr)", "repeat(2, 10px) / minmax(0px, 1fr)"},
        {"1em / repeat(2,[Em] 2em [tail])", "1em / repeat(2, [Em] 2em [tail])"},
        {"var(--definition)", "var(--definition)"}, {"inherit", "inherit"}};
    const char* properties[] = {"grid-template", "grid"};
    for (const char* property : properties) for (const Case& item : common) {
        SCOPED_TRACE(property);
        SCOPED_TRACE(item.value);
        CssDeclaration* declaration = css_parse_property_declaration(property, strlen(property),
            item.value, strlen(item.value), source);
        ASSERT_NE(declaration, nullptr);
        EXPECT_STREQ(css_serialize_declaration_value(declaration, owner), item.canonical);
    }
    const char* flows[] = {"auto-flow 10px 20px / 30px", "dense auto-flow minmax(10px,20px) / repeat(2,30px)",
        "10px 20px / auto-flow dense 30px", "auto-flow / 20px"};
    for (const char* value : flows) {
        SCOPED_TRACE(value);
        EXPECT_NE(css_parse_property_declaration("grid", 4, value, strlen(value), source), nullptr);
        EXPECT_EQ(css_parse_property_declaration("grid-template", 13, value, strlen(value), source), nullptr);
    }
    const char* invalid[] = {"\"a b\" / repeat(2,20px)", "\"a b\" / none", "\"a a\" \"a .\"",
        "10px", "10px /", "/ 20px", "10px / 20px / 30px", "auto-flow / auto-flow", "dense / 20px",
        "auto-flow 20px dense / 30px", "\"a\" -1px", "\"a\" calc(1px + 1)", "[auto] \"a\"",
        "[a][b][c] \"a\""};
    for (const char* property : properties) for (const char* value : invalid) {
        SCOPED_TRACE(property);
        SCOPED_TRACE(value);
        EXPECT_EQ(css_parse_property_declaration(property, strlen(property), value, strlen(value), source), nullptr);
    }
    const char* text = "[top] \"a a\" 10px [mid] [next] \"b b\" [end] / 20px 30px";
    CssDeclaration* declaration = css_parse_property_declaration("grid", 4, text, strlen(text), source);
    ASSERT_NE(declaration, nullptr);
    const CssValue* rows = css_grid_shorthand_longhand(owner, declaration->value, CSS_PROPERTY_GRID,
        CSS_PROPERTY_GRID_TEMPLATE_ROWS);
    const CssValue* areas = css_grid_shorthand_longhand(owner, declaration->value, CSS_PROPERTY_GRID,
        CSS_PROPERTY_GRID_TEMPLATE_AREAS);
    const CssValue* automatic = css_grid_shorthand_longhand(owner, declaration->value, CSS_PROPERTY_GRID,
        CSS_PROPERTY_GRID_AUTO_COLUMNS);
    pool_destroy(source);
    ASSERT_NE(rows, nullptr); ASSERT_EQ(rows->type, CSS_VALUE_TYPE_LIST);
    ASSERT_EQ(rows->data.list.count, 12);
    EXPECT_STREQ(css_math_token_name(rows->data.list.values[1]), "top");
    EXPECT_STREQ(css_math_token_name(rows->data.list.values[6]), "next");
    EXPECT_EQ(rows->data.list.values[8]->data.keyword, CSS_VALUE_AUTO);
    CssGridAreaDimensions dimensions = {};
    ASSERT_TRUE(css_grid_visit_template_areas(areas, &dimensions));
    EXPECT_EQ(dimensions.rows, 2u); EXPECT_EQ(dimensions.columns, 2u);
    ASSERT_NE(automatic, nullptr); EXPECT_EQ(automatic->data.keyword, CSS_VALUE_AUTO);
    pool_destroy(owner);
}

TEST(GridStorage, PlacementRetainsEveryItemBeyondPreviousCapacityInBothFlows) {
    using namespace radiant::grid;
    const GridAutoFlow flows[] = {GridAutoFlow::Row, GridAutoFlow::Column};
    for (GridAutoFlow flow : flows) {
        ItemInfoArray items;
        for (int i = 0; i < 300; i++) {
            GridItemInfo item;
            item.item_index = i;
            ASSERT_TRUE(items.push_back(item));
        }
        CellOccupancyMatrix matrix(TrackCounts(0, 1, 0), TrackCounts(0, 1, 0));
        place_grid_items(matrix, items, flow, 1, 1);
        ASSERT_TRUE(matrix.good());
        EXPECT_EQ(items.size(), 300u);
        const GridItemInfo& last = items.back();
        EXPECT_EQ(flow == GridAutoFlow::Row ? last.resolved_row.start.value : last.resolved_column.start.value, 299);
        EXPECT_EQ(flow == GridAutoFlow::Row ? matrix.rows() : matrix.cols(), 300u);
    }
}

TEST(GridStorage, EmptyExplicitGridUsesOnlyTheDefiniteNegativeTrackInBothFlows) {
    using namespace radiant::grid;
    const GridAutoFlow flows[] = {GridAutoFlow::Row, GridAutoFlow::Column};
    for (GridAutoFlow flow : flows) {
        ItemInfoArray items;
        GridItemInfo item;
        if (flow == GridAutoFlow::Row) item.column = GridPlacement::FromStartSpan(-2,1);
        else item.row = GridPlacement::FromStartSpan(-2,1);
        ASSERT_TRUE(items.push_back(item));
        CellOccupancyMatrix matrix;
        place_grid_items(matrix,items,flow,0,0);
        EXPECT_EQ(matrix.cols(),1u); EXPECT_EQ(matrix.rows(),1u);
        AbsoluteAxis axis = primary_axis(flow);
        EXPECT_EQ(matrix.track_counts(axis).explicit_count,0);
        EXPECT_EQ(matrix.track_counts(axis).negative_implicit,1);
        EXPECT_EQ(matrix.track_counts(axis).positive_implicit,0);
    }
}

TEST(GridStorage, LockedAxisPackingAndCursorTransposeAcrossBothFlows) {
    using namespace radiant::grid;
    const GridAutoFlow flows[] = {GridAutoFlow::Row,GridAutoFlow::RowDense,
        GridAutoFlow::Column,GridAutoFlow::ColumnDense};
    for (GridAutoFlow flow : flows) {
        bool row = primary_axis(flow) == AbsoluteAxis::Horizontal;
        for (bool locked : {true,false}) {
            SCOPED_TRACE(locked ? "locked" : "cursor");
            ItemInfoArray items;
            auto append = [&](GridPlacement primary, GridPlacement secondary) {
                GridItemInfo item;
                item.column = row ? primary : secondary;
                item.row = row ? secondary : primary;
                EXPECT_TRUE(items.push_back(item));
            };
            append(GridPlacement::FromStartSpan(2,1),GridPlacement::FromStartSpan(1,1));
            append(GridPlacement::Auto(2),locked ? GridPlacement::FromStartSpan(1,1) : GridPlacement::Auto());
            append(locked ? GridPlacement::Auto() : GridPlacement::FromStartSpan(3,1),
                locked ? GridPlacement::FromStartSpan(1,1) : GridPlacement::Auto());
            if (!locked) append(GridPlacement::Auto(),GridPlacement::Auto());
            CellOccupancyMatrix matrix(TrackCounts(0,row ? 3 : 2,0),TrackCounts(0,row ? 2 : 3,0));
            place_grid_items(matrix,items,flow,row ? 2 : 3,row ? 3 : 2);
            ASSERT_TRUE(matrix.good());
            auto expect = [&](size_t index,int primary,int secondary) {
                EXPECT_EQ((row ? items[index].resolved_column : items[index].resolved_row).start.value,primary);
                EXPECT_EQ((row ? items[index].resolved_row : items[index].resolved_column).start.value,secondary);
            };
            expect(0,1,0);
            if (locked) {
                expect(1,2,0); expect(2,is_dense(flow) ? 0 : 4,0);
                EXPECT_EQ(matrix.track_counts(primary_axis(flow)).len(),is_dense(flow) ? 4 : 5);
            } else {
                expect(1,0,1); expect(2,2,is_dense(flow) ? 0 : 1);
                expect(3,0,is_dense(flow) ? 0 : 2);
            }
        }
    }
}

TEST(GridStorage, ExtentClipsCrossingAreasAndMovesWhollyOutsideAreas) {
    using namespace radiant::grid;
    struct Case { int64_t start, end; int16_t expected_start, expected_end; };
    const Case cases[] = {
        {9998, INT64_MAX, 9998, 10000}, {INT64_MIN, -9998, -10000, -9998},
        {INT64_MAX - 1, INT64_MAX, 9999, 10000}, {INT64_MIN, INT64_MIN + 1, -10000, -9999},
        {INT64_MAX, INT64_MIN, -10000, 10000}, {10000, 10000, 9999, 10000}
    };
    for (const Case& item : cases) {
        LineSpan span = grid_clamp_line_span(item.start, item.end);
        EXPECT_EQ(span.start.value, item.expected_start);
        EXPECT_EQ(span.end.value, item.expected_end);
    }
    LineSpan negative = GridPlacement::FromNegativeLines(-32000, -31999).to_origin_zero(10000);
    EXPECT_EQ(negative.start.value, -10000);
    EXPECT_EQ(negative.end.value, -9999);
}

TEST(GridStorage, AllocationFailureRetainsExistingEntriesAndPropagatesToSizingOwner) {
    using namespace radiant::grid;
    TrackArray tracks;
    ASSERT_TRUE(tracks.push_back(EnhancedGridTrack()));
    IndexArray indices(&tracks.complete);
    ASSERT_TRUE(indices.push_back(0));
    memtrack_fault_inject(0);
    bool reserved = indices.reserve(indices.capacity() + 1);
    memtrack_fault_clear();
    EXPECT_FALSE(reserved);
    EXPECT_FALSE(indices.good());
    EXPECT_FALSE(tracks.good());
    ASSERT_EQ(indices.size(), 1u);
    EXPECT_EQ(indices[0], 0u);
    CellOccupancyMatrix matrix(TrackCounts(0, 10000, 0), TrackCounts(0, 10000, 0));
    memtrack_fault_inject(0);
    matrix.mark_area_as(AbsoluteAxis::Horizontal, grid_clamp_line_span(0, 1),
                        grid_clamp_line_span(0, 1), CellOccupancyState::AutoPlaced);
    memtrack_fault_clear();
    EXPECT_FALSE(matrix.good());
    EXPECT_EQ(matrix.cols(), 10000u);
    EXPECT_EQ(matrix.rows(), 10000u);
}

TEST_F(RadiantCoreTest, GridAreasRetainLongNamesAndFullDimensions) {
    GridProp grid = {};
    auto project = [&](const char* text) {
        CssDeclaration* declaration = css_parse_property_declaration("grid-template-areas", 19,
            text, strlen(text), tree.prop_pool);
        EXPECT_NE(declaration, nullptr);
        if (declaration) parse_grid_template_areas(tree.prop_pool, &grid, declaration->value);
    };
    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    strbuf_append_char(source, '"');
    for (int i = 0; i < 40; i++) {
        if (i) strbuf_append_char(source, ' ');
        strbuf_append_str(source, "long_area_name_that_must_not_be_split_into_separate_cells_");
        strbuf_append_int(source, i);
    }
    strbuf_append_char(source, '"');
    project(source->str);
    strbuf_free(source);
    ASSERT_EQ(grid.area_count, 40);
    EXPECT_EQ(grid.template_area_rows, 1);
    EXPECT_EQ(grid.template_area_columns, 40);
    EXPECT_STREQ(grid.grid_areas[39].name, "long_area_name_that_must_not_be_split_into_separate_cells_39");
    EXPECT_EQ(grid.grid_areas[39].column_start, 40);
    EXPECT_EQ(grid.grid_areas[39].column_end, 41);
    project("\"a...b\"");
    ASSERT_EQ(grid.area_count, 2);
    EXPECT_EQ(grid.template_area_columns, 3);
    EXPECT_EQ(grid.grid_areas[1].column_start, 3);
    project("\"a a\" \"a a\"");
    ASSERT_EQ(grid.area_count, 1);
    EXPECT_EQ(grid.template_area_rows, 2);
    EXPECT_EQ(grid.grid_areas[0].row_end, 3);
    EXPECT_EQ(grid.grid_areas[0].column_end, 3);
    project("\".... ..\"");
    EXPECT_EQ(grid.area_count, 0);
    EXPECT_EQ(grid.template_area_rows, 1);
    EXPECT_EQ(grid.template_area_columns, 2);
    clear_grid_template_areas(tree.prop_pool, &grid);
    EXPECT_EQ(grid.template_area_columns, 0);
    EXPECT_EQ(grid.template_area_rows, 0);
    lam::free_owned(tree.prop_pool, grid.grid_areas);
}

TEST_F(RadiantCoreTest, GridTrackCloneOwnsMathAndNamesAfterSourcePoolRetirement) {
    Pool* source_pool = pool_create();
    ASSERT_NE(source_pool, nullptr);
    const char* text = "calc(10px + 25%)";
    CssDeclaration* declaration = css_parse_property_declaration(
        "grid-template-columns", strlen("grid-template-columns"), text, strlen(text), source_pool);
    ASSERT_NE(declaration, nullptr);
    GridTrackList* source = create_grid_track_list(source_pool, 2);
    ASSERT_NE(source, nullptr);
    source->tracks[0] = create_grid_track_size(source_pool, GRID_TRACK_SIZE_PERCENTAGE, 0.0f);
    ASSERT_NE(source->tracks[0], nullptr);
    source->track_count = 2;
    GridTrackSize* repeat = create_grid_track_size(source_pool, GRID_TRACK_SIZE_REPEAT, 0.0f);
    ASSERT_NE(repeat, nullptr);
    source->tracks[1] = repeat;
    repeat->is_auto_fill = true;
    repeat->repeat_tracks = lam::own_arr((GridTrackSize**)pool_calloc(source_pool, sizeof(GridTrackSize*)));
    ASSERT_NE(repeat->repeat_tracks, nullptr);
    repeat->repeat_tracks[0] = create_grid_track_size(source_pool, GRID_TRACK_SIZE_LENGTH, 40.0f);
    ASSERT_NE(repeat->repeat_tracks[0], nullptr);
    repeat->repeat_track_count = 1;
    ASSERT_TRUE(grid_add_track_line_name(source_pool, nullptr, &source->line_names, "foo bar", 0));
    ASSERT_TRUE(grid_add_track_line_name(source_pool, nullptr, &source->line_names, "foo", 0));
    ASSERT_TRUE(grid_add_track_line_name(source_pool, nullptr, &source->line_names, "end", 2));
    ASSERT_TRUE(grid_add_track_line_name(source_pool, nullptr, &repeat->repeat_line_names, "s", 0));
    ASSERT_TRUE(grid_add_track_line_name(source_pool, nullptr, &repeat->repeat_line_names, "e", 1));
    source->tracks[0]->expression_owner = lam::own(css_value_clone_owned(declaration->value, source_pool));
    source->tracks[0]->expression = lam::up((const CssValue*)source->tracks[0]->expression_owner.get());
    ASSERT_NE(source->tracks[0]->expression, nullptr);
    GridTrackList* clone = clone_grid_track_list(tree.prop_pool, source);
    ASSERT_NE(clone, nullptr);
    ASSERT_NE(clone->tracks[0]->expression, source->tracks[0]->expression);
    destroy_grid_track_list(source_pool, source);
    pool_destroy(source_pool);
    EXPECT_FLOAT_EQ(radiant::resolve_computed_length_percentage(clone->tracks[0]->expression, 200.0f), 60.0f);
    EXPECT_FLOAT_EQ(radiant::resolve_computed_length_percentage(clone->tracks[0]->expression, 100.0f), 35.0f);
    ASSERT_EQ(clone->line_names.count, 3);
    EXPECT_STREQ(clone->line_names.entries[0].name, "foo bar");
    EXPECT_STREQ(clone->line_names.entries[1].name, "foo");
    EXPECT_EQ(clone->line_names.entries[2].line_index, 2);
    ASSERT_NE(clone->tracks[1], nullptr);
    EXPECT_FLOAT_EQ(clone->tracks[1]->repeat_tracks[0]->value, 40.0f);
    ASSERT_EQ(clone->tracks[1]->repeat_line_names.count, 2);
    EXPECT_STREQ(clone->tracks[1]->repeat_line_names.entries[0].name, "s");
    EXPECT_STREQ(clone->tracks[1]->repeat_line_names.entries[1].name, "e");
    EXPECT_EQ(clone->tracks[1]->repeat_line_names.entries[1].line_index, 1);
    destroy_grid_track_list(tree.prop_pool, clone);
}

TEST_F(RadiantCoreTest, ProvisionalIntrinsicQueriesReuseAndInvalidateAuthoredSizes) {
    DomElement* block = element();
    block->tag_id = MARKUP_NAME_DIV;
    block->view_type = RDT_VIEW_BLOCK;
    block->specified_style = lam::shared(style_tree_create(tree.prop_pool));
    ASSERT_NE(block->specified_style, nullptr);
    DomDocument doc;
    doc.view_tree = lam::own(&tree);
    block->doc = lam::up(&doc);
    LayoutContext context = {};
    context.doc = lam::up(&doc);
    context.pool = lam::up(tree.prop_pool.get());
    context.selected_view_tree = lam::up(&tree);
    context.profiler.enabled = true;
    FontProp font = FONT_PROP_DEFAULT;
    context.font.style = lam::up(&font);
    context.font.current_font_size = 16.0f;
    Arena* scratch = arena_create_default();
    ASSERT_NE(scratch, nullptr);
    scratch_init(&context.scratch, scratch);

    auto set_property = [&](const char* name, const char* value) {
        CssDeclaration* declaration = css_parse_property_declaration(
            name, strlen(name), value, strlen(value), tree.prop_pool);
        EXPECT_NE(declaration, nullptr);
        if (declaration) EXPECT_TRUE(style_tree_apply_declaration(
            block->specified_style, declaration));
        block->set_needs_style_recompute(true);
    };
    set_property("width", "40px");
    IntrinsicSizes first = measure_element_intrinsic_widths(&context, block);
    EXPECT_FLOAT_EQ(first.min_content, 40.0f);
    EXPECT_FALSE(block->styles_resolved());
    IntrinsicSizes repeated = measure_element_intrinsic_widths(&context, block);
    EXPECT_FLOAT_EQ(repeated.max_content, 40.0f);
    EXPECT_EQ(context.profiler.intrinsic_cache_hits, 1u);

    // flex sizing alternates width and height queries before committing styles.
    calculate_max_content_height(&context, block, 40.0f);
    repeated = measure_element_intrinsic_widths(&context, block);
    EXPECT_FLOAT_EQ(repeated.max_content, 40.0f);
    EXPECT_EQ(context.profiler.intrinsic_cache_hits, 2u);

    set_property("width", "80px");
    IntrinsicSizes changed = measure_element_intrinsic_widths(&context, block);
    EXPECT_FLOAT_EQ(changed.min_content, 80.0f);
    EXPECT_FLOAT_EQ(changed.max_content, 80.0f);
    uint64_t misses = context.profiler.intrinsic_cache_misses;
    advance_measurement_cache_generation(&tree);
    changed = measure_element_intrinsic_widths(&context, block);
    EXPECT_FLOAT_EQ(changed.max_content, 80.0f);
    EXPECT_GT(context.profiler.intrinsic_cache_misses, misses);

    // percentage terms depend on the containing width within the same pass.
    set_property("width", "calc(20px + 50%)");
    auto measure_at_width = [&](float width) {
        context.width = width;
        context.block.content_width = width;
        context.available_space.width = AvailableSize::make_definite(width);
        return measure_element_intrinsic_widths(&context, block);
    };
    changed = measure_at_width(100.0f);
    EXPECT_FLOAT_EQ(changed.max_content, 70.0f);
    changed = measure_at_width(200.0f);
    EXPECT_FLOAT_EQ(changed.max_content, 120.0f);
    uint64_t hits = context.profiler.intrinsic_cache_hits;
    repeated = measure_element_intrinsic_widths(&context, block);
    EXPECT_FLOAT_EQ(repeated.max_content, 120.0f);
    EXPECT_EQ(context.profiler.intrinsic_cache_hits, hits + 1u);

    // nested flex queries return to earlier constraints before committing layout.
    for (float width : {100.0f, 200.0f}) {
        hits = context.profiler.intrinsic_cache_hits;
        repeated = measure_at_width(width);
        EXPECT_FLOAT_EQ(repeated.max_content, 20.0f + width * 0.5f);
        EXPECT_EQ(context.profiler.intrinsic_cache_hits, hits + 1u);
    }
    ASSERT_NE(block->layout_cache->intrinsic_width_history, nullptr);
    block->set_has_cached_intrinsic_widths(false);
    misses = context.profiler.intrinsic_cache_misses;
    EXPECT_FLOAT_EQ(measure_at_width(200.0f).max_content, 120.0f);
    EXPECT_FLOAT_EQ(measure_at_width(100.0f).max_content, 70.0f);
    EXPECT_EQ(context.profiler.intrinsic_cache_misses, misses + 2u);
    EXPECT_FLOAT_EQ(measure_at_width(200.0f).max_content, 120.0f);
    advance_measurement_cache_generation(&tree);
    misses = context.profiler.intrinsic_cache_misses;
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, block).max_content, 120.0f);
    EXPECT_GT(context.profiler.intrinsic_cache_misses, misses);
    EXPECT_EQ(block->layout_cache->intrinsic_width_history, nullptr);
    misses = context.profiler.intrinsic_cache_misses;
    set_property("width", "calc(30px + 50%)");
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, block).max_content, 130.0f);
    EXPECT_GT(context.profiler.intrinsic_cache_misses, misses);
    set_property("width", "calc(20px + 50%)");
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, block).max_content, 120.0f);

    // a flex height query with a known wrapping width must not measure widths again.
    set_property("height", "24px");
    set_property("padding-top", "10%");
    uint64_t requests = context.profiler.intrinsic_requests;
    EXPECT_FLOAT_EQ(flex_measure_intrinsic_max_height(&context, block, 40.0f, 100.0f), 34.0f);
    EXPECT_EQ(context.profiler.intrinsic_requests, requests);
    IntrinsicSizes row = calculate_grid_item_intrinsic_sizes(&context,
        lam::view_require_block(block), true);
    EXPECT_FLOAT_EQ(row.min_content, 44.0f);
    EXPECT_FLOAT_EQ(row.max_content, 44.0f);
    EXPECT_EQ(context.profiler.intrinsic_requests, requests);

    // source ancestors above the selected root outlive its view-prop generation.
    DomElement* outside = element();
    outside->tag_id = MARKUP_NAME_DIV;
    outside->doc = lam::up(&doc);
    outside->specified_style = lam::shared(style_tree_create(tree.prop_pool));
    doc.root = lam::up(block);
    ASSERT_TRUE(outside->DomNode::append_child(block));
    block->set_needs_style_recompute(true);
    calculate_max_content_height(&context, block, 40.0f);
    EXPECT_EQ(outside->layout_cache, nullptr);

    scratch_release(&context.scratch);
    arena_destroy(scratch);
    doc.view_tree = nullptr;
}

class IncrementalCssTest : public RadiantCoreTest {
protected:
    DomDocument document;
    LayoutContext context = {};
    BlockContext containing = {};
    FontProp initial_font = FONT_PROP_DEFAULT;
    DomElement* target = nullptr;
    Arena* scratch = nullptr;

    void SetUp() override {
        RadiantCoreTest::SetUp();
        ASSERT_TRUE(css_property_system_init(tree.prop_pool));
        target = element();
        target->tag_id = MARKUP_NAME_DIV;
        target->view_type = RDT_VIEW_BLOCK;
        target->doc = lam::up(&document);
        target->specified_style = lam::shared(style_tree_create(tree.prop_pool));
        document.root = lam::up(target);
        document.view_tree = lam::own(&tree);
        document.document_pool = lam::own(tree.prop_pool.get());
        context.doc = lam::up(&document);
        context.pool = lam::up(tree.prop_pool.get());
        context.view = lam::up(static_cast<View*>(target));
        context.elmt = lam::up(static_cast<DomNode*>(target));
        context.selected_view_tree = lam::up(&tree);
        context.run_mode = radiant::RunMode::ComputeSize;
        context.font.style = lam::up(&initial_font);
        context.font.current_font_size = initial_font.font_size;
        context.width = 1200.0f;
        context.height = 800.0f;
        context.block.parent = lam::up(&containing);
        containing.content_width = containing.given_width = 200.0f;
        containing.content_height = containing.given_height = 100.0f;
        scratch = arena_create_default();
        ASSERT_NE(scratch, nullptr);
        scratch_init(&context.scratch, scratch);
    }

    void TearDown() override {
        // snapshots own cloned expressions, but never own their applied view props.
        radiant::css_resolved_style_release(target->layout_cache, tree.prop_pool);
        radiant::intrinsic_height_cache_release(target->layout_cache, tree.prop_pool);
        scratch_release(&context.scratch);
        arena_destroy(scratch);
        document.view_tree = nullptr;
        document.document_pool = nullptr;
        css_property_system_cleanup();
        RadiantCoreTest::TearDown();
    }

    void set_property(DomElement* element, const char* name, const char* value) {
        CssDeclaration* declaration = css_parse_property_declaration(
            name, strlen(name), value, strlen(value), tree.prop_pool);
        ASSERT_NE(declaration, nullptr);
        ASSERT_TRUE(style_tree_apply_declaration(element->specified_style, declaration));
        element->advance_style_version();
        element->set_needs_style_recompute(true);
    }

    void set_property(const char* name, const char* value) { set_property(target, name, value); }

    void resolve() { dom_node_resolve_style(target, &context); }

    void resolve_child(DomElement* child) {
        LayoutViewScope view_scope(&context);
        context.view = lam::up(static_cast<View*>(child));
        context.elmt = lam::up(static_cast<DomNode*>(child));
        dom_node_resolve_style(child, &context);
    }

    DomElement* append_intrinsic_child(DomElement* parent = nullptr) {
        DomElement* child = element();
        child->tag_id = MARKUP_NAME_DIV;
        child->view_type = RDT_VIEW_BLOCK;
        child->doc = lam::up(&document);
        child->specified_style = lam::shared(style_tree_create(tree.prop_pool));
        EXPECT_TRUE((parent ? parent : target)->DomNode::append_child(child));
        return child;
    }

    void expect_intrinsic_query(float width, bool hit) {
        uint64_t hits = context.profiler.intrinsic_cache_hits;
        uint64_t misses = context.profiler.intrinsic_cache_misses;
        IntrinsicSizes sizes = measure_element_intrinsic_widths(&context, target);
        EXPECT_FLOAT_EQ(sizes.min_content, width);
        EXPECT_FLOAT_EQ(sizes.max_content, width);
        EXPECT_EQ(context.profiler.intrinsic_cache_hits, hits + (hit ? 1u : 0u));
        EXPECT_EQ(context.profiler.intrinsic_cache_misses, misses + (hit ? 0u : 1u));
    }

    void expect_intrinsic_refresh(float width, const char* reason) {
        SCOPED_TRACE(reason);
        expect_intrinsic_query(width, false);
        expect_intrinsic_query(width, true);
    }

    void expect_height_refresh(float width, float height) {
        uint64_t computations = context.profiler.height_computations;
        EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, width), height);
        EXPECT_EQ(context.profiler.height_computations, computations + 1);
        EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, width), height);
        EXPECT_EQ(context.profiler.height_computations, computations + 1);
    }
};

TEST_F(IncrementalCssTest, NestedGridIntrinsicPairMeasuresEachSubtreeOnce) {
    DomElement* grid = target;
    constexpr unsigned grid_depth = 8;
    for (unsigned depth = 0; depth < grid_depth; depth++) {
        set_property(grid, "display", "grid");
        set_property(grid, "grid-template-columns", "auto");
        grid = append_intrinsic_child(grid);
    }
    set_property(grid, "display", "block");
    DomElement* first = append_intrinsic_child(grid);
    DomElement* second = append_intrinsic_child(grid);
    set_property(first, "display", "inline-block");
    set_property(first, "width", "40px");
    set_property(second, "display", "inline-block");
    set_property(second, "width", "60px");
    for (unsigned pass = 0; pass < 2; pass++) {
        if (pass) set_property(second, "width", "80px");
        tree.measurement_cache_generation++;
        uint64_t requests = context.profiler.intrinsic_requests;
        IntrinsicSizes sizes = measure_element_intrinsic_widths(&context, target);
        EXPECT_FLOAT_EQ(sizes.min_content, pass ? 80.0f : 60.0f);
        EXPECT_FLOAT_EQ(sizes.max_content, pass ? 120.0f : 100.0f);
        // each grid consumes the child's min/max pair once, independent of nesting depth.
        EXPECT_LE(context.profiler.intrinsic_requests - requests, grid_depth + 3u);
    }
}

TEST_F(IncrementalCssTest, IntrinsicFlowReusesOnlyGeometryIndependentSubtrees) {
    DomElement* child = append_intrinsic_child();
    set_property(child, "width", "80px");
    set_property("padding", "2px");
    context.block.content_width = 200.0f;
    context.available_space = AvailableSpace::make_definite(200.0f, 100.0f);
    IntrinsicSizes first = measure_element_intrinsic_widths(&context, target);
    EXPECT_FLOAT_EQ(first.max_content, 84.0f);
    uint64_t hits = context.profiler.intrinsic_cache_hits;
    uint64_t requests = context.profiler.intrinsic_requests;

    // flex sizing changes provisional boxes without changing fixed/text contributions.
    containing.content_width = 400.0f;
    containing.content_height = 300.0f;
    context.block.content_width = 400.0f;
    context.block.content_height = 300.0f;
    context.available_space = AvailableSpace::make_definite(400.0f, 300.0f);
    IntrinsicSizes repeated = measure_element_intrinsic_widths(&context, target);
    EXPECT_FLOAT_EQ(repeated.max_content, first.max_content);
    EXPECT_EQ(context.profiler.intrinsic_cache_hits, hits + 1);
    EXPECT_EQ(context.profiler.intrinsic_requests, requests + 1);

    // a changed inherited font remains a distinct query even for a fixed-width result.
    initial_font.font_size += 2.0f;
    requests = context.profiler.intrinsic_requests;
    measure_element_intrinsic_widths(&context, target);
    EXPECT_GT(context.profiler.intrinsic_requests, requests + 1);

    set_property(child, "width", "50%");
    target->set_needs_style_recompute(true);
    context.block.content_width = containing.content_width = 200.0f;
    context.available_space = AvailableSpace::make_definite(200.0f, 100.0f);
    first = measure_element_intrinsic_widths(&context, target);
    EXPECT_FLOAT_EQ(first.max_content, 104.0f);
    context.block.content_width = containing.content_width = 400.0f;
    context.available_space = AvailableSpace::make_definite(400.0f, 300.0f);
    repeated = measure_element_intrinsic_widths(&context, target);
    EXPECT_FLOAT_EQ(repeated.max_content, 204.0f);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, CachedDescendantPropagatesContainingSizeDependency) {
    DomElement* child = append_intrinsic_child();
    set_property(child, "width", "50%");
    set_property("padding", "2px");
    context.block.content_width = 200.0f;
    context.available_space = AvailableSpace::make_definite(200.0f, 100.0f);
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 104.0f);
    ASSERT_TRUE(child->has_cached_intrinsic_widths());

    // rebuilding only the parent must propagate dependencies from child cache hits.
    target->set_has_cached_intrinsic_widths(false);
    uint64_t hits = context.profiler.intrinsic_cache_hits;
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 104.0f);
    EXPECT_EQ(context.profiler.intrinsic_cache_hits, hits + 1u);
    context.block.content_width = containing.content_width = 400.0f;
    context.available_space = AvailableSpace::make_definite(400.0f, 100.0f);
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 204.0f);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, CommittingProvisionalStyleRetainsIntrinsicResultUntilMutation) {
    set_property("width", "80px");
    expect_intrinsic_refresh(80.0f, "initial measurement");
    EXPECT_FALSE(target->styles_resolved());
    context.run_mode = radiant::RunMode::PerformLayout;
    {
        LayoutFontScope font_scope(&context);
        resolve();
    }
    EXPECT_TRUE(target->styles_resolved());
    expect_intrinsic_query(80.0f, true);

    set_property("width", "120px");
    {
        LayoutFontScope font_scope(&context);
        resolve();
    }
    expect_intrinsic_refresh(120.0f, "authored width changed after commit");
}

TEST_F(IncrementalCssTest, IntrinsicCacheInvalidatesKerningStyleEpochAndMeasurementGeneration) {
    set_property("width", "80px");
    expect_intrinsic_refresh(80.0f, "initial measurement");
    initial_font.font_kerning = CSS_VALUE_NONE;
    expect_intrinsic_refresh(80.0f, "inherited kerning changed");
    document.style_query_epoch++;
    expect_intrinsic_refresh(80.0f, "document style epoch changed");
    advance_measurement_cache_generation(&tree);
    expect_intrinsic_refresh(80.0f, "measurement generation changed");
}

TEST_F(IncrementalCssTest, IntrinsicCacheInvalidatesFontResourcesAndRasterScale) {
    FontContextConfig config = {}; config.pixel_ratio = 1;
    FontContext* fonts = font_context_create(&config);
    ASSERT_NE(fonts, nullptr);
    UiContext ui = {}; ui.font_ctx = lam::up(fonts);
    ui.document = lam::up(&document);
    ui.default_font = initial_font;
    ui.default_font.family = lam::up(const_cast<char*>("Arial"));
    context.ui_context = lam::up(&ui);
    set_property("display", "block");
    set_property("width", "80px");
    set_property("height", "2em");
    expect_intrinsic_refresh(80.0f, "initial measurement");
    expect_height_refresh(80.0f, 32.0f);

    // publishing a font descriptor invalidates sizes even when box constraints match.
    uint64_t generation = font_context_resource_generation(fonts);
    FontFaceDesc descriptor = {};
    descriptor.family = "IntrinsicCacheResource";
    descriptor.weight = FONT_WEIGHT_NORMAL;
    descriptor.slant = FONT_SLANT_NORMAL;
    EXPECT_TRUE(font_face_register(fonts, &descriptor));
    EXPECT_GT(font_context_resource_generation(fonts), generation);
    expect_intrinsic_refresh(80.0f, "font resource changed");
    expect_height_refresh(80.0f, 32.0f);
    document.viewport.raster_scale = 2.0f;
    expect_intrinsic_refresh(80.0f, "raster scale changed");
    expect_height_refresh(80.0f, 32.0f);

    font_prop_release_handle(target->font);
    font_context_destroy(fonts);
    context.ui_context = nullptr;
}

TEST_F(IncrementalCssTest, FlexWidthQueryDefersHeightUntilRequested) {
    DomElement* child = append_intrinsic_child();
    set_property(child, "width", "80px");
    set_property(child, "height", "30px");
    resolve();
    ViewElement* item = lam::view_as_element(static_cast<DomNode*>(target));
    ASSERT_NE(item, nullptr);
    alloc_flex_item_prop(&context, item);
    FlexContainerLayout flex = {};
    flex.lycon = lam::up(&context);
    flex.direction = CSS_VALUE_ROW;
    context.profiler.enabled = true;
    calculate_item_intrinsic_sizes(item, &flex, true);
    EXPECT_FLOAT_EQ(target->fi->intrinsic_width.max_content, 80.0f);
    EXPECT_TRUE(target->fi->has_intrinsic_width);
    EXPECT_FALSE(target->fi->has_intrinsic_height);
    EXPECT_EQ(context.profiler.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT], 0u);
    uint64_t width_queries = context.profiler.intrinsic_requests;
    calculate_item_intrinsic_sizes(item, &flex, true);
    EXPECT_EQ(context.profiler.intrinsic_requests, width_queries);
    EXPECT_FALSE(target->fi->has_intrinsic_height);
    EXPECT_EQ(context.profiler.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT], 0u);
    calculate_item_intrinsic_sizes(item, &flex);
    EXPECT_TRUE(target->fi->has_intrinsic_height);
    EXPECT_FLOAT_EQ(target->fi->intrinsic_height.max_content, 30.0f);
    EXPECT_GT(context.profiler.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT], 0u);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, VerticalFlexWidthQueryKeepsPairedIntrinsicMeasurement) {
    DomElement* child = append_intrinsic_child();
    set_property(child, "width", "80px");
    set_property(child, "height", "30px");
    set_property("writing-mode", "vertical-rl");
    resolve();
    ViewElement* item = lam::view_as_element(static_cast<DomNode*>(target));
    ASSERT_NE(item, nullptr);
    alloc_flex_item_prop(&context, item);
    FlexContainerLayout flex = {};
    flex.lycon = lam::up(&context);
    flex.direction = CSS_VALUE_ROW;
    context.profiler.enabled = true;
    calculate_item_intrinsic_sizes(item, &flex, true);
    EXPECT_TRUE(target->fi->has_intrinsic_width);
    EXPECT_TRUE(target->fi->has_intrinsic_height);
    EXPECT_FLOAT_EQ(target->fi->intrinsic_width.max_content, 30.0f);
    EXPECT_FLOAT_EQ(target->fi->intrinsic_height.max_content, 80.0f);
    EXPECT_GT(context.profiler.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT], 0u);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, FlexPreparationUpgradesOnlyTheRequestedMeasurementAxis) {
    DomElement* child = append_intrinsic_child();
    set_property(child, "width", "80px");
    set_property(child, "height", "30px");
    resolve();
    advance_measurement_cache_generation(&tree);
    measure_flex_child_content(&context, target, true);
    MeasurementCacheEntry* cached = get_from_measurement_cache(target);
    ASSERT_NE(cached, nullptr);
    EXPECT_FALSE(cached->has_height);
    EXPECT_FLOAT_EQ(cached->content_width, 80.0f);
    EXPECT_EQ(context.profiler.height_requests, 0u);
    uint64_t width_queries = context.profiler.intrinsic_requests;
    measure_flex_child_content(&context, target, true);
    EXPECT_EQ(context.profiler.intrinsic_requests, width_queries);
    measure_flex_child_content(&context, target);
    cached = get_from_measurement_cache(target);
    ASSERT_NE(cached, nullptr);
    EXPECT_TRUE(cached->has_height);
    EXPECT_FLOAT_EQ(cached->measured_height, 30.0f);
    EXPECT_EQ(tree.measurement_cache_count, 1);
    EXPECT_EQ(context.profiler.intrinsic_requests, width_queries);
    uint64_t height_queries = context.profiler.height_requests;
    EXPECT_GT(height_queries, 0u);
    measure_flex_child_content(&context, target);
    EXPECT_EQ(context.profiler.height_requests, height_queries);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
    destroy_measurement_cache(&tree);
}

TEST_F(IncrementalCssTest, HeightQueriesRetainDistinctWidthsWithoutRepeatingA) {
    set_property("display", "block");
    set_property("aspect-ratio", "2");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 40.0f);
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 160.0f), 80.0f);
    for (float width = 200.0f; width <= 400.0f; width += 20.0f) {
        EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, width), width / 2.0f);
    }
    uint64_t computations = context.profiler.height_computations;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 40.0f);
    EXPECT_EQ(context.profiler.height_computations, computations);
    EXPECT_EQ(context.profiler.height_cache_hits, 1u);
}

TEST_F(IncrementalCssTest, InterleavedIntrinsicAxesKeepComputedStyleAndMeasurementsHot) {
    set_property("width", "80px");
    set_property("height", "30px");
    resolve();
    expect_intrinsic_query(80.0f, false);
    expect_height_refresh(80.0f, 30.0f);
    expect_height_refresh(160.0f, 30.0f);
    uint64_t heights = context.profiler.height_computations;
    uint64_t widths = context.profiler.intrinsic_cache_misses;
    int64_t styles = g_style_resolve_measure;
    for (unsigned pass = 0; pass < 32; ++pass) {
        SCOPED_TRACE(pass);
        EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target,
            pass % 2 ? 160.0f : 80.0f), 30.0f);
        expect_intrinsic_query(80.0f, true);
        resolve();
    }
    EXPECT_EQ(context.profiler.height_computations, heights);
    EXPECT_EQ(context.profiler.intrinsic_cache_misses, widths);
    EXPECT_EQ(g_style_resolve_measure, styles);

    set_property("height", "45px");
    expect_height_refresh(80.0f, 45.0f);
    expect_height_refresh(160.0f, 45.0f);
}

TEST_F(IncrementalCssTest, HeightQueriesUseExplicitWidthAcrossIndependentSpaceLabels) {
    DomElement* child = append_intrinsic_child();
    set_property("display", "block");
    set_property(child, "display", "block");
    set_property(child, "height", "30px");
    context.available_space = AvailableSpace::make_definite(200.0f, 100.0f);
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 30.0f);
    uint64_t computations = context.profiler.height_computations;
    for (AvailableSpace space : {AvailableSpace::make_min_content(),
            AvailableSpace::make_max_content(), AvailableSpace::make_indefinite(),
            AvailableSpace::make_definite(400.0f, 300.0f)}) {
        context.available_space = space;
        EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 30.0f);
        EXPECT_EQ(context.profiler.height_computations, computations);
    }
    EXPECT_EQ(context.profiler.height_cache_hits, 4u);
    calculate_max_content_height(&context, target, 100.0f);
    EXPECT_GT(context.profiler.height_computations, computations);
    // a dependent descendant prevents its ancestor from dropping the caller policy.
    set_property(child, "padding-top", "10%");
    context.available_space = AvailableSpace::make_definite(200.0f, 100.0f);
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 38.0f);
    computations = context.profiler.height_computations;
    context.available_space = AvailableSpace::make_min_content();
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 38.0f);
    EXPECT_GT(context.profiler.height_computations, computations);
    radiant::intrinsic_height_cache_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, HeightQueriesRetainPercentageBasesAndContentPolicy) {
    set_property("display", "block");
    set_property("height", "30px");
    set_property("padding-top", "10%");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 50.0f);
    containing.content_width = containing.given_width = 400.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 70.0f);
    containing.content_width = containing.given_width = 200.0f;
    uint64_t computations = context.profiler.height_computations;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 50.0f);
    EXPECT_EQ(context.profiler.height_computations, computations);
    // content-only block sizing cannot consume the specified-height result.
    calculate_max_content_height(&context, target, 80.0f, true);
    EXPECT_GT(context.profiler.height_computations, computations);
}

TEST_F(IncrementalCssTest, HeightQueriesTrackFlexPercentageBasisOutsideAmbientContext) {
    DomElement* child = append_intrinsic_child();
    set_property("display", "flex");
    set_property(child, "display", "block");
    set_property(child, "height", "30px");
    set_property(child, "padding-top", "10%");
    resolve();
    target->width = 100.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 40.0f);
    target->width = 200.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 50.0f);
    uint64_t computations = context.profiler.height_computations;
    target->width = 100.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 40.0f);
    EXPECT_EQ(context.profiler.height_computations, computations);
    radiant::intrinsic_height_cache_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, HeightQueriesKeepNestedMutableFlexGeometryConservative) {
    DomElement* flex = append_intrinsic_child();
    DomElement* child = append_intrinsic_child(flex);
    set_property("display", "block");
    set_property(flex, "display", "flex");
    set_property(child, "display", "block");
    set_property(child, "height", "30px");
    set_property(child, "padding-top", "10%");
    resolve();
    resolve_child(flex);
    flex->width = 100.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 40.0f);
    flex->width = 200.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 80.0f), 50.0f);
    DomElement* descendants[] = {child, flex};
    for (DomElement* element : descendants) {
        radiant::intrinsic_height_cache_release(element->layout_cache, tree.prop_pool);
        radiant::css_resolved_style_release(element->layout_cache, tree.prop_pool);
    }
}

TEST_F(IncrementalCssTest, HeightQueriesInvalidateDescendantStyleAndExplicitDirtyInputs) {
    DomElement* child = append_intrinsic_child();
    set_property("display", "block");
    set_property(child, "display", "block");
    set_property(child, "height", "30px");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 30.0f);
    uint64_t computations = context.profiler.height_computations;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 30.0f);
    EXPECT_EQ(context.profiler.height_computations, computations);
    set_property(child, "height", "60px");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 60.0f);
    EXPECT_GT(context.profiler.height_computations, computations);
    computations = context.profiler.height_computations;
    child->set_needs_style_recompute(true);
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 60.0f);
    EXPECT_GT(context.profiler.height_computations, computations);
    radiant::intrinsic_height_cache_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, HeightQueriesKeepUnsupportedDescendantsConservative) {
    DomElement* child = append_intrinsic_child();
    set_property("display", "block");
    set_property(child, "display", "grid");
    set_property(child, "height", "30px");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 30.0f);
    uint64_t computations = context.profiler.height_computations;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 30.0f);
    EXPECT_GT(context.profiler.height_computations, computations);
    EXPECT_EQ(context.profiler.height_cache_hits, 0u);
    radiant::intrinsic_height_cache_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, HeightQueriesIgnoreUnrelatedDirtyAndNewNodes) {
    DomElement* child = append_intrinsic_child();
    DomElement* sibling = append_intrinsic_child();
    set_property("display", "block");
    set_property(child, "display", "block");
    set_property(child, "height", "30px");
    set_property(sibling, "display", "block");
    set_property(sibling, "height", "40px");
    resolve();
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 100.0f), 30.0f);
    uint64_t computations = context.profiler.height_computations;
    uint64_t revision = target->intrinsic_input_revision;
    sibling->set_needs_style_recompute(true);
    EXPECT_GT(target->intrinsic_input_revision, revision);
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 100.0f), 30.0f);
    EXPECT_EQ(context.profiler.height_computations, computations);

    DomElement* detached = element();
    detached->doc = lam::up(&document);
    detached->set_needs_style_recompute(true);
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 100.0f), 30.0f);
    EXPECT_EQ(context.profiler.height_computations, computations);
    radiant::intrinsic_height_cache_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, ExplicitDirtyNotificationsExpireAncestorWidths) {
    DomElement* child = append_intrinsic_child();
    set_property(child, "width", "30px");
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 30.0f);
    EXPECT_TRUE(target->has_cached_intrinsic_widths());
    // direct dirty notifications must reach consumers even before a DOM commit.
    CssDeclaration* changed = css_parse_property_declaration(
        "width", 5, "60px", 4, tree.prop_pool);
    ASSERT_NE(changed, nullptr);
    ASSERT_TRUE(style_tree_apply_declaration(child->specified_style, changed));
    child->set_needs_style_recompute(true);
    EXPECT_FALSE(target->has_cached_intrinsic_widths());
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 60.0f);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, HeightQueriesInvalidateEnvironmentAndGenerationButNormalizeIndependentSpace) {
    UiContext ui = {};
    ui.document = lam::up(&document);
    ui.default_font = initial_font;
    context.ui_context = lam::up(&ui);
    set_property("display", "block");
    set_property("height", "30px");
    expect_height_refresh(100.0f, 30.0f);
    context.width += 100.0f;
    expect_height_refresh(100.0f, 30.0f);
    context.root_font_size = 24.0f;
    expect_height_refresh(100.0f, 30.0f);
    document.viewport.raster_scale = 2.0f;
    expect_height_refresh(100.0f, 30.0f);
    uint64_t computations = context.profiler.height_computations;
    for (AvailableSpace space : {AvailableSpace::make_min_content(), AvailableSpace::make_max_content()}) {
        context.available_space = space;
        EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 30.0f);
        EXPECT_EQ(context.profiler.height_computations, computations);
    }
    tree.measurement_cache_generation++;
    expect_height_refresh(100.0f, 30.0f);
    document.mutation_epoch++;
    expect_height_refresh(100.0f, 30.0f);
    context.ui_context = nullptr;
}

TEST_F(IncrementalCssTest, HeightQueriesReuseOnlyGeometryIndependentSubtrees) {
    DomElement* child = append_intrinsic_child();
    set_property("display", "block");
    set_property(child, "display", "block");
    set_property(child, "height", "30px");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 30.0f);
    uint64_t computations = context.profiler.height_computations;
    containing.content_width = containing.given_width = 400.0f;
    containing.content_height = containing.given_height = 500.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 30.0f);
    EXPECT_EQ(context.profiler.height_computations, computations);

    set_property(child, "padding-top", "10%");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 40.0f);
    computations = context.profiler.height_computations;
    containing.content_height = containing.given_height = 600.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, target, 100.0f), 40.0f);
    EXPECT_GT(context.profiler.height_computations, computations);
    radiant::intrinsic_height_cache_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, IntrinsicFlowKeepsGlobalLengthInputsInItsKey) {
    DomElement* child = append_intrinsic_child();
    context.root_font_size = 10.0f;
    set_property(child, "width", "20rem");
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 200.0f);
    uint64_t requests = context.profiler.intrinsic_requests;
    context.block.content_width = containing.content_width = 400.0f;
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 200.0f);
    EXPECT_EQ(context.profiler.intrinsic_requests, requests + 1);
    context.root_font_size = 15.0f;
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 300.0f);

    set_property(child, "width", "10vw");
    target->set_needs_style_recompute(true);
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 120.0f);
    context.width = 1600.0f;
    EXPECT_FLOAT_EQ(measure_element_intrinsic_widths(&context, target).max_content, 160.0f);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST(LayoutProfiling, ContextRestoreRetainsNestedWork) {
    LayoutContext context = {};
    context.block.content_width = 200.0f;
    LayoutContext saved = context;
    context.block.content_width = 400.0f;
    context.profiler.intrinsic_requests = 12;
    context.profiler.intrinsic_cache_hits = 4;
    context.profiler.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT] = 8;
    radiant::layout_restore_context(&context, saved);
    EXPECT_FLOAT_EQ(context.block.content_width, 200.0f);
    EXPECT_EQ(context.profiler.intrinsic_requests, 12u);
    EXPECT_EQ(context.profiler.intrinsic_cache_hits, 4u);
    EXPECT_EQ(context.profiler.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT], 8u);
}

TEST(LayoutProfiling, NestedScopesRetainCountersAndExclusiveTimeAcrossContextRestore) {
    LayoutContext context = {};
    context.profiler.enabled = true;
    {
        radiant::LayoutProfileScope width(&context, radiant::LAYOUT_PROFILE_INTRINSIC, nullptr);
        LayoutContext saved = context;
        {
            radiant::LayoutProfileScope height(&context, radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT, nullptr);
            radiant::layout_profiler_note_intrinsic_request(&context.profiler, false, false);
            radiant::layout_profiler_note_intrinsic_request(&context.profiler, true, false);
        }
        radiant::layout_restore_context(&context, saved);
    }
    const radiant::LayoutProfiler& profile = context.profiler;
    EXPECT_EQ(profile.intrinsic_requests, 2u);
    EXPECT_EQ(profile.intrinsic_cache_hits, 1u);
    EXPECT_EQ(profile.intrinsic_cache_misses, 1u);
    EXPECT_EQ(profile.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC], 1u);
    EXPECT_EQ(profile.scope_calls[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT], 1u);
    // parent self time excludes its child, while the height leaf has no nested work.
    EXPECT_NEAR(profile.scope_total_ms[radiant::LAYOUT_PROFILE_INTRINSIC],
        profile.scope_self_ms[radiant::LAYOUT_PROFILE_INTRINSIC] +
        profile.scope_total_ms[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT], 0.0001);
    EXPECT_DOUBLE_EQ(profile.scope_self_ms[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT],
        profile.scope_total_ms[radiant::LAYOUT_PROFILE_INTRINSIC_HEIGHT]);
}

TEST_F(IncrementalCssTest, RebindsOnlyChangedGeometryAndRestoresAuthoredValues) {
    set_property("font-size", "20px");
    set_property("color", "blue");
    set_property("width", "calc(50% - 10px)");
    set_property("height", "25%");
    set_property("min-width", "calc(5% + 5px)");
    set_property("max-height", "75%");
    set_property("padding-top", "calc(1em + 5%)");
    set_property("padding-inline-start", "5%");
    set_property("margin", "12px 10%");
    set_property("left", "20%");
    resolve();
    ASSERT_NE(target->layout_cache, nullptr);
    ASSERT_NE(target->layout_cache->resolved_style, nullptr);
    EXPECT_FLOAT_EQ(target->block()->given_width, 90.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 30.0f);
    SCOPED_TRACE(radiant::css_style_miss_name(radiant::css_resolved_style_last_miss(target)));
    int64_t full = g_style_resolve_measure;
    int64_t reused = g_style_resolve_incremental;
    int64_t evaluated = g_style_geometry_evaluations;

    // flow consumes margins and sizing; neither mutation changes computed CSS.
    target->boundary_mut()->flow_margin = target->boundary()->margin;
    target->boundary_mut()->has_flow_margin = true;
    target->boundary_mut()->margin.top = 0.0f;
    target->blk->given_width = 777.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_EQ(g_style_resolve_incremental, reused + 1);
    EXPECT_EQ(g_style_geometry_evaluations, evaluated);
    EXPECT_FLOAT_EQ(target->block()->given_width, 90.0f);
    EXPECT_FLOAT_EQ(target->boundary()->margin.top, 12.0f);
    EXPECT_FALSE(target->boundary()->has_flow_margin);

    containing.content_width = containing.given_width = 400.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 190.0f);
    EXPECT_FLOAT_EQ(target->block()->given_min_width, 25.0f);
    EXPECT_FLOAT_EQ(target->block()->given_height, 25.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 40.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.left, 20.0f);
    EXPECT_FLOAT_EQ(target->boundary()->margin.right, 40.0f);
    EXPECT_FLOAT_EQ(target->position->left, 80.0f);
    EXPECT_FLOAT_EQ(target->font->font_size, 20.0f);
    EXPECT_EQ(target->in_line->color.b, 255);
    EXPECT_EQ(target->in_line->color.r, 0);

    containing.content_height = containing.given_height = 200.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_height, 50.0f);
    EXPECT_FLOAT_EQ(target->block()->given_max_height, 150.0f);
    containing.content_width = containing.given_width = 200.0f;
    containing.content_height = containing.given_height = 100.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 90.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 30.0f);

    // committing layout can reuse provisional computed CSS without claiming an earlier commit.
    EXPECT_FALSE(target->styles_resolved());
    context.run_mode = radiant::RunMode::PerformLayout;
    int64_t committed = g_style_resolve_full;
    resolve();
    EXPECT_TRUE(target->styles_resolved());
    EXPECT_EQ(g_style_resolve_full, committed);
}

TEST_F(IncrementalCssTest, InvalidatesVariablesAndExplicitDirtyNotifications) {
    set_property("--width", "30%");
    set_property("width", "var(--width)");
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_width, 60.0f);
    int64_t full = g_style_resolve_measure;
    containing.content_width = containing.given_width = 400.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 120.0f);
    set_property("--width", "40%");
    resolve();
    EXPECT_GT(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 160.0f);
    full = g_style_resolve_measure;
    target->set_needs_style_recompute(true);
    resolve();
    EXPECT_GT(g_style_resolve_measure, full);
}

TEST_F(IncrementalCssTest, RepeatedContextChangesDoNotAmplifyAncestorStyleInvalidations) {
    DomElement* child = append_intrinsic_child();
    DomElement* leaf = append_intrinsic_child(child);
    set_property("font-size", "20px");
    set_property("color", "red");
    set_property(child, "width", "50%");
    set_property(leaf, "padding-left", "1em");
    resolve(); resolve_child(child); resolve_child(leaf);
    uint32_t inherited = radiant::css_resolved_style_inherited_revision(target);
    int64_t full = g_style_resolve_measure;
    for (unsigned pass = 0; pass < 32; ++pass) {
        containing.content_width = containing.given_width = pass % 2 ? 400.0f : 200.0f;
        resolve(); resolve_child(child); resolve_child(leaf);
        EXPECT_FLOAT_EQ(child->block()->given_width, containing.content_width * 0.5f);
        EXPECT_FLOAT_EQ(leaf->boundary()->padding.left, 20.0f);
        EXPECT_EQ(radiant::css_resolved_style_inherited_revision(target), inherited);
    }
    EXPECT_EQ(g_style_resolve_measure, full);

    // a real inherited edit expires each descendant once, then stabilizes again.
    set_property("font-size", "24px");
    resolve(); resolve_child(child); resolve_child(leaf);
    EXPECT_NE(radiant::css_resolved_style_inherited_revision(target), inherited);
    EXPECT_FLOAT_EQ(leaf->boundary()->padding.left, 24.0f);
    full = g_style_resolve_measure;
    for (unsigned pass = 0; pass < 16; ++pass) {
        resolve(); resolve_child(child); resolve_child(leaf);
    }
    EXPECT_EQ(g_style_resolve_measure, full);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(leaf->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, TracksDefinitenessAndZeroWithoutReusingOldInsets) {
    set_property("width", "50%");
    set_property("top", "50%");
    set_property("padding", "10%");
    resolve();
    int64_t full = g_style_resolve_measure;
    EXPECT_TRUE(target->position->has_top);
    EXPECT_FLOAT_EQ(target->position->top, 50.0f);
    containing.given_height = -1.0f;
    resolve();
    // the native inset keeps its deferred percentage while its current used basis is indefinite.
    EXPECT_TRUE(target->position->has_top);
    EXPECT_FLOAT_EQ(target->position->top, 0.0f);
    EXPECT_EQ(g_style_resolve_measure, full);
    containing.content_width = containing.given_width = 0.0f;
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_width, 0.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 0.0f);
    containing.content_width = containing.given_width = 200.0f;
    containing.given_height = 100.0f;
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_width, 100.0f);
    EXPECT_TRUE(target->position->has_top);
    EXPECT_FLOAT_EQ(target->position->top, 50.0f);
    EXPECT_EQ(g_style_resolve_measure, full);
}

TEST_F(IncrementalCssTest, ParentFontEditsInvalidateInheritedComputedLengths) {
    FontContextConfig config = {}; config.pixel_ratio = 1;
    FontContext* fonts = font_context_create(&config);
    ASSERT_NE(fonts, nullptr);
    UiContext ui = {}; ui.font_ctx = lam::up(fonts);
    ui.default_font = initial_font;
    ui.default_font.family = lam::up(const_cast<char*>("Arial"));
    context.ui_context = lam::up(&ui);
    DomElement* parent = element();
    parent->tag_id = MARKUP_NAME_DIV;
    parent->view_type = RDT_VIEW_BLOCK;
    parent->doc = lam::up(&document);
    parent->specified_style = lam::shared(style_tree_create(tree.prop_pool));
    document.root = lam::up(parent);
    ASSERT_TRUE(parent->DomNode::append_child(target));
    auto resolve_parent = [&]() {
        context.view = lam::up(static_cast<View*>(parent));
        context.elmt = lam::up(static_cast<DomNode*>(parent));
        context.font.style = lam::up(&initial_font);
        dom_node_resolve_style(parent, &context);
        context.view = lam::up(static_cast<View*>(target));
        context.elmt = lam::up(static_cast<DomNode*>(target));
        context.font.style = lam::up(parent->font.get());
    };
    set_property(parent, "font-size", "20px");
    set_property(parent, "--gap", "calc(1em + 10%)");
    set_property("padding-top", "var(--gap)");
    resolve_parent();
    resolve();
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 40.0f);
    int64_t full = g_style_resolve_measure;
    containing.content_width = containing.given_width = 400.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 60.0f);
    set_property(parent, "font-size", "30px");
    resolve_parent();
    resolve();
    EXPECT_GT(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 70.0f);
    radiant::css_resolved_style_release(parent->layout_cache, tree.prop_pool);
    font_prop_release_handle(target->font);
    font_prop_release_handle(parent->font);
    font_context_destroy(fonts);
    context.ui_context = nullptr;
}

TEST_F(IncrementalCssTest, InheritedIdentityFollowsComputedValuesAndKeepsFontMetricsOwned) {
    DomElement* child = append_intrinsic_child();
    set_property("font-size", "16px");
    set_property("color", "red");
    set_property("line-height", "1.5");
    resolve();
    resolve_child(child);
    uint32_t inherited = radiant::css_resolved_style_inherited_revision(target);
    ASSERT_NE(inherited, 0u);
    ASSERT_NE(target->font->computed_inputs, nullptr);
    ASSERT_NE(child->font, target->font);
    EXPECT_EQ(child->font->computed_inputs, target->font->computed_inputs);
    const ComputedFontInputs* font = target->font->computed_inputs;
    child->font->ascender = 123.0f;
    EXPECT_NE(target->font->ascender, child->font->ascender);

    set_property("padding-left", "23px");
    set_property("font-size", "16.0px");
    set_property("color", "rgb(255, 0, 0)");
    resolve();
    EXPECT_EQ(radiant::css_resolved_style_inherited_revision(target), inherited);
    EXPECT_EQ(target->font->computed_inputs, font);
    set_property("line-height", "24px");
    resolve();
    // a number and an equal used length inherit differently at another font size.
    EXPECT_NE(radiant::css_resolved_style_inherited_revision(target), inherited);
    inherited = radiant::css_resolved_style_inherited_revision(target);
    set_property("color", "blue");
    resolve();
    EXPECT_NE(radiant::css_resolved_style_inherited_revision(target), inherited);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, InheritedIdentityPropagatesCustomPropertiesThroughUnchangedValues) {
    DomElement* child = append_intrinsic_child();
    DomElement* grandchild = append_intrinsic_child(child);
    set_property("--gap", "12px");
    set_property(grandchild, "padding-left", "var(--gap)");
    resolve(); resolve_child(child); resolve_child(grandchild);
    uint32_t inherited = radiant::css_resolved_style_inherited_revision(child);
    ASSERT_FLOAT_EQ(grandchild->boundary()->padding.left, 12.0f);
    set_property("--gap", "25px");
    resolve(); resolve_child(child); resolve_child(grandchild);
    EXPECT_NE(radiant::css_resolved_style_inherited_revision(child), inherited);
    EXPECT_FLOAT_EQ(grandchild->boundary()->padding.left, 25.0f);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(grandchild->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, ParentStorageRestylePreservesDescendantComputedInputs) {
    DomElement* child = append_intrinsic_child();
    set_property("--gap", "12px");
    set_property("color", "red");
    set_property(child, "padding-left", "var(--gap)");
    resolve();
    resolve_child(child);
    ASSERT_FLOAT_EQ(child->boundary()->padding.left, 12.0f);
    ASSERT_NE(child->in_line, nullptr);
    Color color = child->in_line->color;

    // materializing layout storage must not turn unchanged inheritance into a new style.
    ASSERT_EQ(target->position, nullptr);
    target->ensure_position(&context);
    int64_t full = g_style_resolve_measure;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    full = g_style_resolve_measure;
    resolve_child(child);
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(child->boundary()->padding.left, 12.0f);
    EXPECT_EQ(child->in_line->color.c, color.c);

    set_property("--gap", "25px");
    set_property("color", "blue");
    resolve();
    full = g_style_resolve_measure;
    resolve_child(child);
    EXPECT_EQ(g_style_resolve_measure, full + 1);
    EXPECT_FLOAT_EQ(child->boundary()->padding.left, 25.0f);
    EXPECT_NE(child->in_line->color.c, color.c);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, AncestorStorageRestyleDoesNotCascadeThroughNestedMeasurements) {
    DomElement* child = append_intrinsic_child();
    DomElement* grandchild = element();
    grandchild->tag_id = MARKUP_NAME_DIV;
    grandchild->view_type = RDT_VIEW_BLOCK;
    grandchild->doc = lam::up(&document);
    grandchild->specified_style = lam::shared(style_tree_create(tree.prop_pool));
    ASSERT_TRUE(child->DomNode::append_child(grandchild));
    set_property("color", "red");
    set_property(grandchild, "width", "45px");
    resolve();
    resolve_child(child);
    resolve_child(grandchild);
    target->ensure_position(&context);
    resolve();
    int64_t full = g_style_resolve_measure;
    resolve_child(child);
    resolve_child(grandchild);
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(grandchild->block()->given_width, 45.0f);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(grandchild->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, ReparentingDoesNotConfuseEqualParentRevisionNumbers) {
    DomElement* child = append_intrinsic_child();
    DomElement* new_parent = append_intrinsic_child();
    set_property("color", "red");
    set_property(new_parent, "color", "blue");
    resolve();
    resolve_child(new_parent);
    resolve_child(child);
    ASSERT_EQ(radiant::css_resolved_style_input_revision(&context, target),
              radiant::css_resolved_style_input_revision(&context, new_parent));
    Color before = child->in_line->color;
    ASSERT_TRUE(target->DomNode::remove_child(child));
    ASSERT_TRUE(new_parent->DomNode::append_child(child));
    resolve_child(child);
    EXPECT_NE(child->in_line->color.c, before.c);
    EXPECT_EQ(child->in_line->color.c, new_parent->in_line->color.c);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(new_parent->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, RetainedResetReleasesOwnedComputedBindings) {
    set_property("display", "block");
    set_property("width", "calc(2em + 50%)");
    set_property("height", "30px");
    resolve();
    expect_height_refresh(100.0f, 30.0f);
    ASSERT_NE(target->layout_cache->intrinsic_heights, nullptr);
    ASSERT_NE(target->layout_cache->resolved_style, nullptr);
    int64_t full = g_style_resolve_measure;
    view_pool_reset_retained_subtree(&tree, target);
    ASSERT_NE(target->layout_cache, nullptr);
    EXPECT_EQ(target->layout_cache->resolved_style, nullptr);
    EXPECT_EQ(target->layout_cache->intrinsic_heights, nullptr);
    // layout recreates the view kind before invoking CSS after a retained reset.
    target->view_type = RDT_VIEW_BLOCK;
    resolve();
    EXPECT_GT(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 132.0f);
}

TEST_F(IncrementalCssTest, KeepsStaticPhysicalAndLogicalWinnersOverPercentageShorthands) {
    set_property("margin", "10%");
    set_property("margin-left", "7px");
    set_property("padding", "5%");
    set_property("padding-inline-start", "3px");
    resolve();
    EXPECT_FLOAT_EQ(target->boundary()->margin.left, 7.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.left, 3.0f);
    int64_t full = g_style_resolve_measure;
    containing.content_width = containing.given_width = 400.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->boundary()->margin.right, 40.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.right, 20.0f);
    EXPECT_FLOAT_EQ(target->boundary()->margin.left, 7.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.left, 3.0f);
}

TEST_F(IncrementalCssTest, PreservesAlgorithmAssignedSizesWhenCssDoesNotWriteThem) {
    set_property("min-width", "0");
    context.block.given_width = 317.0f;
    context.block.given_height = 211.0f;
    resolve();
    EXPECT_FLOAT_EQ(context.block.given_width, 317.0f);
    int64_t full = g_style_resolve_measure;
    context.block.given_width = 421.0f;
    context.block.given_height = 299.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(context.block.given_width, 421.0f);
    EXPECT_FLOAT_EQ(context.block.given_height, 299.0f);
    // an explicit CSS auto declaration does write the context and must still replace it.
    set_property("height", "auto");
    resolve();
    EXPECT_FLOAT_EQ(context.block.given_height, -1.0f);
    context.block.given_height = 499.0f;
    resolve();
    EXPECT_FLOAT_EQ(context.block.given_height, -1.0f);
}

TEST_F(IncrementalCssTest, ComputedBoxPropertiesSurviveProvisionalViewKinds) {
    target->view_type = RDT_VIEW_INLINE;
    target->ensure_block(&context);
    set_property("width", "60px");
    set_property("height", "40px");
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_width, 60.0f);
    EXPECT_FLOAT_EQ(context.block.given_width, 60.0f);
    int64_t full = g_style_resolve_measure;
    context.block.given_width = 999.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(context.block.given_width, 60.0f);
    target->view_type = RDT_VIEW_BLOCK;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 60.0f);
    EXPECT_FLOAT_EQ(target->block()->given_height, 40.0f);
}

TEST_F(IncrementalCssTest, ComputedTextPropertiesDoNotDependOnProvisionalBoxRole) {
    target->view_type = RDT_VIEW_NONE;
    set_property("text-align", "center");
    set_property("text-indent", "2em");
    set_property("min-width", "15px");
    resolve();
    ASSERT_NE(target->blk, nullptr);
    EXPECT_EQ(target->block()->text_align, CSS_VALUE_CENTER);
    EXPECT_FLOAT_EQ(target->block()->text_indent, 32.0f);
    EXPECT_FLOAT_EQ(target->block()->given_min_width, 15.0f);
    int64_t full = g_style_resolve_measure;
    const ViewType roles[] = {RDT_VIEW_INLINE, RDT_VIEW_BLOCK, RDT_VIEW_INLINE};
    for (ViewType role : roles) {
        target->view_type = role;
        resolve();
        EXPECT_EQ(g_style_resolve_measure, full);
        EXPECT_EQ(target->block()->text_align, CSS_VALUE_CENTER);
        EXPECT_FLOAT_EQ(target->block()->text_indent, 32.0f);
        EXPECT_FLOAT_EQ(target->block()->given_min_width, 15.0f);
    }
}

TEST_F(IncrementalCssTest, LateGeometryStorageReceivesComputedInitialValues) {
    set_property("color", "red");
    resolve();
    ASSERT_EQ(target->bound, nullptr);
    ASSERT_EQ(target->position, nullptr);
    target->ensure_boundary(&context);
    target->ensure_position(&context);
    target->boundary_mut()->margin.top = 99.0f;
    target->position->position = CSS_VALUE_ABSOLUTE;
    target->position->has_top = true;
    target->position->top = 50.0f;
    int64_t full = g_style_resolve_measure;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->boundary()->margin.top, 0.0f);
    EXPECT_EQ(target->position->position, CSS_VALUE_STATIC);
    EXPECT_FALSE(target->position->has_top);
    EXPECT_TRUE(isnan(target->position->top_percent));
}

TEST_F(IncrementalCssTest, UsedBoxChangesPreserveComputedValuesAcrossViewRoles) {
    target->view_type = RDT_VIEW_INLINE;
    set_property("width", "50%");
    set_property("padding-top", "calc(1em + 10%)");
    set_property("margin-top", "10%");
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_width, 100.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 36.0f);
    int64_t full = g_style_resolve_measure;
    target->view_type = RDT_VIEW_BLOCK;
    containing.content_width = containing.given_width = 400.0f;
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_width, 200.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 56.0f);
    target->boundary_mut()->margin.top += 70.0f;
    target->view_type = RDT_VIEW_INLINE;
    containing.content_width = containing.given_width = 200.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 100.0f);
    EXPECT_FLOAT_EQ(context.block.given_width, 100.0f);
    EXPECT_FLOAT_EQ(target->boundary()->padding.top, 36.0f);
    EXPECT_FLOAT_EQ(target->boundary()->margin.top, 20.0f);
    int64_t geometry = g_style_geometry_evaluations;
    resolve();
    EXPECT_EQ(g_style_geometry_evaluations, geometry);
    EXPECT_EQ(g_style_resolve_measure, full);
}

TEST_F(IncrementalCssTest, IntrinsicPercentageHeightIgnoresProvisionalAutoParentSizes) {
    DomElement* child = append_intrinsic_child();
    set_property("display", "block");
    set_property(child, "display", "inline-block");
    set_property(child, "height", "100%");
    set_property(child, "padding", "8px");
    set_property(child, "box-sizing", "border-box");
    resolve();
    target->height = 1000.0f;
    containing.content_height = 1000.0f;
    containing.given_height = -1.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 16.0f);
    set_property(child, "height", "calc(50% + 10px)");
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 16.0f);
    set_property("height", "60px");
    resolve();
    containing.content_height = containing.given_height = 60.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 40.0f);
    radiant::intrinsic_height_cache_release(child->layout_cache, tree.prop_pool);
    radiant::css_resolved_style_release(child->layout_cache, tree.prop_pool);
}

TEST_F(IncrementalCssTest, SizeQueriesReadCleanStyleAfterLayoutAssignsPreferredSizes) {
    set_property("height", "30px");
    resolve();
    target->block_mut()->given_height = 200.0f;
    radiant::CssResolvedSize size = {};
    ASSERT_TRUE(radiant::css_resolved_style_size(&context, target, LAYOUT_AXIS_Y, &size));
    EXPECT_FLOAT_EQ(size.value, 30.0f);
    EXPECT_FALSE(size.percentage);
    EXPECT_FLOAT_EQ(target->block()->given_height, 200.0f);
}

TEST_F(IncrementalCssTest, CommittedStylesValidateConstraintsAndRestoreCleanSizes) {
    set_property("height", "30px");
    set_property("width", "50%");
    context.run_mode = radiant::RunMode::PerformLayout;
    resolve();
    ASSERT_TRUE(target->styles_resolved());
    int64_t full = g_style_resolve_full;
    target->block_mut()->given_height = 200.0f;
    containing.content_width = containing.given_width = 400.0f;
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_height, 30.0f);
    EXPECT_FLOAT_EQ(target->block()->given_width, 200.0f);
    EXPECT_EQ(g_style_resolve_full, full);
    containing.content_width = containing.given_width = 200.0f;
    resolve();
    EXPECT_FLOAT_EQ(target->block()->given_width, 100.0f);
    EXPECT_EQ(g_style_resolve_full, full);
}

TEST_F(IncrementalCssTest, HeightQueriesKeyFlexAssignedDefinitenessWithoutStyleChanges) {
    DomElement* parent = append_intrinsic_child();
    DomElement* child = append_intrinsic_child(parent);
    set_property("display", "flex");
    set_property("flex-direction", "column");
    set_property("height", "200px");
    set_property(parent, "display", "block");
    set_property(child, "display", "block");
    set_property(child, "height", "100%");
    set_property(child, "padding", "8px");
    set_property(child, "box-sizing", "border-box");
    resolve();
    parent->ensure_flex_item(&tree);
    resolve_child(parent);
    parent->height = 80.0f;
    containing.content_height = 80.0f;
    containing.given_height = -1.0f;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 16.0f);
    uint64_t computations = context.profiler.height_computations;
    int64_t full = g_style_resolve_measure;
    parent->fi->main_size_from_flex = true;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 80.0f);
    EXPECT_EQ(context.profiler.height_computations, computations + 1);
    EXPECT_EQ(g_style_resolve_measure, full);
    parent->fi->main_size_from_flex = false;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, child, 80.0f), 16.0f);
    EXPECT_EQ(context.profiler.height_computations, computations + 1);
    DomElement* descendants[] = {child, parent};
    for (DomElement* element : descendants) {
        radiant::intrinsic_height_cache_release(element->layout_cache, tree.prop_pool);
        radiant::css_resolved_style_release(element->layout_cache, tree.prop_pool);
    }
}

TEST_F(IncrementalCssTest, PercentageHeightUsesOnlyItsOwningFlexConstraint) {
    DomElement* parent = append_intrinsic_child();
    DomElement* child = append_intrinsic_child(parent);
    set_property("display", "flex");
    set_property("flex-direction", "column");
    set_property(parent, "display", "block");
    set_property(child, "display", "block");
    set_property(child, "height", "100%");
    set_property(child, "padding", "8px");
    set_property(child, "box-sizing", "border-box");
    resolve();
    parent->ensure_flex_item(&tree);
    resolve_child(parent);
    parent->height = 80.0f;
    containing.content_height = containing.given_height = 80.0f;
    containing.flow_box = lam::up(lam::unsafe_view_block_element_storage(parent));
    FlexContainerLayout flex = {};
    flex.container = lam::up(lam::unsafe_view_block_element_storage(target));
    flex.direction = DIR_COLUMN;
    context.flex_container = lam::up(&flex);
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, parent, 80.0f), 16.0f);
    uint64_t computations = context.profiler.height_computations;
    flex.main_axis_available_size_is_definite = true;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, parent, 80.0f), 80.0f);
    EXPECT_EQ(context.profiler.height_computations, computations + 2);
    flex.main_axis_available_size_is_definite = false;
    EXPECT_FLOAT_EQ(calculate_max_content_height(&context, parent, 80.0f), 16.0f);
    EXPECT_EQ(context.profiler.height_computations, computations + 2);
    // an outer flex container cannot make another box's auto-height constraint definite.
    flex.main_axis_available_size_is_definite = true;
    containing.flow_box = lam::up(lam::unsafe_view_block_element_storage(target));
    EXPECT_TRUE(layout_percentage_height_is_automatic(
        lam::unsafe_view_block_element_storage(child), &context));
    context.flex_container = nullptr;
    DomElement* descendants[] = {child, parent};
    for (DomElement* element : descendants) {
        radiant::intrinsic_height_cache_release(element->layout_cache, tree.prop_pool);
        radiant::css_resolved_style_release(element->layout_cache, tree.prop_pool);
    }
}

TEST_F(IncrementalCssTest, RestoresComputedFlexPropertiesAfterItemInitialization) {
    set_property("flex", "2 3 40%");
    set_property("order", "5");
    set_property("align-self", "center");
    set_property("aspect-ratio", "2");
    resolve();
    ASSERT_NE(target->flex_item(), nullptr);
    ASSERT_FLOAT_EQ(target->fi->flex_grow, 2.0f);
    ASSERT_FLOAT_EQ(target->fi->flex_basis, 40.0f);
    int64_t full = g_style_resolve_measure;
    reset_flex_item_prop_for_style(&context,
        lam::view_require_element(static_cast<View*>(target)));
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->fi->flex_grow, 2.0f);
    EXPECT_FLOAT_EQ(target->fi->flex_shrink, 3.0f);
    EXPECT_FLOAT_EQ(target->fi->flex_basis, 40.0f);
    EXPECT_TRUE(target->fi->flex_basis_is_percent);
    EXPECT_FLOAT_EQ(target->fi->aspect_ratio, 2.0f);
    EXPECT_EQ(target->fi->align_self, CSS_VALUE_CENTER);
    EXPECT_EQ(target->fi->order, 5);
}

TEST_F(IncrementalCssTest, RasterScaleInvalidatesComputedMath) {
    UiContext ui = {};
    ui.device_scale = 1.0f;
    context.ui_context = lam::up(&ui);
    set_property("width", "round(line-width, 1.3px)");
    resolve();
    ASSERT_FLOAT_EQ(target->block()->given_width, 1.0f);
    int64_t full = g_style_resolve_measure;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    ui.device_scale = 2.0f;
    resolve();
    EXPECT_GT(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 1.5f);
    context.ui_context = nullptr;
}

TEST_F(IncrementalCssTest, RootFontChangesInvalidateNormalizedRemBindings) {
    context.root_font_size = 16.0f;
    set_property("width", "calc(1rem + 10%)");
    resolve();
    ASSERT_FLOAT_EQ(target->block()->given_width, 36.0f);
    int64_t full = g_style_resolve_measure;
    containing.content_width = containing.given_width = 400.0f;
    resolve();
    EXPECT_EQ(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 56.0f);
    context.root_font_size = 20.0f;
    resolve();
    EXPECT_GT(g_style_resolve_measure, full);
    EXPECT_FLOAT_EQ(target->block()->given_width, 60.0f);
}

TEST_F(RadiantCoreTest, TableTraversalResumesAcrossTransparentWrappersWithinItsOwner) {
    auto append_view = [&](ViewType view_type, DomElement* parent) {
        DomElement* child = element();
        child->view_type = view_type;
        child->set_styles_resolved(true);
        if (view_type == RDT_VIEW_INLINE) {
            child->display = {CSS_VALUE_CONTENTS, CSS_VALUE_FLOW};
        }
        if (parent) EXPECT_TRUE(parent->DomNode::append_child(child));
        return child;
    };
    DomElement* table_node = append_view(RDT_VIEW_TABLE, nullptr);
    DomElement* group = append_view(RDT_VIEW_TABLE_ROW_GROUP, table_node);
    DomElement* contents = append_view(RDT_VIEW_INLINE, group);
    ViewTableRow* first = static_cast<ViewTableRow*>(append_view(RDT_VIEW_TABLE_ROW, contents));
    ViewTableRow* second = static_cast<ViewTableRow*>(append_view(RDT_VIEW_TABLE_ROW, group));
    DomElement* next_group = append_view(RDT_VIEW_TABLE_ROW_GROUP, table_node);
    ViewTableRow* third = static_cast<ViewTableRow*>(append_view(RDT_VIEW_TABLE_ROW, next_group));
    DomElement* nested_table = append_view(RDT_VIEW_TABLE, table_node);
    ViewTableRow* foreign = static_cast<ViewTableRow*>(append_view(RDT_VIEW_TABLE_ROW, nested_table));
    ViewTable* table = static_cast<ViewTable*>(table_node);
    ViewTableRowGroup* row_group = static_cast<ViewTableRowGroup*>(group);
    EXPECT_EQ(table->first_row(), first);
    EXPECT_EQ(table->next_row(first), second);
    EXPECT_EQ(table->next_row(second), third);
    EXPECT_EQ(table->next_row(third), nullptr);
    EXPECT_EQ(table->next_row(foreign), nullptr);
    EXPECT_EQ(row_group->first_row(), first);
    EXPECT_EQ(row_group->next_row(first), second);
    EXPECT_EQ(row_group->next_row(second), nullptr);
    EXPECT_EQ(row_group->next_row(third), nullptr);
    DomElement* cell_contents = append_view(RDT_VIEW_INLINE, first);
    ViewTableCell* first_cell = static_cast<ViewTableCell*>(append_view(RDT_VIEW_TABLE_CELL, cell_contents));
    ViewTableCell* second_cell = static_cast<ViewTableCell*>(append_view(RDT_VIEW_TABLE_CELL, first));
    ViewTableCell* foreign_cell = static_cast<ViewTableCell*>(append_view(RDT_VIEW_TABLE_CELL, foreign));
    EXPECT_EQ(first->first_cell(), first_cell);
    EXPECT_EQ(first->next_cell(first_cell), second_cell);
    EXPECT_EQ(first->next_cell(second_cell), nullptr);
    EXPECT_EQ(first->next_cell(foreign_cell), nullptr);
}

TEST_F(RadiantCoreTest, ComputedFamilyListsShareOneTreeLifetimeString) {
    CssValue* parts[] = {
        css_value_create_string(tree.prop_pool, "Arial"),
        css_value_create_keyword(tree.prop_pool, "sans-serif"),
    };
    CssValue* list = css_value_create_list(tree.prop_pool, parts, 2);
    ASSERT_NE(list, nullptr);
    list->data.list.comma_separated = true;
    LayoutContext lycon = {};
    lycon.selected_view_tree = lam::up(&tree);

    const char* first = css_select_font_family(&lycon, list);
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first, "Arial, sans-serif");
    // inherited and copied fonts borrow the family; nothing frees it per restyle
    FontProp parent = {};
    FontProp child = {};
    radiant_retain_font_family(&parent, lam::PoolPtr<char>((char*)first));
    radiant_copy_font_values(&child, &parent);

    PoolStats warm = {};
    pool_get_detailed_stats(tree.prop_pool, &warm);
    for (int i = 0; i < 256; i++) {
        const char* again = css_select_font_family(&lycon, list);
        ASSERT_EQ(again, first);
        radiant_retain_font_family(&parent, lam::PoolPtr<char>((char*)again));
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(tree.prop_pool, &repeated);
    EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    EXPECT_STREQ(child.family, "Arial, sans-serif");

    // equal text from a foreign, unterminated buffer reuses the canonical copy
    char foreign[] = "Arial, sans-serif, monospace";
    EXPECT_EQ(view_tree_canonical_font_family(&tree, foreign, 17), first);
    const char* other = view_tree_canonical_font_family(&tree, foreign, strlen(foreign));
    ASSERT_NE(other, nullptr);
    EXPECT_NE(other, first);
    EXPECT_STREQ(other, "Arial, sans-serif, monospace");
    EXPECT_EQ(tree.canonical_stats.font_family_misses, 2u);
}

TEST_F(RadiantCoreTest, SingleFontFamiliesOutliveComputedValueScratch) {
    LayoutContext lycon = {};
    lycon.selected_view_tree = lam::up(&tree);
    Pool* scratch = pool_create();
    ASSERT_NE(scratch, nullptr);
    CssValue* family = css_value_create_string(scratch, "Arial");
    ASSERT_NE(family, nullptr);
    CssValue* parts[] = {family};
    CssValue* list = css_value_create_list(scratch, parts, 1);
    ASSERT_NE(list, nullptr);
    const char* selected = css_select_font_family(&lycon, family);
    ASSERT_NE(selected, nullptr);
    EXPECT_NE(selected, family->data.string);
    EXPECT_EQ(css_select_font_family(&lycon, list), selected);
    pool_destroy(scratch);
    EXPECT_STREQ(selected, "Arial");
}

TEST_F(RadiantCoreTest, CanonicalCapFallsBackToOwnedStorage) {
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
    Element* backing = nullptr;

    void SetUp() override {
        ASSERT_TRUE(doc.init(&input));
        backing = elmt_arena(doc.node_arena);
        ASSERT_NE(backing, nullptr);
    }

    void TearDown() override {
        doc.destroy();
    }

    DomElement* element(const char* tag) {
        // retirement fixtures need backed nodes; null backing marks layout-only nodes synthetic.
        DomElement* value = DomElement::create(&doc, tag, backing);
        EXPECT_NE(value, nullptr);
        if (value) EXPECT_FALSE(value->is_synthetic());
        return value;
    }

    DomElement* root() {
        DomElement* value = element("root");
        doc.root = lam::up(value);
        return value;
    }

    bool attach(DomElement* parent, DomElement* child) {
        // lifecycle tests link the DOM chain without editing Lambda content.
        return static_cast<DomNode*>(parent)->append_child(child);
    }

    CssTransitionElemState* transition_snapshot(DomElement* child) {
        auto* snapshot = static_cast<CssTransitionElemState*>(pool_calloc(
            doc.document_pool, sizeof(CssTransitionElemState)));
        if (!snapshot) return nullptr;
        snapshot->pool = doc.document_pool;
        if (!lam::pool_grow_array(snapshot->pool, &snapshot->tracks,
                &snapshot->track_capacity, 1, 4)) {
            pool_free(doc.document_pool, snapshot);
            return nullptr;
        }
        snapshot->track_count = 1;
        child->set_transition_state_prop(snapshot);
        return snapshot;
    }
};

TEST_F(DomRetirementTest, FocusValidationUsesOwnedRootAfterReflow) {
    DomElement* owned_root = root();
    DomElement* parser_document = element("#document");
    DomElement* button = element("button");
    ASSERT_TRUE(attach(parser_document, owned_root));
    ASSERT_TRUE(attach(owned_root, button));
    owned_root->view_type = parser_document->view_type = button->view_type = RDT_VIEW_BLOCK;
    ASSERT_NE(state_store_create(&doc), nullptr);
    DocState* state = doc.state;

    // defer mutation assertions so the test can inspect the post-prune invariant.
    state->transition_depth++;
    focus_set_programmatic(state, button);
    state_store_prune_after_reflow(state);
    EXPECT_EQ(focus_get(state), button);
    EXPECT_TRUE(state_get_bool(state, owned_root, STATE_FOCUS_WITHIN));
    EXPECT_FALSE(state_get_bool(state, parser_document, STATE_FOCUS_WITHIN));
    StateValidationReport report{};
    EXPECT_TRUE(radiant_state_validate_interaction(state, &report)) << report.message;
    state_set_bool(state, owned_root, STATE_FOCUS_WITHIN, false);
    EXPECT_FALSE(radiant_state_validate_interaction(state, &report));
    EXPECT_STREQ(report.message, ":focus-within ancestry is inconsistent");
    state_set_bool(state, owned_root, STATE_FOCUS_WITHIN, true);
    EXPECT_TRUE(parser_document->remove_child(owned_root));
    state->transition_depth--;
}

TEST_F(DomRetirementTest, InteractionGraphsRejectFlagsOnUnrelatedSiblings) {
    DomElement* owned_root = root();
    DomElement* focused = element("button");
    DomElement* sibling = element("button");
    ASSERT_TRUE(attach(owned_root, focused));
    ASSERT_TRUE(attach(owned_root, sibling));
    owned_root->view_type = focused->view_type = sibling->view_type = RDT_VIEW_BLOCK;
    ASSERT_NE(state_store_create(&doc), nullptr);
    DocState* state = doc.state;
    state->transition_depth++;
    focus_set_programmatic(state, focused);
    struct InvalidFlag { const char* name; const char* message; } cases[] = {
        {STATE_FOCUS, "focus pseudo-state does not match focus target"},
        {STATE_FOCUS_WITHIN, ":focus-within ancestry is inconsistent"},
        {STATE_FOCUS_VISIBLE, ":focus-visible state is inconsistent"},
        {STATE_HOVER, ":hover ancestry chain is inconsistent"},
        {STATE_ACTIVE, ":active ancestry chain is inconsistent"},
    };
    for (const InvalidFlag& test : cases) {
        SCOPED_TRACE(test.name);
        StateValidationReport report{};
        ASSERT_TRUE(radiant_state_validate_interaction(state, &report)) << report.message;
        // validation must still find stray positive flags outside every target path.
        state_set_bool(state, sibling, test.name, true);
        EXPECT_FALSE(radiant_state_validate_interaction(state, &report));
        EXPECT_STREQ(report.message, test.message);
        state_set_bool(state, sibling, test.name, false);
    }
    EXPECT_TRUE(radiant_state_validate_interaction(state, nullptr));
    state->transition_depth--;
}

class DomGcBackingTest : public DomRetirementTest {
protected:
    Runtime runtime = {};
    EvalContext evaluator = {};
    Heap heap = {};
    TypeElmt data_type = EmptyElmt;
    DomDocument target_doc;
    Runtime target_runtime = {};
    EvalContext target_evaluator = {};
    Heap target_heap = {};
    bool target_initialized = false;

    void SetUp() override {
        DomRetirementTest::SetUp();
        heap.gc = gc_heap_create();
        ASSERT_NE(heap.gc, nullptr);
        evaluator.heap = &heap;
        runtime.eval_context = &evaluator;
        doc.lambda_runtime = &runtime;
    }

    void TearDown() override {
        // the test owns its evaluator; release native roots before its heap.
        if (target_initialized) {
            dom_lifecycle_release_backing_roots(&target_doc);
            target_doc.lambda_runtime = nullptr;
            target_doc.destroy();
            gc_heap_destroy(target_heap.gc);
        }
        dom_lifecycle_release_backing_roots(&doc);
        doc.lambda_runtime = nullptr;
        DomRetirementTest::TearDown();
        gc_heap_destroy(heap.gc);
    }

    DomElement* runtime_element(int64_t children = 0, bool attributes = false) {
        Element* source = static_cast<Element*>(gc_heap_calloc(
            heap.gc, sizeof(Element), LMD_TYPE_ELEMENT));
        if (!source) return nullptr;
        source->type_id = LMD_TYPE_ELEMENT;
        source->type = &EmptyElmt;
        if (attributes) {
            data_type.byte_size = sizeof(uint64_t);
            source->type = &data_type;
            source->data = gc_data_alloc(heap.gc, data_type.byte_size);
            source->data_cap = data_type.byte_size;
        }
        if (children) {
            source->items = static_cast<Item*>(gc_data_alloc(heap.gc, sizeof(Item) * children));
            source->length = source->capacity = children;
        }
        return DomElement::create(&doc, "span", source);
    }

    String* runtime_string() {
        const char* content = "retained text";
        size_t length = strlen(content);
        String* string = static_cast<String*>(gc_heap_calloc(
            heap.gc, sizeof(String) + length + 1, LMD_TYPE_STRING));
        if (!string) return nullptr;
        string->len = length;
        memcpy(string->chars, content, length + 1);
        return string;
    }

    void init_adoption_target(bool separate_heap) {
        ASSERT_TRUE(target_doc.init(&input));
        target_initialized = true;
        target_heap.gc = gc_heap_create();
        ASSERT_NE(target_heap.gc, nullptr);
        target_evaluator.heap = separate_heap ? &target_heap : &heap;
        target_runtime.eval_context = &target_evaluator;
        target_doc.lambda_runtime = &target_runtime;
    }

    void check_adoption(bool separate_heap) {
        init_adoption_target(separate_heap);
        ASSERT_TRUE(target_initialized);
        DomElement* child = runtime_element(1);
        ASSERT_NE(child, nullptr);
        Element* source = dom_element_render_source(child);
        Item* original_items = source->items;
        // adoption must refresh a borrow even when nobody read it after collection.
        gc_collect(heap.gc, nullptr, 0);
        ASSERT_NE(source->items, original_items);
        uint32_t id = dom_document_alloc_node_id(&target_doc);
        ASSERT_TRUE(dom_node_registry_transfer(&doc, &target_doc, child, &id));
        static_cast<DomNode*>(child)->id = id;
        child->doc = lam::up(&target_doc);
        ASSERT_EQ(heap.gc->root_slot_count, 1);
        EXPECT_EQ(target_heap.gc->root_slot_count, 0);
        dom_lifecycle_release_backing_roots(&doc);
        gc_collect(heap.gc, nullptr, 0);
        EXPECT_EQ(heap.gc->object_count, 1u);
        EXPECT_EQ(dom_element_to_element(child)->items, source->items);
        id = dom_document_alloc_node_id(&doc);
        ASSERT_TRUE(dom_node_registry_transfer(&target_doc, &doc, child, &id));
        static_cast<DomNode*>(child)->id = id;
        child->doc = lam::up(&doc);
        EXPECT_EQ(heap.gc->root_slot_count, 1);
        dom_lifecycle_release_backing_roots(&target_doc);
        EXPECT_EQ(heap.gc->root_slot_count, 1);
        dom_node_schedule_detached(&doc, child);
        EXPECT_EQ(dom_retire_sweep(&doc), 1u);
        EXPECT_EQ(heap.gc->root_slot_count, 0);
        gc_collect(heap.gc, nullptr, 0);
        EXPECT_EQ(heap.gc->object_count, 0u);
    }
};

TEST_F(DomGcBackingTest, DetachedWrapperRetainsBackingUntilUnpinnedRetirement) {
    DomElement* parent = root();
    DomElement* child = runtime_element();
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 1u);
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, RecycledNodesDoNotAccumulateBackingRoots) {
    DomElement* parent = root();
    for (int i = 0; i < 1024; i++) {
        DomElement* child = runtime_element();
        ASSERT_NE(child, nullptr);
        ASSERT_EQ(heap.gc->root_slot_count, 1);
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
        ASSERT_EQ(heap.gc->root_slot_count, 0);
        gc_collect(heap.gc, nullptr, 0);
        ASSERT_EQ(heap.gc->object_count, 0u);
    }
}

TEST_F(DomGcBackingTest, RuntimeTeardownDropsAttachedRootsIdempotently) {
    ASSERT_NE(runtime_element(), nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    dom_lifecycle_release_backing_roots(&doc);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    dom_lifecycle_release_backing_roots(&doc);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, BorrowedContentBufferFollowsCompaction) {
    DomElement* child = runtime_element(1);
    ASSERT_NE(child, nullptr);
    Element* source = dom_element_render_source(child);
    Item* original = source->items;
    ASSERT_EQ(dom_element_to_element(child)->items, original);
    gc_collect(heap.gc, nullptr, 0);
    ASSERT_NE(source->items, original);
    EXPECT_EQ(dom_element_to_element(child)->items, source->items);
}

TEST_F(DomGcBackingTest, BorrowedAttributeBufferFollowsCompaction) {
    DomElement* child = runtime_element(0, true);
    ASSERT_NE(child, nullptr);
    Element* source = dom_element_render_source(child);
    void* original = source->data;
    gc_collect(heap.gc, nullptr, 0);
    ASSERT_NE(source->data, original);
    EXPECT_EQ(dom_element_to_element(child)->data, source->data);
}

TEST_F(DomGcBackingTest, IndependentDomBuffersSurviveLaterOwnerCompaction) {
    DomElement* child = runtime_element(1, true);
    ASSERT_NE(child, nullptr);
    Element* backing = dom_element_to_element(child);
    void* owned_data = arena_calloc(doc.node_arena, sizeof(uint64_t));
    Item* owned_items = static_cast<Item*>(arena_calloc(doc.node_arena, sizeof(Item)));
    backing->data = owned_data;
    backing->items = owned_items;
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(dom_element_to_element(child)->data, owned_data);
    EXPECT_EQ(dom_element_to_element(child)->items, owned_items);
}

TEST_F(DomGcBackingTest, AdoptionAndReturnKeepPhysicalHeapRoot) {
    check_adoption(true);
}

TEST_F(DomGcBackingTest, SameHeapAdoptionRefreshesBorrowedPointers) {
    check_adoption(false);
}

TEST(DomRetirementOwnerArenaTest, AdoptedMetadataRetiresFromItsPhysicalPool) {
    Pool* input_pool = pool_create();
    ASSERT_NE(input_pool, nullptr);
    Input* input = Input::create(input_pool, nullptr);
    ASSERT_NE(input, nullptr);
    DomDocument doc;
    DomDocument target_doc;
    ASSERT_TRUE(doc.init(input));
    ASSERT_TRUE(target_doc.init(input));
    MarkBuilder builder(input);
    DomElement* child = DomElement::create(&doc, "span", builder.element("span").final().element);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("id", "source-id"));
    ASSERT_TRUE(child->set_attribute("class", "source-class"));
    ASSERT_TRUE(child->set_attribute("data-storage", "source"));
    uint32_t id = dom_document_alloc_node_id(&target_doc);
    ASSERT_TRUE(dom_node_registry_transfer(&doc, &target_doc, child, &id));
    static_cast<DomNode*>(child)->id = id;
    child->doc = lam::up(&target_doc);
    EXPECT_EQ(child->storage_owner(), &doc);
    EXPECT_TRUE(pool_owns(doc.document_pool, child->tag_name));
    ASSERT_TRUE(child->set_attribute("id", "destination-id"));
    ASSERT_TRUE(child->set_attribute("class", "destination-class"));
    ASSERT_TRUE(child->set_attribute("data-storage", "destination"));
    const char* tag = child->tag_name;
    const char* cached_id = child->id;
    const char* cached_class = child->class_names[0];
    DomElementExt* extension = child->ext;
    EXPECT_TRUE(pool_owns(doc.document_pool, cached_id));
    EXPECT_TRUE(pool_owns(doc.document_pool, cached_class));
    dom_node_schedule_detached(&target_doc, child);
    EXPECT_EQ(dom_retire_sweep(&target_doc), 1u);
    EXPECT_FALSE(pool_owns(doc.document_pool, tag));
    EXPECT_FALSE(pool_owns(doc.document_pool, cached_id));
    EXPECT_FALSE(pool_owns(doc.document_pool, cached_class));
    EXPECT_FALSE(pool_owns(doc.document_pool, extension));
    target_doc.destroy();
    doc.destroy();
    pool_destroy(input_pool);
}

TEST_F(DomGcBackingTest, AdoptedOwnedTextTracksReplacementAndReturnPools) {
    init_adoption_target(true);
    DomText* text = DomText::create_detached_copy(&doc, "inline", 6);
    ASSERT_NE(text, nullptr);
    String* original = dom_document_create_string(&doc, "source", 6);
    ASSERT_TRUE(dom_text_adopt_document_string(text, &doc, original));
    uint32_t id = dom_document_alloc_node_id(&target_doc);
    ASSERT_TRUE(dom_node_registry_transfer(&doc, &target_doc, text, &id));
    text->id = id;
    ASSERT_TRUE(dom_text_adopt_document_string(text, &target_doc, original));
    EXPECT_EQ(dom_node_registry_owned_string_pool(&target_doc, text), doc.document_pool);
    String* replacement = dom_document_create_string(&target_doc, "destination", 11);
    ASSERT_TRUE(dom_text_adopt_document_string(text, &target_doc, replacement));
    EXPECT_FALSE(pool_owns(doc.document_pool, original));
    EXPECT_TRUE(pool_owns(target_doc.document_pool, replacement));
    id = dom_document_alloc_node_id(&doc);
    ASSERT_TRUE(dom_node_registry_transfer(&target_doc, &doc, text, &id));
    text->id = id;
    EXPECT_EQ(dom_node_registry_owned_string_pool(&doc, text), target_doc.document_pool);
    dom_node_schedule_detached(&doc, text);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_FALSE(pool_owns(target_doc.document_pool, replacement));
}

struct BackingRootTeardownProbe : DomDocumentResourceData {
    gc_heap_t* gc;
    bool called;
};

static void backing_root_teardown_probe(DomDocumentResourceData* resource) {
    auto* probe = static_cast<BackingRootTeardownProbe*>(resource);
    // adopted-document resources may destroy the physical source heap here.
    EXPECT_EQ(probe->gc->root_slot_count, 0);
    probe->called = true;
}

TEST_F(DomGcBackingTest, BackingRootsWithdrawBeforeDocumentResourceTeardown) {
    ASSERT_NE(runtime_element(), nullptr);
    BackingRootTeardownProbe probe = {};
    probe.gc = heap.gc;
    ASSERT_TRUE(dom_document_add_resource(&doc, &probe, backing_root_teardown_probe));
    doc.lambda_runtime = nullptr;
    doc.destroy();
    EXPECT_TRUE(probe.called);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
}

TEST_F(DomGcBackingTest, DetachedTextRetainsItsRuntimeStringUntilRetirement) {
    String* source = runtime_string();
    ASSERT_NE(source, nullptr);
    DomText* text = DomText::create_detached(source, &doc);
    ASSERT_NE(text, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    DomNodeRef ref = dom_node_ref(text);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    dom_node_schedule_detached(&doc, text);
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 1u);
    EXPECT_STREQ(text->text, "retained text");
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, ReplacingTextBackingReleasesOldRuntimeString) {
    DomText* text = DomText::create_detached(runtime_string(), &doc);
    ASSERT_NE(text, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    String* replacement = dom_document_create_string(&doc, "edited", 6);
    ASSERT_NE(replacement, nullptr);
    ASSERT_TRUE(dom_text_adopt_document_string(text, &doc, replacement));
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
    EXPECT_STREQ(text->text, "edited");
    dom_node_schedule_detached(&doc, text);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
}

TEST_F(DomGcBackingTest, TextAdoptionKeepsPhysicalStringHeapRoot) {
    init_adoption_target(true);
    ASSERT_TRUE(target_initialized);
    DomText* text = DomText::create_detached(runtime_string(), &doc);
    ASSERT_NE(text, nullptr);
    uint32_t id = dom_document_alloc_node_id(&target_doc);
    ASSERT_TRUE(dom_node_registry_transfer(&doc, &target_doc, text, &id));
    text->id = id;
    EXPECT_EQ(heap.gc->root_slot_count, 1);
    EXPECT_EQ(target_heap.gc->root_slot_count, 0);
    dom_lifecycle_release_backing_roots(&doc);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 1u);
    EXPECT_STREQ(text->text, "retained text");
    dom_node_schedule_detached(&target_doc, text);
    EXPECT_EQ(dom_retire_sweep(&target_doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, DetachedCommentRetainsRuntimeContentUntilRetirement) {
    DomElement* element = runtime_element(1);
    ASSERT_NE(element, nullptr);
    Element* source = dom_element_render_source(element);
    TypeElmt comment_type = EmptyElmt;
    comment_type.name.str = "!--";
    comment_type.name.length = 3;
    source->type = &comment_type;
    source->items[0] = Item{.item = s2it(runtime_string())};
    DomComment* comment = DomComment::create_detached(source, &doc);
    ASSERT_NE(comment, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 2);
    dom_node_schedule_detached(&doc, element);
    ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 1);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 2u);
    EXPECT_STREQ(comment->content, "retained text");
    dom_node_schedule_detached(&doc, comment);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomRetirementTest, UnpinnedDetachedNodeRetiresAndRejectsStaleRef) {
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
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

TEST_F(DomRetirementTest, AttributePinKeepsDetachedOwnerAndRetiresTogether) {
    DomElement* parent = root();
    DomElement* owner = element("owner");
    ASSERT_TRUE(attach(parent, owner));
    DomAttr* attribute = dom_attribute_node_create(&doc, "", "data-retained", "value");
    ASSERT_NE(attribute, nullptr);
    DomNodeRef ref = dom_node_ref(attribute);
    dom_attribute_node_attach(owner, attribute);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    ASSERT_TRUE(parent->remove_child(owner));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(attribute->owner_element.get(), owner);
    EXPECT_STREQ(attribute->value, "value");
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
}

TEST_F(DomRetirementTest, AttributeArenaGrowthPlateausAcrossTenThousandRemovals) {
    DomElement* owner = root();
    ArenaStats warm = {};
    for (int i = 0; i < 10128; i++) {
        DomAttr* attribute = dom_attribute_node_create(&doc, "urn:test", "a:value", "initial");
        ASSERT_NE(attribute, nullptr);
        dom_attribute_node_attach(owner, attribute);
        ASSERT_TRUE(dom_attribute_node_set_value(attribute, "changed"));
        dom_attribute_node_detach(attribute);
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
        if (i == 127) arena_get_stats(doc.node_arena, &warm);
    }
    ArenaStats after = {};
    arena_get_stats(doc.node_arena, &after);
    EXPECT_EQ(after.fresh_growth_bytes, warm.fresh_growth_bytes);
    EXPECT_GE(after.bump_back_count - warm.bump_back_count, 10000u);
}

TEST_F(DomRetirementTest, PinBlocksRetirementUntilReleased) {
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
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
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);

    ASSERT_TRUE(parent->remove_child(child));
    ASSERT_TRUE(attach(parent, child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);
}

TEST_F(DomRetirementTest, PinnedDescendantBlocksBottomUpSubtreeRetirement) {
    DomElement* outer = root();
    DomElement* branch = element("branch");
    DomElement* leaf = element("leaf");
    ASSERT_TRUE(attach(outer, branch));
    ASSERT_TRUE(attach(branch, leaf));
    DomNodeRef leaf_ref = dom_node_ref(leaf);
    ASSERT_TRUE(dom_node_pin(&doc, leaf_ref, DOM_NODE_PIN_RANGE));

    ASSERT_TRUE(outer->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    ASSERT_TRUE(dom_node_unpin(&doc, leaf_ref, DOM_NODE_PIN_RANGE));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
}

TEST_F(DomRetirementTest, UnregisteredLayoutBoxDoesNotRetainAuthoredSubtree) {
    DomElement* parent = root();
    DomElement* branch = element("branch");
    DomElement* generated = DomElement::create_in(doc.document_pool);
    ASSERT_NE(generated, nullptr);
    ASSERT_TRUE(generated->is_synthetic());
    ASSERT_TRUE(attach(parent, branch));
    ASSERT_TRUE(attach(branch, generated));
    DomNodeRef ref = dom_node_ref(branch);

    ASSERT_TRUE(parent->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
}

TEST_F(DomRetirementTest, RetirementRepairsStaleEdgesOnceForTheWholeBatch) {
    DomElement* parent = root();
    DomElement* branch = element("branch");
    ASSERT_TRUE(attach(parent, branch));
    for (int i = 0; i < 1024; i++) {
        ASSERT_TRUE(attach(branch, element("leaf")));
    }
    DomElement* retained = element("retained");
    ASSERT_TRUE(attach(parent, retained));

    // A backing-tree edit can leave raw edges in a surviving wrapper.
    retained->first_child = branch->first_child;
    retained->last_child = branch->last_child;
    ASSERT_TRUE(parent->remove_child(branch));
    retained->prev_sibling = lam::up(branch);
    EXPECT_EQ(dom_retire_sweep(&doc), 1025u);
    EXPECT_EQ(retained->first_child, nullptr);
    EXPECT_EQ(retained->last_child, nullptr);
    EXPECT_EQ(retained->prev_sibling, nullptr);
    EXPECT_EQ(parent->first_child, retained);
    EXPECT_EQ(parent->last_child, retained);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retirement_edge_visits, stats.registered_nodes);
}

TEST_F(DomRetirementTest, ConsumedMutationRecordsReleaseDetachedSubtree) {
    DomElement* parent = root();
    DomElement* branch = element("branch");
    DomElement* leaf = element("leaf");
    ASSERT_TRUE(attach(parent, branch));
    ASSERT_TRUE(attach(branch, leaf));
    DomNodeRef ref = dom_node_ref(leaf);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_RECONCILE));
    doc.js.mutation_record_count = 1;
    doc.js.mutation_records[0].target = leaf;
    doc.js.mutation_records[0].target_id = ref.expected_id;
    ASSERT_TRUE(parent->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);

    dom_js_mutation_records_reset(&doc);
    EXPECT_EQ(doc.js.mutation_record_count, 0);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retired_nodes, 2u);
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

TEST_F(DomRetirementTest, TransitionSnapshotLivesUntilUnpinnedRetirement) {
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    auto* snapshot = transition_snapshot(child);
    ASSERT_NE(snapshot, nullptr);
    CssTransitionTrack* tracks = snapshot->tracks;
    snapshot->tracks[0].has_snapshot = true;
    snapshot->tracks[0].snapshot.value.f = 0.25f;
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));

    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_TRUE(pool_owns(doc.document_pool, snapshot));
    EXPECT_TRUE(pool_owns(doc.document_pool, tracks));
    EXPECT_EQ(child->transition_state_prop(), snapshot);
    EXPECT_FLOAT_EQ(snapshot->tracks[0].snapshot.value.f, 0.25f);

    dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    EXPECT_FALSE(pool_owns(doc.document_pool, snapshot));
    EXPECT_FALSE(pool_owns(doc.document_pool, tracks));
}

TEST_F(DomRetirementTest, TransitionSnapshotStoragePlateausAcrossRetirements) {
    DomElement* parent = root();
    PoolStats warm = {};
    for (int i = 0; i < 1056; i++) {
        DomElement* child = element("child");
        auto* snapshot = transition_snapshot(child);
        ASSERT_NE(snapshot, nullptr);
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
        if (i == 31) pool_get_detailed_stats(doc.document_pool, &warm);
    }
    PoolStats after = {};
    pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_EQ(after.live_bytes, warm.live_bytes);
    EXPECT_GE(after.free_count - warm.free_count, 1024u);
}

struct DeferredDomRetirementScope {
    bool previous = dom_retire_set_deferred(true);
    ~DeferredDomRetirementScope() { dom_retire_set_deferred(previous); }
};

TEST_F(DomRetirementTest, DeferredRetirementWaitsForIdleAndRecyclesLater) {
    DeferredDomRetirementScope deferred;
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);

    // A zero recycle budget still commits logical retirement at the safe point.
    EXPECT_TRUE(dom_retire_idle(0));
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_GT(stats.pending_primary_bytes, 0u);
    EXPECT_EQ(stats.recycled_nodes, 0u);
    EXPECT_FALSE(dom_retire_idle(UINT64_MAX));
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.pending_primary_bytes, 0u);
    EXPECT_EQ(stats.recycled_nodes, 1u);
}

TEST_F(DomRetirementTest, ReattachedNodeSurvivesDeferredSweep) {
    DeferredDomRetirementScope deferred;
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(parent->remove_child(child));
    dom_retire_sweep(&doc);
    ASSERT_TRUE(attach(parent, child));
    EXPECT_FALSE(dom_retire_idle(UINT64_MAX));
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);
}

TEST_F(DomRetirementTest, DestroyDiscardsQueuedArenaRecycling) {
    DeferredDomRetirementScope deferred;
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    ASSERT_TRUE(parent->remove_child(child));
    dom_retire_sweep(&doc);
    EXPECT_TRUE(dom_retire_idle(0));
    dom_retire_begin_destroy(&doc);
    EXPECT_FALSE(dom_retire_idle(UINT64_MAX));
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.pending_primary_bytes, 0u);
    EXPECT_EQ(stats.recycled_nodes, 0u);
}

TEST_F(DomRetirementTest, RetiredTextLeavesBorrowedAncestorFontAllocated) {
    // the view tree owns the layout props a retirement sweep returns
    ViewTree tree = {};
    tree.prop_pool = lam::own(pool_create());
    ASSERT_NE(tree.prop_pool, nullptr);
    view_tree_canonical_init(&tree);
    doc.view_tree = lam::own(&tree);

    // layout_text points a text node at the FontProp of its nearest
    // font-owning ancestor; an unstyled span owns none, so its text borrows
    // the live parent's
    DomElement* parent = root();
    DomElement* span = element("span");
    ASSERT_TRUE(attach(parent, span));
    DomText* text = DomText::create_copy("world", 5, span);
    ASSERT_NE(text, nullptr);
    ASSERT_TRUE(span->append_child(text));
    FontProp* owned = (FontProp*)tree.alloc_prop(sizeof(FontProp));
    parent->font = lam::view_prop(owned);
    text->font = lam::view_ref(owned);
    TextRect* rect = tree.alloc_text_rect();
    ASSERT_NE(rect, nullptr);
    text->rect = lam::view_prop(rect);

    ASSERT_TRUE(parent->remove_child(span));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
    // the retired borrower only drops its pointer; freeing the prop left the
    // live parent's font dangling
    EXPECT_TRUE(pool_owns(tree.prop_pool, owned));
    EXPECT_EQ(parent->font, owned);
    EXPECT_EQ(tree.alloc_text_rect(), rect);

    parent->font = nullptr;
    doc.view_tree = nullptr;
    view_tree_canonical_destroy(&tree);
    pool_destroy(tree.prop_pool);
}

TEST_F(DomRetirementTest, NodeArenaGrowthPlateausAcrossTenThousandRetirements) {
    DomElement* parent = root();
    for (int i = 0; i < 128; i++) {
        DomElement* child = element("child");
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats warm = {};
    arena_get_stats(doc.node_arena, &warm);

    uint32_t last_id = 0;
    for (int i = 0; i < 10000; i++) {
        DomElement* child = element("child");
        uint32_t node_id = static_cast<DomNode*>(child)->id;
        ASSERT_GT(node_id, last_id);
        last_id = node_id;
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats after = {};
    arena_get_stats(doc.node_arena, &after);
    EXPECT_EQ(after.fresh_growth_bytes, warm.fresh_growth_bytes);
    EXPECT_GE(after.bump_back_count - warm.bump_back_count, 10000u);
}

TEST_F(DomRetirementTest, RecycledNodesReleaseTheirRegistryRecords) {
    DomElement* parent = root();
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    DomLifecycleStats warm_stats = {};
    dom_lifecycle_get_stats(&doc, &warm_stats);
    DomElement* children[64] = {};
    DomNodeRef refs[64] = {};
    for (int i = 0; i < 64; i++) {
        children[i] = element("child");
        ASSERT_NE(children[i], nullptr);
        refs[i] = dom_node_ref(static_cast<DomNode*>(children[i]));
        ASSERT_TRUE(attach(parent, children[i]));
    }
    for (int i = 0; i < 64; i++) ASSERT_TRUE(parent->remove_child(children[i]));
    EXPECT_EQ(dom_retire_sweep(&doc), 64u);
    // a record outlives no recycled slot; a stale ref still fails validation
    PoolStats after = {};
    pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_EQ(after.live_bytes, warm.live_bytes);
    for (int i = 0; i < 64; i++) EXPECT_EQ(dom_node_ref_validate(&doc, refs[i]), nullptr);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.recycled_nodes - warm_stats.recycled_nodes, 64u);
    // the recycled slots are reused by the next generation of nodes
    DomElement* next = element("child");
    ASSERT_NE(next, nullptr);
    EXPECT_NE(dom_node_ref_validate(&doc, dom_node_ref(static_cast<DomNode*>(next))), nullptr);
}

TEST_F(DomRetirementTest, WrapperOnlyDetachedSubtreesReportStrandedBytesForIdleCollection) {
    DomElement* parent = root();
    DomElement* held = element("held");
    DomElement* held_child = element("child");
    DomElement* observed = element("observed");
    ASSERT_TRUE(attach(parent, held));
    ASSERT_TRUE(attach(held, held_child));
    ASSERT_TRUE(attach(parent, observed));
    DomNodeRef held_ref = dom_node_ref(static_cast<DomNode*>(held));
    DomNodeRef observed_ref = dom_node_ref(static_cast<DomNode*>(observed));
    // a wrapper on a descendant strands the whole detached subtree; an observer
    // pin is not a wrapper, so that subtree waits on something a GC cannot clear
    ASSERT_TRUE(dom_node_pin(&doc, dom_node_ref(static_cast<DomNode*>(held_child)), DOM_NODE_PIN_WRAPPER));
    ASSERT_TRUE(dom_node_pin(&doc, observed_ref, DOM_NODE_PIN_OBSERVER));
    ASSERT_TRUE(parent->remove_child(held));
    ASSERT_TRUE(parent->remove_child(observed));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);

    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    ASSERT_GT(stats.wrapper_stranded_bytes, 0u);
    size_t stranded = stats.wrapper_stranded_bytes;
    EXPECT_TRUE(dom_retire_wrapper_collection_due(&doc, stranded));
    EXPECT_FALSE(dom_retire_wrapper_collection_due(&doc, stranded + 1));
    // repeated sweeps charge a detachment once
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.wrapper_stranded_bytes, stranded);
    // a collection that leaves the wrapper alive resets pacing to the survivors
    dom_retire_wrapper_collection_done(&doc);
    EXPECT_FALSE(dom_retire_wrapper_collection_due(&doc, 1));

    // clearing the wrapper (what the collection does) retires the subtree
    ASSERT_TRUE(dom_node_unpin(&doc, dom_node_ref(static_cast<DomNode*>(held_child)), DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
    EXPECT_EQ(dom_node_ref_validate(&doc, held_ref), nullptr);
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.wrapper_stranded_bytes, 0u);
    ASSERT_TRUE(dom_node_unpin(&doc, observed_ref, DOM_NODE_PIN_OBSERVER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
}

TEST_F(DomRetirementTest, MoreThanMutationRecordCapRetiresAfterPinsRelease) {
    DomElement* parent = root();
    DomNodeRef refs[DOM_JS_MUTATION_RECORD_CAP * 4] = {};
    for (int i = 0; i < DOM_JS_MUTATION_RECORD_CAP * 4; i++) {
        DomElement* child = element("held");
        refs[i] = dom_node_ref(child);
        DomNodePinReason reason = (DomNodePinReason)(i % DOM_NODE_PIN_REASON_COUNT);
        ASSERT_TRUE(dom_node_pin(&doc, refs[i], reason));
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    for (int i = 0; i < DOM_JS_MUTATION_RECORD_CAP * 4; i++) {
        DomNodePinReason reason = (DomNodePinReason)(i % DOM_NODE_PIN_REASON_COUNT);
        ASSERT_TRUE(dom_node_unpin(&doc, refs[i], reason));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), DOM_JS_MUTATION_RECORD_CAP * 4u);
}

TEST_F(DomRetirementTest, MutationJournalGrowsPreservingOrderAndReusesStorage) {
    DomElement* parent = root();
    const int count = DOM_JS_MUTATION_RECORD_CAP * 4;
    for (int i = 0; i < count; i++) {
        DomElement* child = element("journal");
        ASSERT_TRUE(attach(parent, child));
        DomNodeRef ref = dom_node_ref(child);
        ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_RECONCILE));
        ASSERT_TRUE(dom_js_mutation_records_reserve(&doc, i + 1));
        DomJsMutationRecord* record = &doc.js.mutation_records[i];
        *record = {};
        record->sequence = i + 1;
        record->target = child;
        record->target_id = ref.expected_id;
        doc.js.mutation_record_count++;
        ASSERT_TRUE(parent->remove_child(child));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    for (int i = 0; i < count; i++) {
        EXPECT_EQ(doc.js.mutation_records[i].sequence, i + 1);
        EXPECT_NE(dom_node_ref_validate(&doc, {doc.js.mutation_records[i].target,
                  doc.js.mutation_records[i].target_id}), nullptr);
    }
    DomJsMutationRecord* retained = doc.js.mutation_records;
    int capacity = doc.js.mutation_record_capacity;
    dom_js_mutation_records_reset(&doc);
    EXPECT_EQ(doc.js.mutation_record_count, 0);
    EXPECT_EQ(doc.js.mutation_record_overflow, 0);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retired_nodes, count);
    ASSERT_TRUE(dom_js_mutation_records_reserve(&doc, count));
    EXPECT_EQ(doc.js.mutation_records, retained);
    EXPECT_EQ(doc.js.mutation_record_capacity, capacity);
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
    Arena* input_arena = arena_create_default();
    ASSERT_NE(input_arena, nullptr);
    Input input = {};
    input.arena = input_arena;
    DomDocument doc;
    ASSERT_TRUE(doc.init(&input));

    DomElement* root = DomElement::create(&doc, "root", nullptr);
    ASSERT_NE(root, nullptr);
    doc.root = lam::up(root);
    DomElement* storage = DomElement::create_in(input_arena);
    ASSERT_NE(storage, nullptr);
    Element* backing = elmt_arena(input_arena);
    ASSERT_NE(backing, nullptr);
    DomElement* child = DomElement::create_in(
        storage, &doc, "child", backing);
    ASSERT_NE(child, nullptr);
    ASSERT_FALSE(child->is_synthetic());
    ASSERT_TRUE(static_cast<DomNode*>(root)->append_child(child));
    ASSERT_TRUE(root->remove_child(child));

    ArenaStats before = {};
    arena_get_stats(input_arena, &before);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    ArenaStats after = {};
    arena_get_stats(input_arena, &after);
    EXPECT_EQ(after.retire_count, before.retire_count + 1u);

    doc.destroy();
    arena_destroy(input_arena);
    pool_destroy(input_pool);
}

TEST(DomRetirementOwnerArenaTest, FlattenedArrayTextIsRegisteredAndRetired) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    MarkBuilder builder(input);
    Item first = {.item = s2it(builder.createDomTextString("first", 5))};
    Item second = {.item = s2it(builder.createDomTextString("second", 6))};
    Item nested = builder.array().append(second).final();
    Item children = builder.array().append(first).append(nested).final();
    Item branch_source = builder.element("branch").child(children).final();
    Item root_source = builder.element("root").child(branch_source).final();
    doc.root = lam::up(build_dom_tree_from_element(root_source.element, &doc, nullptr));
    ASSERT_NE(doc.root, nullptr);
    DomElement* branch = doc.root->first_child->as_element();
    DomNode* first_node = branch->first_child;
    DomNode* second_node = first_node->next_sibling;
    ASSERT_NE(second_node, nullptr);
    DomNodeRef first_ref = dom_node_ref(first_node);
    DomNodeRef second_ref = dom_node_ref(second_node);
    EXPECT_EQ(dom_node_ref_validate(&doc, first_ref), first_node);
    EXPECT_EQ(dom_node_ref_validate(&doc, second_ref), second_node);

    ASSERT_TRUE(static_cast<DomNode*>(doc.root)->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 3u);
    EXPECT_EQ(dom_node_ref_validate(&doc, first_ref), nullptr);
    EXPECT_EQ(dom_node_ref_validate(&doc, second_ref), nullptr);
    doc.destroy();
    pool_destroy(pool);
}

TEST(DomViewCopyTest, RepeatedMarkupOwnsLinksAndBackingAcrossRebuilds) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    doc.element_dom_map = lam::own(element_dom_map_create());
    MarkBuilder builder(input);
    Item leaf = builder.element("span").attr("title", "original").text("shared").final();
    Item nested = builder.array().append(builder.array().append(leaf).final()).final();
    Item branch = builder.element("section").child(nested).final();
    Item source = builder.element("main").child(branch).child(branch).final();
    DomElement* root = build_dom_tree_from_element(source.element, &doc, nullptr);
    ASSERT_NE(root, nullptr);
    doc.root = lam::up(root);
    DomElement* first = root->first_child->as_element();
    ASSERT_NE(first->next_sibling, nullptr);
    DomElement* second = first->next_sibling->as_element();
    EXPECT_NE(first, second);
    EXPECT_EQ(first->parent, root);
    EXPECT_EQ(second->parent, root);
    EXPECT_EQ(second->prev_sibling, first);
    EXPECT_EQ(second->next_sibling, nullptr);
    EXPECT_EQ(root->last_child, second);
    DomElement* first_leaf = first->first_child->as_element();
    DomElement* second_leaf = second->first_child->as_element();
    EXPECT_NE(first_leaf, second_leaf);
    EXPECT_NE(first_leaf->first_child, second_leaf->first_child);
    EXPECT_EQ(first_leaf->first_child->parent, first_leaf);
    EXPECT_EQ(second_leaf->first_child->parent, second_leaf);
    EXPECT_EQ(dom_element_render_source(first_leaf), leaf.element);
    EXPECT_EQ(dom_element_render_source(second_leaf), leaf.element);
    ASSERT_TRUE(first_leaf->set_attribute("title", "changed"));
    EXPECT_STREQ(second_leaf->get_attribute("title"), "original");
    EXPECT_STREQ(leaf.element->get_attr("title").string()->chars, "original");

    // retiring the old occurrence must preserve the new source-to-DOM mapping.
    DomElement* replacement = build_dom_tree_from_element(source.element, &doc, nullptr);
    ASSERT_NE(replacement, nullptr);
    EXPECT_NE(replacement, root);
    doc.root = lam::up(replacement);
    dom_node_schedule_detached(&doc, root);
    EXPECT_EQ(dom_retire_sweep(&doc), 7u);
    EXPECT_EQ(element_dom_map_lookup(doc.element_dom_map, source.element), replacement);
    EXPECT_NE(replacement->first_child->next_sibling, nullptr);
    DomElement* live_leaf = replacement->first_child->as_element()->first_child->as_element();
    ASSERT_TRUE(live_leaf->set_attribute("title", "native edit"));
    Item equal_result = builder.deep_copy_for_view(source);
    dom_rebind_equal_render_result(&doc, source, equal_result);
    EXPECT_EQ(element_dom_map_lookup(doc.element_dom_map, equal_result.element), replacement);
    EXPECT_EQ(element_dom_map_lookup(doc.element_dom_map, source.element), nullptr);
    EXPECT_EQ(dom_element_render_source(replacement), equal_result.element);
    EXPECT_STREQ(live_leaf->get_attribute("title"), "native edit");
    EXPECT_STREQ(leaf.element->get_attr("title").string()->chars, "original");
    doc.destroy();
    pool_destroy(pool);
}

TEST(DomAttributeValueTest, RepeatedValuesReuseInputStorageAndPreserveSnapshots) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    MarkBuilder builder(input);
    Item source = builder.element("div").attr("id", "kept").final();
    auto* element = build_dom_tree_from_element(source.element, &doc, nullptr);
    ASSERT_NE(element, nullptr);
    doc.root = lam::up(element);
    ASSERT_TRUE(element->set_attribute("aria-hidden", "true"));
    const char* saved = element->get_attribute("aria-hidden");
    ASSERT_TRUE(element->set_attribute("aria-hidden", "false"));
    ASSERT_TRUE(element->set_attribute("inert", ""));
    ASSERT_TRUE(element->remove_attribute("inert"));
    size_t warm = arena_total_used(input->arena);
    for (size_t i = 0; i < 256; i++) {
        ASSERT_TRUE(element->set_attribute("aria-hidden", i % 2 ? "true" : "false"));
        ASSERT_TRUE(element->set_attribute("inert", ""));
        ASSERT_TRUE(element->remove_attribute("inert"));
    }
    EXPECT_EQ(arena_total_used(input->arena), warm);
    EXPECT_STREQ(saved, "true");
    EXPECT_STREQ(element->get_attribute("aria-hidden"), "true");
    EXPECT_STREQ(element->get_attribute("id"), "kept");
    doc.destroy();
    pool_destroy(pool);
}

TEST(DomRetirementOwnerArenaTest, SharedUiSourceSurvivesMutationRetirementAndRedisplay) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    MarkBuilder builder(input);
    String* text = builder.createDomTextString("Retained history", 16);
    ASSERT_NE(text, nullptr);
    Item nested = builder.element("span").child(Item{.item = s2it(text)}).final();
    Item source = builder.element("p").attr("title", "original").child(nested).final();
    source.element->cow_state |= COW_STATE_SHARED;
    auto* source_type = (TypeElmt*)source.element->type;
    Target ns = {};
    TypeNominal nominal = {};
    // D2.6.6v2 resolves nominal tags through their declared type name.
    nominal.type_name = source_type->name;
    source_type->ns = &ns;
    source_type->nominal = &nominal;
    source_type->is_nominal = true;
    Item* source_items = source.element->items;
    void* source_data = source.element->data;

    DomElement* root = DomElement::create(&doc, "root", nullptr);
    ASSERT_NE(root, nullptr);
    doc.root = lam::up(root);
    DomElement* occurrence = build_dom_tree_from_element(source.element, &doc, root);
    ASSERT_NE(occurrence, nullptr);
    EXPECT_NE(dom_element_to_element(occurrence), source.element);
    EXPECT_EQ(dom_node_registry_backing_source(&doc, occurrence), source.element);
    auto* occurrence_type = (TypeElmt*)occurrence->elmt.type;
    EXPECT_NE(occurrence_type, source_type);
    EXPECT_EQ(occurrence_type->ns, &ns);
    EXPECT_EQ(occurrence_type->nominal, &nominal);
    ASSERT_TRUE(occurrence->set_attribute("title", "edited"));
    ASSERT_TRUE(occurrence->set_attribute("data-added", "new"));
    DomElement* nested_dom = occurrence->first_child->as_element();
    ASSERT_NE(nested_dom, nullptr);
    EXPECT_NE(dom_element_to_element(nested_dom), nested.element);
    EXPECT_EQ(occurrence->elmt.items[0].element, dom_element_to_element(nested_dom));
    ASSERT_TRUE(nested_dom->set_attribute("data-child-edit", "visible"));
    String* edited_child = ElementReader(occurrence->elmt.items[0].element).get_string_attr("data-child-edit");
    ASSERT_NE(edited_child, nullptr);
    EXPECT_STREQ(edited_child->chars, "visible");
    EXPECT_FALSE(nested.element->has_attr("data-child-edit"));
    ASSERT_TRUE(root->remove_child(occurrence));
    EXPECT_EQ(dom_retire_sweep(&doc), 3u);

    EXPECT_EQ(source.element->type_id, LMD_TYPE_ELEMENT);
    EXPECT_EQ(source.element->type, source_type);
    EXPECT_EQ(source.element->items, source_items);
    EXPECT_EQ(source.element->data, source_data);
    EXPECT_EQ(source.element->length, 1);
    EXPECT_FALSE(source.element->has_attr("data-added"));
    String* original_title = ElementReader(source.element).get_string_attr("title");
    ASSERT_NE(original_title, nullptr);
    EXPECT_STREQ(original_title->chars, "original");
    EXPECT_EQ(nested.element->type_id, LMD_TYPE_ELEMENT);
    EXPECT_EQ(text->len, 16u);
    EXPECT_STREQ(text->chars, "Retained history");

    DomElement* redisplayed = build_dom_tree_from_element(source.element, &doc, root);
    ASSERT_NE(redisplayed, nullptr);
    EXPECT_STREQ(redisplayed->get_attribute("title"), "original");
    ASSERT_NE(redisplayed->first_child, nullptr);
    DomNode* redisplayed_text = redisplayed->first_child->as_element()->first_child;
    ASSERT_NE(redisplayed_text, nullptr);
    EXPECT_STREQ(redisplayed_text->as_text()->text, "Retained history");
    doc.destroy();
    pool_destroy(pool);
}

TEST(DomRetirementOwnerArenaTest, RetiringAnOldOccurrenceKeepsTheCurrentSourceLookup) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    doc.element_dom_map = lam::own(element_dom_map_create());
    ASSERT_NE(doc.element_dom_map, nullptr);
    MarkBuilder builder(input);
    Item source = builder.element("p").text("Retained source").final();
    DomElement* root = DomElement::create(&doc, "root", nullptr);
    ASSERT_NE(root, nullptr);
    doc.root = lam::up(root);
    DomElement* old_occurrence = build_dom_tree_from_element(source.element, &doc, root);
    DomElement* current_occurrence = build_dom_tree_from_element(source.element, &doc, root);
    ASSERT_NE(old_occurrence, nullptr);
    ASSERT_NE(current_occurrence, nullptr);
    ASSERT_NE(old_occurrence, current_occurrence);
    EXPECT_EQ(element_dom_map_lookup(doc.element_dom_map, source.element), current_occurrence);
    ASSERT_TRUE(root->remove_child(old_occurrence));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
    EXPECT_EQ(element_dom_map_lookup(doc.element_dom_map, source.element), current_occurrence);
    ASSERT_TRUE(root->remove_child(current_occurrence));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
    EXPECT_EQ(element_dom_map_lookup(doc.element_dom_map, source.element), nullptr);
    doc.destroy();
    pool_destroy(pool);
}

TEST(DomRetirementOwnerArenaTest, ParsedHtmlRetainsItsMutableElementAndTextStorage) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    doc.page_kind = DOM_PAGE_KIND_HTML;
    MarkBuilder builder(input);
    String* text = builder.createDomTextString("HTML source", 11);
    ASSERT_NE(text, nullptr);
    Item source = builder.element("p").attr("title", "original").child(Item{.item=s2it(text)}).final();
    // normalized content owns the string produced by ElementBuilder's child writer.
    text = source.element->items[0].get_string();
    DomElement* occurrence = build_dom_tree_from_element(source.element, &doc, nullptr);
    ASSERT_NE(occurrence, nullptr);
    doc.root = lam::up(occurrence);
    EXPECT_EQ(dom_element_to_element(occurrence), source.element);
    ASSERT_NE(occurrence->first_child, nullptr);
    EXPECT_EQ(occurrence->first_child, string_to_dom_text(text));
    ASSERT_TRUE(occurrence->set_attribute("title", "DOM edit"));
    EXPECT_STREQ(ElementReader(source.element).get_string_attr("title")->chars, "DOM edit");
    doc.destroy();
    pool_destroy(pool);
}

TEST(DomRetirementOwnerArenaTest, UiNestedArraysRetainValuesAndUseMutableChildOccurrencesWithoutShareMark) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    MarkBuilder builder(input);
    Item child = builder.element("span").attr("title", "source").final();
    Item nested = builder.array().append(child).final();
    Item middle = builder.array().append(nested).final();
    Item outer = builder.array().append(middle).final();
    Item source = builder.element("div").child(outer).final();
    DomElement* occurrence = build_dom_tree_from_element(source.element, &doc, nullptr);
    ASSERT_NE(occurrence, nullptr);
    doc.root = lam::up(occurrence);
    DomElement* child_dom = occurrence->first_child->as_element();
    ASSERT_NE(child_dom, nullptr);
    ASSERT_TRUE(child_dom->set_attribute("title", "edited"));
    Array* copied_outer = occurrence->elmt.items[0].array;
    ASSERT_NE(copied_outer, outer.array);
    Array* copied_middle = copied_outer->items[0].array;
    ASSERT_NE(copied_middle, middle.array);
    Array* copied_nested = copied_middle->items[0].array;
    ASSERT_NE(copied_nested, nested.array);
    EXPECT_EQ(copied_nested->items[0].element, dom_element_to_element(child_dom));
    EXPECT_EQ(nested.array->items[0].element, child.element);
    EXPECT_EQ(source.element->items[0].array, outer.array);
    EXPECT_STREQ(ElementReader(child.element).get_string_attr("title")->chars, "source");
    EXPECT_STREQ(ElementReader(copied_nested->items[0].element).get_string_attr("title")->chars, "edited");
    doc.destroy();
    pool_destroy(pool);
}

class StyleEpochTest : public ::testing::Test {
protected:
    Pool* input_pool = nullptr;
    Input* input = nullptr;
    DomDocument doc;
    CssEngine engine = {};
    CssEngine* stylesheet_engine = nullptr;
    DomElement* document_root = nullptr;

    void SetUp() override {
        input_pool = pool_create();
        ASSERT_NE(input_pool, nullptr);
        input = Input::create(input_pool);
        ASSERT_NE(input, nullptr);
        ASSERT_TRUE(doc.init(input));
        engine.context.viewport_width = 1280.0;
        engine.context.viewport_height = 720.0;
        engine.context.device_pixel_ratio = 2.0;
        document_root = authored_element("root");
        ASSERT_NE(document_root, nullptr);
        doc.root = lam::up(document_root);
    }

    void TearDown() override {
        doc.services.cached_css_engine = nullptr;
        if (stylesheet_engine) css_engine_destroy(stylesheet_engine);
        doc.destroy();
        if (input_pool) pool_destroy(input_pool);
    }

    DomElement* authored_element(const char* tag) {
        // null backing denotes layout-only synthetic boxes, excluded from stylesheet matching.
        MarkBuilder builder(input);
        Item backing = builder.element(tag).final();
        EXPECT_NE(backing.element, nullptr);
        return backing.element ? DomElement::create(&doc, tag, backing.element) : nullptr;
    }

    DomElement* append(const char* tag) {
        DomElement* child = authored_element(tag);
        EXPECT_NE(child, nullptr);
        EXPECT_TRUE(document_root->append_child(child));
        return child;
    }

    void install_stylesheet(const char* css) {
        stylesheet_engine = css_engine_create(doc.document_pool);
        ASSERT_NE(stylesheet_engine, nullptr);
        doc.services.cached_css_engine = stylesheet_engine;
        CssStylesheet* sheet = css_parse_stylesheet(stylesheet_engine, css, nullptr);
        ASSERT_NE(sheet, nullptr);
        doc.stylesheets = lam::own_arr((CssStylesheet**)pool_alloc(
            doc.document_pool, sizeof(CssStylesheet*)));
        ASSERT_NE(doc.stylesheets, nullptr);
        doc.stylesheets[0] = sheet;
        doc.stylesheet_count = 1;
    }

    bool cascade_mutation(DomJsMutationKind kind, DomNode* target, DomNode* parent) {
        // raw DOM edits need the same cache invalidation as the JS notifier.
        doc.mutation_epoch++;
        doc.style_content_epoch++;
        doc.js.mutation_records[0] = {1, kind, target, parent, 0, 0,
            DOM_JS_MUTATION_ATTRIBUTE_UNKNOWN, CSS_PROPERTY_UNKNOWN, true};
        doc.js.mutation_record_count = doc.js.mutation_count = 1;
        return radiant_apply_load_mutation_cascade(&doc, nullptr);
    }

    CssRule* rule(CssPropertyCode property, CssValue* value,
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
    EXPECT_FALSE(canonical->owns_payload);
    EXPECT_NE(canonical->payload_owner, nullptr);
    EXPECT_NE(canonical->value,
        width->data.style_rule.declarations[0]->value);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_GE(stats.hit_count, 1u);
    EXPECT_EQ(stats.bound_element_refs, 3u);
    EXPECT_GT(stats.current_reserved_bytes, 0u);
}

TEST_F(StyleEpochTest, CanonicalRecipesShareFrozenPayloadSnapshots) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);

    ASSERT_TRUE(style_epoch_cascade_begin(&doc, document_root, &engine, false));
    ASSERT_EQ(dom_element_apply_rule(first, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(second, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(second, height, {}), 1);
    style_epoch_cascade_end(&doc);

    CssDeclaration* first_width = dom_element_get_specified_value(
        first, CSS_PROPERTY_WIDTH);
    CssDeclaration* second_width = dom_element_get_specified_value(
        second, CSS_PROPERTY_WIDTH);
    ASSERT_NE(first_width, nullptr);
    ASSERT_NE(second_width, nullptr);
    EXPECT_NE(first->specified_style, second->specified_style);
    EXPECT_EQ(first_width->value, second_width->value);
    EXPECT_EQ(first_width->payload_owner, second_width->payload_owner);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_EQ(stats.current_payload_count, 2u);
    EXPECT_EQ(stats.current_payload_ref_count, 3u);
}

TEST_F(StyleEpochTest, ReplaceModeRebuildsWithoutPriorRecipeBase) {
    DomElement* child = append("child");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);

    ASSERT_TRUE(style_epoch_cascade_begin_replace(&doc, document_root, &engine));
    ASSERT_EQ(dom_element_apply_rule(child, width, {}), 1);
    style_epoch_cascade_end(&doc);
    ASSERT_NE(dom_element_get_specified_value(child, CSS_PROPERTY_WIDTH), nullptr);

    dom_element_clear_cascaded_styles(child);
    ASSERT_TRUE(style_epoch_cascade_begin_replace(&doc, child, &engine));
    ASSERT_EQ(dom_element_apply_rule(child, height, {}), 1);
    style_epoch_cascade_end(&doc);

    EXPECT_EQ(dom_element_get_specified_value(child, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_NE(dom_element_get_specified_value(child, CSS_PROPERTY_HEIGHT), nullptr);
}

TEST_F(StyleEpochTest, SubtreeReplacementPreservesUnrelatedSelectionCascades) {
    DomElement* left = append("left"); DomElement* right = append("right");
    DomElement* leaf = authored_element("leaf");
    ASSERT_TRUE(left->append_child(leaf));
    install_stylesheet("left::selection{color:red}right::selection{color:blue}leaf::selection{color:green}");
    CssRule** rules = doc.stylesheets[0]->rules;
    ASSERT_TRUE(style_epoch_cascade_begin_replace(&doc, document_root, &engine));
    for (size_t index = 0; index < 3; index++) {
        DomElement* hosts[] = {left, right, leaf};
        ASSERT_EQ(dom_element_apply_pseudo_element_rule(hosts[index], rules[index], {}, PSEUDO_ELEMENT_SELECTION), 1);
    }
    style_epoch_cascade_end(&doc);
    CssDeclaration* sibling_color = dom_element_get_pseudo_element_value(right, CSS_PROPERTY_COLOR, PSEUDO_ELEMENT_SELECTION);
    ASSERT_NE(sibling_color, nullptr);
    for (size_t iteration = 0; iteration < 64; iteration++) {
        dom_element_clear_cascaded_styles(left); dom_element_clear_cascaded_styles(leaf);
        ASSERT_TRUE(style_epoch_cascade_begin_replace(&doc, left, &engine));
        EXPECT_EQ(dom_element_get_pseudo_element_value(left, CSS_PROPERTY_COLOR, PSEUDO_ELEMENT_SELECTION), nullptr);
        EXPECT_EQ(dom_element_get_pseudo_element_value(leaf, CSS_PROPERTY_COLOR, PSEUDO_ELEMENT_SELECTION), nullptr);
        EXPECT_EQ(dom_element_get_pseudo_element_value(right, CSS_PROPERTY_COLOR, PSEUDO_ELEMENT_SELECTION), sibling_color);
        ASSERT_EQ(dom_element_apply_pseudo_element_rule(left, rules[0], {}, PSEUDO_ELEMENT_SELECTION), 1);
        ASSERT_EQ(dom_element_apply_pseudo_element_rule(leaf, rules[2], {}, PSEUDO_ELEMENT_SELECTION), 1);
        style_epoch_cascade_end(&doc);
        EXPECT_EQ(dom_element_get_pseudo_element_value(right, CSS_PROPERTY_COLOR, PSEUDO_ELEMENT_SELECTION), sibling_color);
    }
    ASSERT_TRUE(style_epoch_cascade_begin_replace(&doc, document_root, &engine));
    EXPECT_EQ(dom_element_get_pseudo_element_value(right, CSS_PROPERTY_COLOR, PSEUDO_ELEMENT_SELECTION), nullptr);
    style_epoch_cascade_end(&doc);
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
    // warm both a winning declaration and a miss before copy-on-write updates
    ASSERT_NE(style_tree_get_declaration(canonical, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_EQ(style_tree_get_declaration(canonical, CSS_PROPERTY_HEIGHT), nullptr);
    ASSERT_NE(style_tree_get_authored_declaration(canonical, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_EQ(style_tree_get_authored_declaration(canonical, CSS_PROPERTY_HEIGHT), nullptr);

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
    EXPECT_NE(style_tree_get_authored_declaration(first->specified_style, CSS_PROPERTY_HEIGHT), nullptr);
    EXPECT_EQ(style_tree_get_authored_declaration(second->specified_style, CSS_PROPERTY_HEIGHT), nullptr);

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

TEST_F(StyleEpochTest, ConditionEvaluationIsBoundedAndInvalidatesOnEnvironmentChange) {
    const char* media = "(min-width: 1px)";
    const char* supports = "(display: block)";
    CssEngine* conditional_engine = css_engine_create(doc.document_pool);
    ASSERT_NE(conditional_engine, nullptr);
    css_engine_set_viewport(conditional_engine, 1280.0, 720.0);
    ASSERT_TRUE(css_evaluate_media_query(conditional_engine, media));
    ASSERT_TRUE(css_evaluate_supports_condition(conditional_engine, supports));
    uint64_t warm_evaluations = conditional_engine->condition_evaluations;
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);

    for (size_t i = 0; i < 100; i++) {
        EXPECT_TRUE(css_evaluate_media_query(conditional_engine, media));
        EXPECT_TRUE(css_evaluate_supports_condition(conditional_engine, supports));
    }
    PoolStats stable = {};
    pool_get_detailed_stats(doc.document_pool, &stable);
    EXPECT_EQ(conditional_engine->condition_evaluations, warm_evaluations);
    EXPECT_GE(conditional_engine->condition_cache_hits, 200u);
    EXPECT_EQ(stable.live_bytes, warm.live_bytes);

    css_engine_set_viewport(conditional_engine, 0.0, 720.0);
    EXPECT_FALSE(css_evaluate_media_query(conditional_engine, media));
    EXPECT_GT(conditional_engine->condition_evaluations, warm_evaluations);

    PoolStats before_uncached = {};
    pool_get_detailed_stats(doc.document_pool, &before_uncached);
    EXPECT_TRUE(css_evaluate_media_query(conditional_engine, "(min-height: 1px)"));
    EXPECT_TRUE(css_evaluate_supports_condition(conditional_engine, "(color: red)"));
    PoolStats after_uncached = {};
    pool_get_detailed_stats(doc.document_pool, &after_uncached);
    // New keys retain only their bounded engine-owned cache text, not parser
    // scratch or value graphs. Repeating them must add no further storage.
    EXPECT_LE(after_uncached.live_bytes, before_uncached.live_bytes + 4096u);
    EXPECT_TRUE(css_evaluate_media_query(conditional_engine, "(min-height: 1px)"));
    EXPECT_TRUE(css_evaluate_supports_condition(conditional_engine, "(color: red)"));
    PoolStats repeated_uncached = {};
    pool_get_detailed_stats(doc.document_pool, &repeated_uncached);
    EXPECT_EQ(repeated_uncached.live_bytes, after_uncached.live_bytes);
}

TEST_F(StyleEpochTest, RecascadeRetiresStylesheetCustomPropertyRecords) {
    DomElement* child = append("child");
    CssDeclaration* declaration = css_declaration_create(
        CSS_PROPERTY_UNKNOWN, css_value_create_string(doc.document_pool, "blue"),
        {}, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_NE(declaration, nullptr);
    declaration->property_name = pool_strdup(doc.document_pool, "--theme");
    declaration->value_text = "blue";
    declaration->value_text_len = 4;
    CssRule* custom = (CssRule*)pool_calloc(doc.document_pool, sizeof(CssRule));
    ASSERT_NE(custom, nullptr);
    custom->type = CSS_RULE_STYLE;
    custom->pool = doc.document_pool;
    custom->origin = CSS_ORIGIN_AUTHOR;
    custom->data.style_rule.declarations = (CssDeclaration**)pool_alloc(
        doc.document_pool, sizeof(CssDeclaration*));
    ASSERT_NE(custom->data.style_rule.declarations, nullptr);
    custom->data.style_rule.declarations[0] = declaration;
    custom->data.style_rule.declaration_count = 1;

    for (size_t i = 0; i < 32; i++) {
        dom_element_clear_cascaded_styles(child);
        apply(document_root, custom, child);
        size_t records = 0;
        for (CssCustomProp* prop = child->css_variables; prop; prop = prop->next) records++;
        EXPECT_EQ(records, 1u);
        EXPECT_NE(dom_element_lookup_own_custom_property_entry(child, "theme"), nullptr);
    }
}

TEST_F(StyleEpochTest, CustomPropertyLookupRetainsCascadeAndMutationAcrossLargeEnvironments) {
    DomElement* child = append("child");
    StrBuf* css = strbuf_new();
    ASSERT_NE(css, nullptr);
    for (unsigned i = 0; i < 256; i++) strbuf_append_format(css, "--entry-%03u:%upx;", i, i);
    strbuf_append_str(css, "--ENTRY-017:500px;--entry-017:300px!important");
    EXPECT_EQ(dom_element_apply_inline_style(document_root, css->str), 258);
    strbuf_free(css);

    DomElement* owner = nullptr;
    const CssValue* value = dom_element_lookup_custom_property(child, "entry-017", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(owner, document_root);
    EXPECT_DOUBLE_EQ(value->data.length.value, 300.0);
    const CssCustomProp* upper = dom_element_lookup_own_custom_property_entry(document_root, "--ENTRY-017");
    ASSERT_NE(upper, nullptr);
    EXPECT_DOUBLE_EQ(upper->value->data.length.value, 500.0);
    EXPECT_EQ(dom_element_lookup_own_custom_property_entry(document_root, "--Entry-017"), nullptr);
    const CssCustomProp* slice = dom_element_lookup_own_custom_property_entry(document_root, "entry-017suffix", 9);
    ASSERT_NE(slice, nullptr);
    EXPECT_EQ(slice->value.get(), value);

    PoolStats before = {}, after = {};
    pool_get_detailed_stats(doc.document_pool, &before);
    for (unsigned i = 0; i < 100; i++) {
        EXPECT_NE(dom_element_lookup_custom_property(child, "--entry-255", &owner), nullptr);
        EXPECT_EQ(dom_element_lookup_custom_property(child, "--absent", &owner), nullptr);
    }
    pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_EQ(after.live_bytes, before.live_bytes);

    bool changed = false;
    ASSERT_TRUE(dom_element_set_presentation_style(document_root, "--entry-255", "600px", &changed));
    value = dom_element_lookup_custom_property(child, "--entry-255", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_DOUBLE_EQ(value->data.length.value, 600.0);
    const CssCustomProp* authored = dom_element_lookup_own_custom_property_entry(document_root, "entry-255", (size_t)-1, true);
    ASSERT_NE(authored, nullptr);
    // presentation samples are not animation-owned; exclusion must retain them.
    EXPECT_EQ(authored->value.get(), value);
    CssRule* animation = rule(CSS_PROPERTY_UNKNOWN,
        css_value_create_length(doc.document_pool, 700.0, CSS_UNIT_PX));
    CssDeclaration* sample = animation->data.style_rule.declarations[0];
    sample->property_name = pool_strdup(doc.document_pool, "--entry-255");
    sample->value_text = "700px";
    sample->value_text_len = 5;
    ASSERT_TRUE(dom_element_set_animation_custom_property(document_root, sample, &animation, &changed));
    value = dom_element_lookup_custom_property(child, "--entry-255", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_DOUBLE_EQ(value->data.length.value, 700.0);
    authored = dom_element_lookup_own_custom_property_entry(document_root, "entry-255", (size_t)-1, true);
    ASSERT_NE(authored, nullptr);
    EXPECT_DOUBLE_EQ(authored->value->data.length.value, 600.0);
    ASSERT_TRUE(dom_element_clear_animation_custom_properties(document_root, &animation));
    ASSERT_TRUE(dom_element_clear_presentation_style(document_root));
    value = dom_element_lookup_custom_property(child, "--entry-255", &owner);
    ASSERT_NE(value, nullptr);
    EXPECT_DOUBLE_EQ(value->data.length.value, 255.0);
    ASSERT_TRUE(dom_element_remove_inline_styles(document_root));
    EXPECT_EQ(dom_element_lookup_custom_property(child, "entry-017", &owner), nullptr);
}

TEST_F(StyleEpochTest, OverlappingMutationRootsCascadeTheFinalTreeOnce) {
    DomElement* child = append("child");
    DomElement* grandchild = authored_element("grandchild");
    ASSERT_NE(grandchild, nullptr);
    ASSERT_TRUE(child->append_child(grandchild));
    install_stylesheet("* { width: 10px; }");
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_ATTRIBUTE, child, document_root));
    StyleEpochStats first = {};
    style_epoch_get_stats(&doc, &first);
    ASSERT_GT(first.lookup_count, 0u);

    for (int i = 0; i < 16; i++) {
        // attribute roots overlap independently of child-list selector dependencies.
        doc.js.mutation_records[i] = {static_cast<uint32_t>(i + 1),
            DOM_JS_MUTATION_ATTRIBUTE, i % 2 ? child : grandchild,
            i % 2 ? document_root : child, 0, 0,
            DOM_JS_MUTATION_ATTRIBUTE_UNKNOWN, CSS_PROPERTY_UNKNOWN, true};
    }
    doc.js.mutation_record_count = doc.js.mutation_count = 16;
    ASSERT_TRUE(radiant_apply_load_mutation_cascade(&doc, nullptr));
    StyleEpochStats repeated = {};
    style_epoch_get_stats(&doc, &repeated);
    EXPECT_EQ(repeated.lookup_count - first.lookup_count, first.lookup_count);
    CssDeclaration* width = dom_element_get_specified_value(grandchild, CSS_PROPERTY_WIDTH);
    ASSERT_NE(width, nullptr);
    EXPECT_DOUBLE_EQ(width->value->data.length.value, 10.0);
}

TEST_F(StyleEpochTest, ChildListOutsideStructuralAnchorsPreservesExistingCascade) {
    DomElement* sibling = append("sibling");
    install_stylesheet("* { width: 10px; } .component > :not(:last-child) { width: 20px; } "
        ".component > * + * { height: 25px; } .component:empty { height: 30px; }");
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_ATTRIBUTE, document_root, nullptr));
    StyleTree* retained_style = sibling->specified_style;
    StyleEpochStats before = {};
    style_epoch_get_stats(&doc, &before);

    DomElement* inserted = append("inserted");
    DomElement* nested = authored_element("nested");
    ASSERT_NE(nested, nullptr);
    ASSERT_TRUE(inserted->append_child(nested));
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_CHILD_INSERT, inserted, document_root));
    StyleEpochStats after_insert = {};
    style_epoch_get_stats(&doc, &after_insert);
    EXPECT_EQ(after_insert.lookup_count - before.lookup_count, 2u);
    EXPECT_EQ(sibling->specified_style, retained_style);
    CssDeclaration* width = dom_element_get_specified_value(nested, CSS_PROPERTY_WIDTH);
    ASSERT_NE(width, nullptr);
    EXPECT_DOUBLE_EQ(width->value->data.length.value, 10.0);

    ASSERT_TRUE(document_root->remove_child(inserted));
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_CHILD_REMOVE, inserted, document_root));
    StyleEpochStats after_remove = {};
    style_epoch_get_stats(&doc, &after_remove);
    EXPECT_EQ(after_remove.lookup_count, after_insert.lookup_count);
    EXPECT_EQ(sibling->specified_style, retained_style);
}

TEST_F(StyleEpochTest, PositionAndEmptySelectorsRecascadeChangedParent) {
    DomElement* list = append("ul");
    install_stylesheet("li { width: 10px; } li:nth-child(2) { width: 20px; } "
        "ul:empty { height: 30px; }");
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_ATTRIBUTE, document_root, nullptr));
    ASSERT_NE(dom_element_get_specified_value(list, CSS_PROPERTY_HEIGHT), nullptr);

    DomElement* first = authored_element("li");
    DomElement* second = authored_element("li");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(list->append_child(first));
    ASSERT_TRUE(list->append_child(second));
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_CHILD_INSERT, second, list));
    EXPECT_EQ(dom_element_get_specified_value(list, CSS_PROPERTY_HEIGHT), nullptr);
    CssDeclaration* second_width = dom_element_get_specified_value(second, CSS_PROPERTY_WIDTH);
    ASSERT_NE(second_width, nullptr);
    EXPECT_DOUBLE_EQ(second_width->value->data.length.value, 20.0);

    ASSERT_TRUE(list->remove_child(first));
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_CHILD_REMOVE, first, list));
    second_width = dom_element_get_specified_value(second, CSS_PROPERTY_WIDTH);
    ASSERT_NE(second_width, nullptr);
    EXPECT_DOUBLE_EQ(second_width->value->data.length.value, 10.0);
    ASSERT_TRUE(list->remove_child(second));
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_CHILD_REMOVE, second, list));
    EXPECT_NE(dom_element_get_specified_value(list, CSS_PROPERTY_HEIGHT), nullptr);
}

TEST_F(StyleEpochTest, SiblingRemovalRecascadesWhenPreviousAnchorIsDetached) {
    DomElement* parent = append("div");
    ASSERT_TRUE(parent->set_attribute("class", "component"));
    DomElement* before = authored_element("div");
    DomElement* after = authored_element("div");
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    ASSERT_TRUE(before->set_attribute("class", "before"));
    ASSERT_TRUE(after->set_attribute("class", "after"));
    ASSERT_TRUE(parent->append_child(before));
    ASSERT_TRUE(parent->append_child(after));
    install_stylesheet("* { width: 10px; } .component > .before + .after { width: 20px; }");
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_ATTRIBUTE, document_root, nullptr));
    CssDeclaration* width = dom_element_get_specified_value(after, CSS_PROPERTY_WIDTH);
    ASSERT_NE(width, nullptr);
    EXPECT_DOUBLE_EQ(width->value->data.length.value, 20.0);
    ASSERT_TRUE(parent->remove_child(before));
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_CHILD_REMOVE, before, parent));
    width = dom_element_get_specified_value(after, CSS_PROPERTY_WIDTH);
    ASSERT_NE(width, nullptr);
    EXPECT_DOUBLE_EQ(width->value->data.length.value, 10.0);
}

TEST_F(StyleEpochTest, RecascadeReclaimsExclusiveOwnedStyleTrees) {
    DomElement* child = append("child");
    CssRule* calc = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX));

    // No epoch batch makes this an exclusive, element-owned cascade tree.
    dom_element_apply_rule(child, calc, {});
    PoolStats first = {};
    pool_get_detailed_stats(doc.document_pool, &first);
    for (size_t i = 0; i < 32; i++) {
        dom_element_clear_cascaded_styles(child);
        ASSERT_EQ(dom_element_apply_rule(child, calc, {}), 1);
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_LE(repeated.live_bytes, first.live_bytes + 4096u);
}

TEST_F(StyleEpochTest, RecascadeWithInlineStateReclaimsRemovedCascadeRecords) {
    DomElement* child = append("child");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX));
    CssSpecificity specificity = {};
    specificity.inline_style = 1;
    CssDeclaration* height = css_declaration_create(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 25.0, CSS_UNIT_PX),
        specificity, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_TRUE(dom_element_apply_declaration(child, height));
    ASSERT_EQ(dom_element_apply_rule(child, width, {}), 1);
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    for (size_t i = 0; i < 128; i++) {
        dom_element_clear_cascaded_styles(child);
        ASSERT_EQ(dom_element_apply_rule(child, width, {}), 1);
        ASSERT_EQ(dom_element_get_specified_value(child, CSS_PROPERTY_HEIGHT), height);
    }
    PoolStats stable = {};
    pool_get_detailed_stats(doc.document_pool, &stable);
    EXPECT_LE(stable.live_bytes, warm.live_bytes + 4096u);
}

TEST_F(StyleEpochTest, MotionInitialValuesDoNotGrowRetainedCallerStorage) {
    DomElement* child = append("child");
    Pool* view_pool = pool_create();
    ASSERT_NE(view_pool, nullptr);
    CssTransitionList first = {};
    css_transition_resolve_config(child, view_pool, &first);
    PoolStats warm = {};
    pool_get_detailed_stats(view_pool, &warm);
    for (size_t i = 0; i < 128; i++) {
        CssTransitionList sampled = {};
        css_transition_resolve_config(child, view_pool, &sampled);
        for (size_t property = 0; property < 4; property++)
            EXPECT_EQ(sampled.values[property], first.values[property]);
    }
    PoolStats stable = {};
    pool_get_detailed_stats(view_pool, &stable);
    EXPECT_EQ(stable.live_bytes, warm.live_bytes);
    pool_destroy(view_pool);
}

TEST_F(StyleEpochTest, ReplacingInlineValuesReclaimsParserPayloads) {
    DomElement* child = append("child");
    const char* styles[] = {
        "transform:translate(1px,2px);opacity:0.5;color:blue;line-height:1.4;--accent:red",
        "transform:translate(3px,4px);opacity:0.8;color:green;line-height:1.6;--accent:blue",
        "transform:none;opacity:1;color:red;line-height:normal;--accent:green"
    };
    for (const char* style : styles) ASSERT_TRUE(child->set_attribute("style", style));
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    // Replacement must retire both ordinary declarations and custom values.
    for (size_t i = 0; i < 192; i++) {
        ASSERT_TRUE(child->set_attribute("style", styles[i % 3]));
        CssDeclaration* transform = dom_element_get_specified_value(child,
            CSS_PROPERTY_TRANSFORM);
        ASSERT_NE(transform, nullptr);
        EXPECT_TRUE(transform->owns_payload);
        ASSERT_NE(child->css_variables, nullptr);
        ASSERT_NE(child->css_variables->declaration, nullptr);
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_LE(repeated.live_bytes, warm.live_bytes + 4096u);
    ASSERT_TRUE(child->remove_attribute("style"));
    EXPECT_EQ(dom_element_get_specified_value(child, CSS_PROPERTY_TRANSFORM), nullptr);
    EXPECT_EQ(child->css_variables, nullptr);
}

TEST_F(StyleEpochTest, RejectedRuleDoesNotRetainCascadeCopies) {
    DomElement* child = append("child");
    CssRule* invalid = rule(CSS_PROPERTY_WIDTH,
        css_value_create_string(doc.document_pool, "invalid-width"));
    ASSERT_EQ(dom_element_apply_rule(child, invalid, {}), 0);
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    for (size_t i = 0; i < 128; i++)
        ASSERT_EQ(dom_element_apply_rule(child, invalid, {}), 0);
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
}

TEST_F(StyleEpochTest, OwnedDeclarationRetainsKeywordSpelling) {
    CssValue* value = css_value_create_keyword(doc.document_pool, "auto");
    ASSERT_NE(value, nullptr);
    char* spelling = pool_strdup(doc.document_pool, "auto");
    ASSERT_NE(spelling, nullptr);
    value->has_keyword_spelling = true;
    value->data.keyword_token.spelling = spelling;
    CssDeclaration* source = css_declaration_create(CSS_PROPERTY_WIDTH, value,
        {}, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_NE(source, nullptr);
    CssDeclaration* copy = css_declaration_clone_owned(source, {},
        CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->value->data.keyword_token.spelling, spelling);
    // Retiring or reusing parser bytes cannot change a published owned value.
    spelling[0] = 'x';
    EXPECT_STREQ(copy->value->data.keyword_token.spelling, "auto");
    css_declaration_destroy_owned(copy, doc.document_pool);
}

TEST_F(StyleEpochTest, PendingGridProjectionRetainsGrammarAfterSourcePoolRetirement) {
    struct Case {const char* declaration; CssPropertyCode shorthand, member; const char* resolved; const char* expected;};
    const Case cases[] = {
        {"grid-area:var(--definition)", CSS_PROPERTY_GRID_AREA, CSS_PROPERTY_GRID_ROW_END, "1 / Foo / 3 / Bar", "3"},
        {"grid-template:var(--definition)", CSS_PROPERTY_GRID_TEMPLATE, CSS_PROPERTY_GRID_TEMPLATE_COLUMNS,
            "\"a b\" 10px / 20px 30px", "20px 30px"},
        {"grid:var(--definition)", CSS_PROPERTY_GRID, CSS_PROPERTY_GRID_AUTO_COLUMNS,
            "10px / auto-flow dense 30px 40px", "30px 40px"}
    };
    for (const Case& item : cases) {
        SCOPED_TRACE(item.declaration);
        Pool* source_pool = pool_create(); ASSERT_NE(source_pool, nullptr);
        CssDeclaration* source = css_parse_declaration_text(item.declaration, strlen(item.declaration), source_pool);
        ASSERT_NE(source, nullptr);
        source->property_code = item.member;
        source->pending_shorthand_code = item.shorthand;
        source->pending_identity_shorthand = true;
        CssDeclaration* copy = css_declaration_clone_owned(source, {}, CSS_ORIGIN_AUTHOR, doc.document_pool);
        ASSERT_NE(copy, nullptr);
        pool_destroy(source_pool);
        EXPECT_EQ(copy->pending_shorthand_code, item.shorthand);
        EXPECT_TRUE(copy->pending_identity_shorthand);
        EXPECT_TRUE(css_value_contains_pending_substitution(copy->value));
        EXPECT_STREQ(copy->value_text, "var(--definition)");
        CssValue* resolved = css_parse_component_value_text(item.resolved, strlen(item.resolved), doc.document_pool);
        ASSERT_NE(resolved, nullptr);
        const CssValue* projected = css_grid_shorthand_longhand(doc.document_pool, resolved,
            copy->pending_shorthand_code, copy->property_code);
        ASSERT_NE(projected, nullptr);
        CssFormatter* formatter = css_formatter_create(doc.document_pool, CSS_FORMAT_COMPACT);
        ASSERT_NE(formatter, nullptr);
        css_format_value_with_property(formatter, const_cast<CssValue*>(projected), item.member);
        EXPECT_STREQ(stringbuf_to_string(formatter->output)->chars, item.expected);
        css_declaration_destroy_owned(copy, doc.document_pool);
    }
}

TEST_F(StyleEpochTest, GridComputedReadsDoNotRetainProjectionScratch) {
    // component cascade reads require the same property metadata initialized by a document loader.
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* child = append("child");
    struct Case {CssPropertyCode property; const char* expected;};
    const Case cases[] = {
        {CSS_PROPERTY_GRID_TEMPLATE_COLUMNS, "repeat(2, [Em] 20px [tail])"},
        {CSS_PROPERTY_GRID_TEMPLATE, "10px / repeat(2, [Em] 20px [tail])"},
        {CSS_PROPERTY_GRID, "10px / repeat(2, [Em] 20px [tail]) / none / row / auto / auto"},
        {CSS_PROPERTY_GRID_COLUMN_START, "2"},
        {CSS_PROPERTY_GRID_COLUMN_END, "span 2"},
        {CSS_PROPERTY_GRID_COLUMN, "2 / span 2"}
    };
    auto read = [&]() {
        Pool* caller = pool_create();
        for (const Case& item : cases) {
            String* value = css_prop_serialize_computed_value(caller, child, item.property, 0);
            EXPECT_NE(value, nullptr);
            if (value) EXPECT_STREQ(value->chars, item.expected);
        }
        pool_destroy(caller);
    };
    const char* counts[] = {"2", "calc(20px / 1em)", "calc(2 * (3 - 2))"};
    for (const char* count : counts) {
        SCOPED_TRACE(count);
        char style[256];
        snprintf(style,sizeof(style),"font-size:10px;grid-template:1em / repeat(%s,[Em] 2em [tail]);grid-column:calc(20px / 1em) / span calc(1.6)",count);
        ASSERT_TRUE(child->set_attribute("style", style));
        ASSERT_NE(dom_element_get_specified_value(child,CSS_PROPERTY_GRID_TEMPLATE),nullptr);
        read();
        PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
        for (size_t i = 0; i < 256; i++) read();
        PoolStats repeated = {}; pool_get_detailed_stats(doc.document_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    }
}

TEST_F(StyleEpochTest, FocusRenderingUsesLiveDisplayAncestryAndReleasesScratch) {
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* parent = append("div");
    DomElement* child = authored_element("button");
    ASSERT_NE(child, nullptr); ASSERT_TRUE(parent->append_child(child));
    const struct Case {
        const char* parent_style;
        const char* child_style;
        bool parent_hidden;
        bool child_hidden;
        bool excluded;
    } cases[] = {
        {"", "", false, false, false},
        {"", "display:none", false, false, true},
        {"display:none", "display:block", false, false, true},
        {"display:block", "display:block", false, false, false},
        {"", "visibility:hidden", false, false, true},
        {"", "visibility:collapse", false, false, true},
        {"visibility:hidden", "", false, false, true},
        {"visibility:hidden", "visibility:visible", false, false, false},
        {"", "", false, true, true},
        {"", "display:block", false, true, false},
        {"", "", true, false, true},
        {"display:block", "", true, false, false},
        {"display:contents", "display:contents", false, false, false},
        {"", "opacity:0;width:0;height:0", false, false, false},
        {"", "--box:none;display:var(--box)", false, false, true},
        {"", "--box:block;display:var(--box)", false, true, false},
    };
    for (const Case& item : cases) {
        SCOPED_TRACE(item.parent_style);
        SCOPED_TRACE(item.child_style);
        ASSERT_TRUE(parent->set_attribute("style", item.parent_style));
        ASSERT_TRUE(child->set_attribute("style", item.child_style));
        if (item.parent_hidden) ASSERT_TRUE(parent->set_attribute("hidden", ""));
        else parent->remove_attribute("hidden");
        if (item.child_hidden) ASSERT_TRUE(child->set_attribute("hidden", ""));
        else child->remove_attribute("hidden");
        auto read = [&]() {
            EXPECT_EQ(view_focus_is_hidden(static_cast<View*>(child)), item.excluded);
        };
        read();
        PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
        for (size_t index = 0; index < 256; index++) read();
        PoolStats repeated = {}; pool_get_detailed_stats(doc.document_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    }
}

TEST_F(StyleEpochTest, LineClampComputedCountsUseDeclarationOwnersWithoutRetainedScratch) {
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* parent = append("parent");
    DomElement* child = DomElement::create(&doc, "child", nullptr);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(parent->append_child(child));
    ASSERT_TRUE(parent->set_attribute("style", "font-size:10px;-webkit-line-clamp:calc(20px / 1em)"));
    const struct Case { const char* style; const char* expected; } cases[] = {
        {"font-size:5px;-webkit-line-clamp:inherit", "2"},
        {"font-size:10px;-webkit-line-clamp:calc(20px / 1em)", "2"},
        {"font-size:20px;-webkit-line-clamp:calc(20px / 1em)", "1"},
        {"-webkit-line-clamp:calc(-1)", "1"},
        {"-webkit-line-clamp:calc(1.5)", "2"},
        {"-webkit-line-clamp:4;-webkit-line-clamp:2.0", "4"},
        {"--count:1px;-webkit-line-clamp:4;-webkit-line-clamp:var(--count)", "none"},
        {"-webkit-line-clamp:initial", "none"},
        {"-webkit-line-clamp:unset", "none"},
        {"all:initial", "none"},
        {"all:inherit", "2"},
        {"", "none"}
    };
    for (const Case& item : cases) {
        SCOPED_TRACE(item.style);
        ASSERT_TRUE(child->set_attribute("style", item.style));
        auto read = [&]() {
            Pool* caller = pool_create();
            String* value = css_prop_serialize_computed_value(caller, child, CSS_PROPERTY_WEBKIT_LINE_CLAMP, 0);
            EXPECT_NE(value, nullptr);
            if (value) EXPECT_STREQ(value->chars, item.expected);
            pool_destroy(caller);
        };
        read();
        PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
        for (size_t i = 0; i < 256; i++) read();
        PoolStats repeated = {}; pool_get_detailed_stats(doc.document_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    }
}

TEST_F(StyleEpochTest, LegacyBoxComputedReadsUseLiveDeclarationsAndReleaseScratch) {
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    const CssProperty* orientation = css_property_get_by_name("-webkit-box-orient");
    ASSERT_NE(orientation, nullptr);
    DomElement* parent = append("parent");
    DomElement* child = DomElement::create(&doc, "child", nullptr);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(parent->append_child(child));
    ASSERT_TRUE(parent->set_attribute("style", "-webkit-box-orient:vertical"));
    const struct Case { const char* style; const char* axis; const char* display; } cases[] = {
        {"display:-webkit-box", "horizontal", "-webkit-box"},
        {"display:-webkit-box;-webkit-box-orient:vertical;-webkit-line-clamp:2", "vertical", "flow-root"},
        {"display:-webkit-inline-box;-webkit-box-orient:vertical;-webkit-line-clamp:2", "vertical", "inline-block"},
        {"display:-webkit-box;-webkit-box-orient:inline-axis;-webkit-line-clamp:2", "horizontal", "-webkit-box"},
        {"display:-webkit-box;-webkit-box-orient:block-axis;-webkit-line-clamp:2", "vertical", "flow-root"},
        {"display:-webkit-box;-webkit-box-orient:inherit;-webkit-line-clamp:2", "vertical", "flow-root"},
        {"display:-webkit-box;-webkit-box-orient:unset;-webkit-line-clamp:2", "horizontal", "-webkit-box"},
        {"display:-webkit-box;--axis:block-axis;-webkit-box-orient:var(--axis);-webkit-line-clamp:2", "vertical", "flow-root"},
        {"display:-webkit-box;--axis:invalid;-webkit-box-orient:var(--axis);-webkit-line-clamp:2", "horizontal", "-webkit-box"},
        {"display:flow-root;-webkit-box-orient:vertical;-webkit-line-clamp:2", "vertical", "flow-root"}
    };
    for (const Case& item : cases) {
        SCOPED_TRACE(item.style); ASSERT_TRUE(child->set_attribute("style", item.style));
        auto read = [&]() {
            Pool* caller = pool_create();
            String* axis = css_prop_serialize_computed_value(caller, child, orientation->code, 0);
            String* display = css_prop_serialize_computed_value(caller, child, CSS_PROPERTY_DISPLAY, 0);
            EXPECT_NE(axis, nullptr); EXPECT_NE(display, nullptr);
            if (axis) EXPECT_STREQ(axis->chars, item.axis);
            if (display) EXPECT_STREQ(display->chars, item.display);
            pool_destroy(caller);
        };
        read(); PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
        for (size_t i = 0; i < 256; i++) read();
        PoolStats repeated = {}; pool_get_detailed_stats(doc.document_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    }
}

TEST_F(StyleEpochTest, TextTransformComputedReadsFollowInheritanceAndReleaseScratch) {
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* parent = append("parent");
    DomElement* child = DomElement::create(&doc, "child", nullptr);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(parent->append_child(child));
    ASSERT_TRUE(parent->set_attribute("style", "text-transform:uppercase;word-spacing:10px"));
    const struct Case { const char* style; const char* expected; CssPropertyCode property = CSS_PROPERTY_TEXT_TRANSFORM; } cases[] = {
        {"", "uppercase"},
        {"text-transform:capitalize", "capitalize"},
        {"text-transform:lowercase", "lowercase"},
        {"text-transform:none", "none"},
        {"text-transform:inherit", "uppercase"},
        {"text-transform:unset", "uppercase"},
        {"text-transform:initial", "none"},
        {"--case:capitalize;text-transform:var(--case)", "capitalize"},
        {"--case:invalid;text-transform:none;text-transform:var(--case)", "uppercase"},
        {"text-transform:lowercase;text-transform:invalid", "lowercase"},
        {"text-transform:uppercase full-width", "uppercase full-width"},
        {"text-transform:full-size-kana", "full-size-kana"},
        {"text-transform:math-auto", "math-auto"},
        {"text-transform:lowercase;text-transform:none uppercase", "lowercase"},
        {"text-transform:lowercase;text-transform:uppercase lowercase", "lowercase"},
        {"text-transform:lowercase;text-transform:uppercase uppercase", "lowercase"},
        {"", "10px", CSS_PROPERTY_WORD_SPACING},
        {"word-spacing:normal", "0px", CSS_PROPERTY_WORD_SPACING},
        {"word-spacing:initial", "0px", CSS_PROPERTY_WORD_SPACING},
        {"word-spacing:inherit", "10px", CSS_PROPERTY_WORD_SPACING},
        {"word-spacing:unset", "10px", CSS_PROPERTY_WORD_SPACING},
        {"word-spacing:-5px", "-5px", CSS_PROPERTY_WORD_SPACING},
        {"--gap:initial;word-spacing:var(--gap)", "10px", CSS_PROPERTY_WORD_SPACING},
        {"word-spacing:var(--missing,initial)", "0px", CSS_PROPERTY_WORD_SPACING},
        {"--gap:normal;word-spacing:var(--gap)", "0px", CSS_PROPERTY_WORD_SPACING}
    };
    for (const Case& item : cases) {
        SCOPED_TRACE(item.style); ASSERT_TRUE(child->set_attribute("style", item.style));
        auto read = [&]() {
            Pool* caller = pool_create();
            String* value = css_prop_serialize_computed_value(caller, child, item.property, 0);
            EXPECT_NE(value, nullptr);
            if (value) EXPECT_STREQ(value->chars, item.expected);
            pool_destroy(caller);
        };
        read(); PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
        for (size_t index = 0; index < 256; index++) read();
        PoolStats repeated = {}; pool_get_detailed_stats(doc.document_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    }
}

TEST_F(StyleEpochTest, BackgroundComputedComponentsPreserveOwnersIndependentPeriodsAndDefaults) {
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* parent = append("parent");
    DomElement* child = DomElement::create(&doc, "child", nullptr);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(parent->append_child(child));
    ASSERT_TRUE(parent->set_attribute("style", "font-size:4px;background:none;background-size:2em 1em,3em 4em"));
    const struct Case { const char* style; CssPropertyCode property; const char* expected; } cases[] = {
        {"background:linear-gradient(red,red) right 3px bottom 5px/8px 4px no-repeat blue", CSS_PROPERTY_BACKGROUND_SIZE, "8px 4px"},
        {"background:linear-gradient(red,red) right 3px bottom 5px/8px 4px no-repeat blue", CSS_PROPERTY_BACKGROUND_POSITION, "calc(100% - 3px) calc(100% - 5px)"},
        {"font-size:4px;background-image:none,none;background-size:2em 1em,min(50%,2em) max(10%,1em)", CSS_PROPERTY_BACKGROUND_SIZE, "8px 4px, min(50%, 8px) max(10%, 4px)"},
        {"font-size:4px;background-position:right 1em bottom max(25%,1em)", CSS_PROPERTY_BACKGROUND_POSITION, "calc(100% - 4px) calc(100% - max(25%, 4px))"},
        {"background-image:none,none,none,none;background-position-x:3px,13px;background-position-y:5px,19px,25px", CSS_PROPERTY_BACKGROUND_POSITION, "3px 5px, 13px 19px, 3px 25px, 13px 5px"},
        // inheritance preserves unused original periods rather than the parent's image-count projection (CI-18).
        {"font-size:30px;background-image:none,none,none,none;background-size:inherit", CSS_PROPERTY_BACKGROUND_SIZE, "8px 4px, 12px 16px, 8px 4px, 12px 16px"},
        {"background:linear-gradient(red,red) right bottom/8px 4px no-repeat;all:initial", CSS_PROPERTY_BACKGROUND_POSITION, "0% 0%"},
        {"--repeat:no-repeat none;background-repeat:var(--repeat,no-repeat)", CSS_PROPERTY_BACKGROUND_REPEAT, "repeat"},
        {"background-blend-mode:multiply;background:none", CSS_PROPERTY_BACKGROUND_BLEND_MODE, "multiply"},
        {"background-repeat:no-repeat;background-repeat:repeat-x repeat", CSS_PROPERTY_BACKGROUND_REPEAT, "no-repeat"},
        {"background-position-x:right 3px !important;background:left bottom", CSS_PROPERTY_BACKGROUND_POSITION, "calc(100% - 3px) 100%"},
        {"background-position:calc(0% + 3px) calc(100% + 0px)", CSS_PROPERTY_BACKGROUND_POSITION, "calc(0% + 3px) 100%"},
        {"background:linear-gradient(red,red) text", CSS_PROPERTY_BACKGROUND_ORIGIN, "padding-box"},
        {"background:linear-gradient(red,red) text", CSS_PROPERTY_BACKGROUND_CLIP, "text"}
    };
    for (const Case& item : cases) {
        SCOPED_TRACE(item.style); ASSERT_TRUE(child->set_attribute("style", item.style));
        auto read = [&]() {
            Pool* caller = pool_create();
            String* value = css_prop_serialize_computed_value(caller, child, item.property, 0);
            EXPECT_NE(value, nullptr);
            if (value) EXPECT_STREQ(value->chars, item.expected);
            pool_destroy(caller);
        };
        read(); PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
        for (size_t index = 0; index < 256; index++) read();
        PoolStats repeated = {}; pool_get_detailed_stats(doc.document_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    }
    ASSERT_TRUE(child->set_attribute("style", "font-size:30px;background-image:none,none;background-size:inherit"));
    ASSERT_TRUE(parent->set_attribute("style", "font-size:8px;background:none;background-size:2em 1em"));
    Pool* caller = pool_create();
    String* value = css_prop_serialize_computed_value(caller, child, CSS_PROPERTY_BACKGROUND_SIZE, 0);
    ASSERT_NE(value, nullptr); EXPECT_STREQ(value->chars, "16px 8px, 16px 8px");
    pool_destroy(caller);
}

TEST_F(StyleEpochTest, BackgroundComputedPseudosKeepOriginatingFontsAndInheritedOwnerLengths) {
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* host = append("host");
    ASSERT_TRUE(host->set_attribute("style", "font-size:4px;background-size:2em 1em"));
    install_stylesheet("host::before{content:'';font-size:6px;background:linear-gradient(red,red) right 1em bottom 2em/2em 1em no-repeat,none}"
        "host::after{content:'';font-size:30px;background-image:none,none;background-size:inherit}");
    ASSERT_TRUE(cascade_mutation(DOM_JS_MUTATION_ATTRIBUTE, document_root, nullptr));
    const struct Case { int pseudo; CssPropertyCode property; const char* expected; } cases[] = {
        {1, CSS_PROPERTY_BACKGROUND_SIZE, "12px 6px, auto"},
        {1, CSS_PROPERTY_BACKGROUND_POSITION, "calc(100% - 6px) calc(100% - 12px), 0% 0%"},
        {2, CSS_PROPERTY_BACKGROUND_SIZE, "8px 4px, 8px 4px"}
    };
    for (const Case& item : cases) {
        auto read = [&]() {
            Pool* caller = pool_create();
            String* value = css_prop_serialize_computed_value(caller, host, item.property, item.pseudo);
            EXPECT_NE(value, nullptr); if (value) EXPECT_STREQ(value->chars, item.expected);
            pool_destroy(caller);
        };
        read(); PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
        for (size_t index = 0; index < 256; index++) read();
        PoolStats repeated = {}; pool_get_detailed_stats(doc.document_pool, &repeated);
        EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    }
}

TEST_F(StyleEpochTest, BackgroundComputedListsKeepFullLengthAndCallerOwnership) {
    ASSERT_TRUE(css_property_system_init(doc.document_pool));
    DomElement* child = append("child");
    Pool* caller = pool_create();
    StringBuf* style = stringbuf_new(caller); StringBuf* expected = stringbuf_new(caller);
    stringbuf_append_str(style, "font-size:4px;--size:2em 1em;background-size:var(--size);background-image:");
    for (size_t index = 0; index < 128; index++) {
        if (index) { stringbuf_append_str(style, ","); stringbuf_append_str(expected, ", "); }
        stringbuf_append_str(style, "none"); stringbuf_append_str(expected, "8px 4px");
    }
    ASSERT_TRUE(child->set_attribute("style", stringbuf_to_string(style)->chars));
    String* value = css_prop_serialize_computed_value(caller, child, CSS_PROPERTY_BACKGROUND_SIZE, 0);
    ASSERT_NE(value, nullptr); ASSERT_GT(value->len, 511u);
    // to_string transfers the buffer; retain that caller-owned string across declaration replacement.
    String* expected_text = stringbuf_to_string(expected);
    ASSERT_NE(expected_text, nullptr); EXPECT_STREQ(value->chars, expected_text->chars);
    char bounded[512];
    EXPECT_FALSE(css_prop_serialize_computed(child, CSS_PROPERTY_BACKGROUND_SIZE, 0, bounded, sizeof(bounded)));
    EXPECT_STREQ(bounded, "");
    PoolStats warm = {}; pool_get_detailed_stats(doc.document_pool, &warm);
    for (size_t index = 0; index < 256; index++) {
        Pool* projection = pool_create();
        String* repeated = css_prop_serialize_computed_value(projection, child, CSS_PROPERTY_BACKGROUND_SIZE, 0);
        ASSERT_NE(repeated, nullptr); EXPECT_STREQ(repeated->chars, value->chars);
        pool_destroy(projection);
    }
    PoolStats after = {}; pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_EQ(after.live_bytes, warm.live_bytes);
    ASSERT_TRUE(child->set_attribute("style", "background:none;background-size:cover"));
    EXPECT_STREQ(value->chars, expected_text->chars);
    pool_destroy(caller);
}

TEST_F(StyleEpochTest, PresentationLayerPreservesAuthoredStateAndCascade) {
    DomElement* child = append("child");
    const char* authored = "opacity:0.3;color:red !important";
    ASSERT_TRUE(child->set_attribute("style", authored));
    bool changed = false;
    ASSERT_TRUE(dom_element_set_presentation_style(child, "opacity", "0.8", &changed));
    EXPECT_TRUE(changed);
    ASSERT_TRUE(dom_element_set_presentation_style(child, "color", "green", &changed));
    EXPECT_STREQ(child->get_attribute("style"), authored);
    EXPECT_DOUBLE_EQ(style_tree_get_authored_declaration(child->specified_style,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.3);
    EXPECT_DOUBLE_EQ(dom_element_get_specified_value(child,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.8);
    EXPECT_TRUE(dom_element_get_specified_value(child, CSS_PROPERTY_COLOR)->important);

    dom_element_clear_cascaded_styles(child);
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX));
    apply(document_root, width, child);
    EXPECT_TRUE(dom_element_get_specified_value(child, CSS_PROPERTY_OPACITY)->presentation_value);
    ASSERT_TRUE(child->set_attribute("style", "opacity:0.4;color:blue"));
    EXPECT_TRUE(dom_element_get_specified_value(child, CSS_PROPERTY_COLOR)->presentation_value);
    EXPECT_DOUBLE_EQ(style_tree_get_authored_declaration(child->specified_style,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.4);

    EXPECT_FALSE(dom_element_set_presentation_style(child, "opacity", "invalid", &changed));
    EXPECT_FALSE(dom_element_set_presentation_style(child, "opacity", "0.2 !important", &changed));
    EXPECT_FALSE(dom_element_set_presentation_style(child, "not-a-css-property", "1", &changed));
    EXPECT_DOUBLE_EQ(dom_element_get_specified_value(child,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.8);
    EXPECT_TRUE(dom_element_clear_presentation_style(child));
    EXPECT_FALSE(dom_element_clear_presentation_style(child));
    EXPECT_DOUBLE_EQ(dom_element_get_specified_value(child,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.4);
    EXPECT_STREQ(child->get_attribute("style"), "opacity:0.4;color:blue");
}

TEST_F(StyleEpochTest, MixedAuthoredTreeRetainsInlineInsetsDuringRecascade) {
    DomElement* child = append("child");
    ASSERT_TRUE(child->set_attribute("style", "left:200px;top:40px;width:80px"));
    CssRule* position = rule(CSS_PROPERTY_POSITION,
        css_value_create_keyword(doc.document_pool, "absolute"));
    CssRule* opacity = rule(CSS_PROPERTY_OPACITY,
        css_value_create_number(doc.document_pool, 0.8));
    apply(document_root, position, child);
    apply(document_root, opacity, child);
    EXPECT_TRUE(style_tree_has_inline_declarations(child->specified_style));
    EXPECT_TRUE(style_tree_has_local_declarations(child->specified_style));
    dom_element_clear_cascaded_styles(child);
    apply(document_root, position, child);
    apply(document_root, opacity, child);
    EXPECT_FALSE(child->specified_style_shared());
    CssDeclaration* left = dom_element_get_specified_value(child, CSS_PROPERTY_LEFT);
    ASSERT_NE(left, nullptr);
    EXPECT_DOUBLE_EQ(left->value->data.length.value, 200.0);
    EXPECT_TRUE(left->specificity.inline_style);
}

TEST_F(StyleEpochTest, PresentationSamplesDoNotGrowRetainedPoolsOrArenas) {
    DomElement* child = append("child");
    const char* transforms[] = {
        "translate(1px,2px) rotate(10deg) scale(1,1)",
        "translate(3px,4px) rotate(20deg) scale(1.2,0.8)", "none"
    };
    bool changed = false;
    for (const char* transform : transforms)
        ASSERT_TRUE(dom_element_set_presentation_style(child, "transform", transform, &changed));
    ASSERT_TRUE(dom_element_set_presentation_style(child, "clip-path", "inset(0 10% 0 0)", &changed));
    ASSERT_TRUE(dom_element_set_presentation_style(child, "fill", "#336699", &changed));
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    size_t input_used = arena_total_used(input->arena);
    size_t nodes_used = arena_total_used(doc.node_arena);
    for (size_t i = 0; i < 256; i++) {
        ASSERT_TRUE(dom_element_set_presentation_style(child, "transform", transforms[i % 3], &changed));
        ASSERT_TRUE(dom_element_set_presentation_style(child, "opacity", i % 2 ? "0.5" : "1", &changed));
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_LE(repeated.live_bytes, warm.live_bytes + 4096u);
    EXPECT_EQ(arena_total_used(input->arena), input_used);
    EXPECT_EQ(arena_total_used(doc.node_arena), nodes_used);
    EXPECT_EQ(child->get_attribute("style"), nullptr);
    EXPECT_TRUE(dom_element_clear_presentation_style(child));
    EXPECT_TRUE(style_tree_is_empty(child->specified_style));
}

TEST_F(StyleEpochTest, ColdCanonicalEntriesRespectCacheBudgetAfterCascade) {
    DomElement* child = append("child");
    CssRule* variants[8] = {};
    for (size_t i = 0; i < 8; i++) {
        variants[i] = rule(CSS_PROPERTY_WIDTH,
            css_value_create_length(doc.document_pool, 20.0 + (double)i,
                                    CSS_UNIT_PX), (uint32_t)(i + 1u));
    }

    // A zero budget makes every unbound canonical snapshot eligible at the
    // outer cascade boundary while the child remains a live consumer.
    style_epoch_debug_set_cold_cache_cap(&doc, 0);
    for (size_t i = 0; i < 8; i++) {
        dom_element_clear_cascaded_styles(child);
        apply(document_root, variants[i], child);
    }

    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_EQ(stats.current_unbound_entry_count, 0u);
    EXPECT_EQ(stats.current_unbound_bytes, 0u);
    EXPECT_GT(stats.cache_eviction_count, 0u);
    EXPECT_TRUE(child->specified_style_shared());
    EXPECT_NE(dom_element_get_specified_value(child, CSS_PROPERTY_WIDTH), nullptr);
}

TEST_F(StyleEpochTest, RecascadeRetiresPseudoTreesAfterBorrowersRebind) {
    DomElement* host = append("host");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 12.0, CSS_UNIT_PX));
    ASSERT_EQ(dom_element_apply_pseudo_element_rule(host, width, {}, 1), 1);
    StyleTree* first = host->pseudo_style(PSEUDO_STYLE_BEFORE);
    ASSERT_NE(first, nullptr);

    DomElement* generated = DomElement::create(&doc, "::before", nullptr);
    ASSERT_NE(generated, nullptr);
    dom_element_borrow_specified_style(generated, first);
    EXPECT_EQ(first->borrow_ref_count, 1u);

    ASSERT_TRUE(dom_element_clear_pseudo_styles(host));
    StyleTree* second = host->pseudo_style(PSEUDO_STYLE_BEFORE);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second, first);
    EXPECT_EQ(generated->specified_style, first);
    EXPECT_TRUE(generated->specified_style_borrowed());

    dom_element_borrow_specified_style(generated, second);
    EXPECT_EQ(generated->specified_style, second);
    EXPECT_EQ(second->borrow_ref_count, 1u);
    dom_element_destroy(generated);
    EXPECT_EQ(second->borrow_ref_count, 0u);

    ASSERT_EQ(dom_element_apply_pseudo_element_rule(host, width, {}, 1), 1);
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    for (size_t i = 0; i < 32; i++) {
        ASSERT_TRUE(dom_element_clear_pseudo_styles(host));
        ASSERT_EQ(dom_element_apply_pseudo_element_rule(host, width, {}, 1), 1);
    }
    PoolStats stable = {};
    pool_get_detailed_stats(doc.document_pool, &stable);
    EXPECT_LE(stable.live_bytes, warm.live_bytes + 4096u);
}

// Every view-tree allocator is registered under its document's context, so
// the view tree is a subtree of the document in the ownership tree.
TEST(ViewTreeOwnershipTest, AllocatorsRegisterUnderTheOwnerContext) {
    MemContext* doc_ctx = mem_context_create(mem_context_root(), MEM_ROLE_NODE, "test.document");
    ASSERT_NE(doc_ctx, nullptr);
    ViewTree tree = {};
    tree.init(doc_ctx);
    ASSERT_NE(tree.prop_pool, nullptr);
    EXPECT_EQ(mem_node_owner((MemNode*)pool_get_mem_node(tree.prop_pool)), doc_ctx);
    Arena* arenas[] = {tree.canonical_prop_arena, tree.scratch_arena, tree.layout_pass_arena,
                       tree.render_scratch_arena, tree.display_list_arena};
    for (Arena* arena : arenas) {
        ASSERT_NE(arena, nullptr);
        EXPECT_EQ(mem_node_owner((MemNode*)arena_get_mem_node(arena)), doc_ctx);
    }
    tree.destroy();
    EXPECT_EQ(mem_context_live_count(doc_ctx), 0u);
    mem_context_destroy(doc_ctx);
}

TEST(RadiantImageSizing, IntrinsicPercentFitAndIndependentViewportAxesRetainTheirOwnBases) {
    const struct { RadiantImageAxis x, y; bool non_uniform; float width, height, content_width, content_height; } cases[] = {
        {{RADIANT_IMAGE_AUTO, 0}, {RADIANT_IMAGE_AUTO, 0}, false, NAN, NAN, 60, 30},
        {{RADIANT_IMAGE_LENGTH, 120}, {RADIANT_IMAGE_AUTO, 0}, false, NAN, NAN, 120, 60},
        {{RADIANT_IMAGE_AUTO, 0}, {RADIANT_IMAGE_LENGTH, 15}, false, 100, 100, 30, 15},
        {{RADIANT_IMAGE_LENGTH, 120}, {RADIANT_IMAGE_LENGTH, 15}, false, 100, 100, 30, 15},
        {{RADIANT_IMAGE_LENGTH, 120}, {RADIANT_IMAGE_LENGTH, 15}, true, 100, 100, 120, 15},
        {{RADIANT_IMAGE_FIT, 0}, {RADIANT_IMAGE_FIT, 0}, false, 120, 30, 60, 30},
        {{RADIANT_IMAGE_FIT, 0}, {RADIANT_IMAGE_AUTO, 0}, false, 120, 15, 120, 60},
        {{RADIANT_IMAGE_FIT_DOWN, 0}, {RADIANT_IMAGE_AUTO, 0}, false, 120, 10, 60, 30},
        {{RADIANT_IMAGE_FIT_UP, 0}, {RADIANT_IMAGE_AUTO, 0}, false, 30, 20, 60, 30},
        {{RADIANT_IMAGE_PERCENT, 50}, {RADIANT_IMAGE_PERCENT, 200}, false, NAN, NAN, 30, 15},
        {{RADIANT_IMAGE_PERCENT, 50}, {RADIANT_IMAGE_PERCENT, 200}, true, NAN, NAN, 30, 60},
        {{RADIANT_IMAGE_FIT, 0}, {RADIANT_IMAGE_AUTO, 0}, false, NAN, 100, 60, 30}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SCOPED_TRACE(i); const auto& test = cases[i]; RadiantImageSpec spec = {};
        spec.axes[0] = test.x; spec.axes[1] = test.y; spec.non_uniform = test.non_uniform;
        float width = test.width, height = test.height, content_width = 0.0f, content_height = 0.0f;
        ASSERT_TRUE(radiant_image_size(&spec, 60, 30, &width, &height, &content_width, &content_height));
        EXPECT_FLOAT_EQ(content_width, test.content_width); EXPECT_FLOAT_EQ(content_height, test.content_height);
        EXPECT_FLOAT_EQ(width, isnan(test.width) ? test.content_width : test.width);
        EXPECT_FLOAT_EQ(height, isnan(test.height) ? test.content_height : test.height);
    }
    RadiantImageSpec spec = {}; float width = NAN, height = NAN, x = 0.0f, y = 0.0f;
    EXPECT_FALSE(radiant_image_size(&spec, 0, 30, &width, &height, &x, &y));
    EXPECT_FALSE(radiant_image_size(&spec, 60, INFINITY, &width, &height, &x, &y));
}

TEST(RadiantImageSizing, AllowedScalesIntersectUniformAxesAndPreferDiscreteChoicesBeforeAny) {
    const float choices[] = {.5f, 1.5f, 3.0f}, shared[] = {1.0f, 1.5f}, disjoint[] = {1.0f}, too_large[] = {3.0f};
    struct Case { RadiantImageScales x, y; bool independent, success; float width, height; };
    const Case cases[] = {
        {{choices, 3, true}, {shared, 2, true}, false, true, 90, 45},
        {{choices, 3, false}, {shared, 2, false}, false, true, 90, 45},
        {{choices, 3, false}, {disjoint, 1, false}, false, false, 0, 0},
        {{choices, 3, true}, {disjoint, 1, true}, false, true, 90, 45},
        {{choices, 3, false}, {disjoint, 1, false}, true, true, 90, 30},
        {{too_large, 1, true}, {}, false, true, 120, 60},
        {{too_large, 1, false}, {}, false, false, 0, 0}
    };
    for (const Case& item : cases) {
        RadiantImageSpec spec = {}; spec.axes[0].kind = spec.axes[1].kind = RADIANT_IMAGE_FIT;
        spec.allowed[0] = item.x; spec.allowed[1] = item.y; spec.non_uniform = item.independent;
        float width = 120, height = 60, x = -1, y = -1;
        ASSERT_EQ(radiant_image_size(&spec, 60, 30, &width, &height, &x, &y), item.success);
        if (item.success) { EXPECT_FLOAT_EQ(x, item.width); EXPECT_FLOAT_EQ(y, item.height); }
        EXPECT_FLOAT_EQ(width, 120); EXPECT_FLOAT_EQ(height, 60);
    }
}

TEST(RadiantImageSizing, AllowedScalesRespectExactAxesAndTheInactiveUpDownFitBranches) {
    const float values[] = {.5f, 1.5f};
    const struct { RadiantImageAxis axis; float viewport; bool success; float content; } cases[] = {
        {{RADIANT_IMAGE_FIT_DOWN, 0}, 120, false, 0},
        {{RADIANT_IMAGE_FIT_DOWN, 0}, 45, true, 30},
        {{RADIANT_IMAGE_FIT_UP, 0}, 30, false, 0},
        {{RADIANT_IMAGE_FIT_UP, 0}, 120, true, 90},
        {{RADIANT_IMAGE_PERCENT, 150}, 120, true, 90},
        {{RADIANT_IMAGE_LENGTH, 90}, 120, true, 90},
        {{RADIANT_IMAGE_PERCENT, 125}, 120, false, 0},
        {{RADIANT_IMAGE_AUTO, 0}, 120, false, 0}
    };
    for (const auto& item : cases) {
        RadiantImageSpec spec = {}; spec.axes[0] = item.axis; spec.allowed[0] = {values, 2, false};
        float width = item.viewport, height = NAN, x = 0, y = 0;
        ASSERT_EQ(radiant_image_size(&spec, 60, 30, &width, &height, &x, &y), item.success);
        if (item.success) { EXPECT_FLOAT_EQ(x, item.content); EXPECT_FLOAT_EQ(y, item.content * .5f); }
    }
}

TEST(RadiantLabelBodySizing, SeparateWidthsAndGapRetainTheirOwnContainingBlockBases) {
    RadiantLabelBodySpec spec = {}; spec.distance = {25.0f, true}; spec.separation = {5.0f, true};
    float label = 0.0f, gap = 0.0f, body = 0.0f;
    ASSERT_TRUE(radiant_label_body_size(&spec, 200.0f, &label, &gap, &body));
    EXPECT_FLOAT_EQ(label, 40.0f); EXPECT_FLOAT_EQ(gap, 10.0f); EXPECT_FLOAT_EQ(body, 150.0f);
    spec.distance = {32.0f, false}; spec.separation = {8.0f, false};
    ASSERT_TRUE(radiant_label_body_size(&spec, 200.0f, &label, &gap, &body));
    EXPECT_FLOAT_EQ(label, 24.0f); EXPECT_FLOAT_EQ(gap, 8.0f); EXPECT_FLOAT_EQ(body, 168.0f);
    spec.separation.value = 40.0f; EXPECT_FALSE(radiant_label_body_size(&spec, 200.0f, &label, &gap, &body));
    spec.separation.value = -1.0f; EXPECT_FALSE(radiant_label_body_size(&spec, 200.0f, &label, &gap, &body));
    spec.separation.value = 8.0f; EXPECT_FALSE(radiant_label_body_size(&spec, 32.0f, &label, &gap, &body));
    EXPECT_FALSE(radiant_label_body_size(&spec, INFINITY, &label, &gap, &body));
}

TEST(LayoutTableTracks, ProportionalDistributionRetainsBasesAndHandlesLargeWeightsWithoutOverflow) {
    float widths[] = {20.0f, 10.0f, 0.0f, 0.0f};
    const float weights[] = {0.0f, 2.0f, 2.0f, 1.0f}; float extent = 230.0f;
    layout_table_distribute_fixed_columns(widths, 4, &extent, 30.0f, 2, weights);
    EXPECT_FLOAT_EQ(widths[0], 20.0f); EXPECT_FLOAT_EQ(widths[1], 90.0f);
    EXPECT_FLOAT_EQ(widths[2], 80.0f); EXPECT_FLOAT_EQ(widths[3], 40.0f); EXPECT_FLOAT_EQ(extent, 230.0f);
    float large[] = {0.0f, 0.0f}; const float large_weights[] = {3e38f, 1e38f}; extent = 120.0f;
    layout_table_distribute_fixed_columns(large, 2, &extent, 0.0f, 2, large_weights);
    EXPECT_FLOAT_EQ(large[0], 90.0f); EXPECT_FLOAT_EQ(large[1], 30.0f);
    float overfull[] = {100.0f, 60.0f}; extent = 120.0f;
    layout_table_distribute_fixed_columns(overfull, 2, &extent, 160.0f, 0, large_weights);
    EXPECT_FLOAT_EQ(extent, 160.0f); EXPECT_FLOAT_EQ(overfull[0], 100.0f); EXPECT_FLOAT_EQ(overfull[1], 60.0f);
    // unspecified tracks share the remainder with unit weight beside authored proportional tracks.
    float mixed[] = {20.0f, 0.0f, 0.0f, 0.0f}; const float mixed_weights[] = {0.0f, 2.0f, 2.0f, 0.0f}; extent = 200.0f;
    layout_table_distribute_fixed_columns(mixed, 4, &extent, 20.0f, 3, mixed_weights);
    EXPECT_FLOAT_EQ(mixed[0], 20.0f); EXPECT_FLOAT_EQ(mixed[1], 72.0f);
    EXPECT_FLOAT_EQ(mixed[2], 72.0f); EXPECT_FLOAT_EQ(mixed[3], 36.0f);
}

TEST(FoExpressions, ProportionalProjectionPreservesAffineBasesAndRejectsNonlinearTrackExpressions) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "test.fo.proportional"); ASSERT_NE(pool, nullptr);
    struct Case { const char* text; double weight, base; CssMathType type; };
    const Case cases[] = {{"proportional-column-width(3)", 3, 0, CSS_MATH_LENGTH},
        {"2 * 3 * proportional-column-width(4)", 24, 0, CSS_MATH_LENGTH},
        {"(10pt + proportional-column-width(2)) * 3", 6, 40, CSS_MATH_LENGTH},
        {"proportional-column-width(3) div 2 + 6pt", 1.5, 8, CSS_MATH_LENGTH},
        {"5% + proportional-column-width(1)", 1, 5, CSS_MATH_LENGTH_PERCENT},
        {"max(6pt + proportional-column-width(2), 15pt + proportional-column-width(2))", 2, 20, CSS_MATH_LENGTH}};
    for (const Case& item : cases) {
        SCOPED_TRACE(item.text); double weight = 0.0;
        RadiantFoExpression expression = radiant_fo_expression(pool, item.text, 1000, 64, nullptr, nullptr, &weight);
        ASSERT_EQ(expression.status, TYPESET_OK) << expression.reason; EXPECT_DOUBLE_EQ(weight, item.weight);
        CssMathEvaluationContext context = {}; context.preserve_percentages = true;
        CssMathResult base = css_math_evaluate(expression.value, &context); EXPECT_TRUE(base.resolved); EXPECT_EQ(base.type, item.type);
        EXPECT_NEAR(item.type == CSS_MATH_LENGTH_PERCENT ? base.percentage : base.value, item.base, 1e-9);
    }
    const char* invalid[] = {"proportional-column-width(1) * proportional-column-width(2)",
        "10pt div proportional-column-width(2)", "1pt * proportional-column-width(1)",
        "min(10pt, proportional-column-width(1))", "abs(proportional-column-width(1))",
        "proportional-column-width(1) - proportional-column-width(2)", "proportional-column-width(1) + 2",
        "proportional-column-width(proportional-column-width(1))", "proportional-column-width(1) * 1e309"};
    for (const char* text : invalid) {
        SCOPED_TRACE(text); double weight = 0.0;
        RadiantFoExpression expression = radiant_fo_expression(pool, text, 1000, 64, nullptr, nullptr, &weight);
        EXPECT_EQ(expression.status, TYPESET_INVALID); EXPECT_EQ(expression.value, nullptr); EXPECT_NE(expression.reason, nullptr);
    }
    StrBuf* sum = strbuf_new(); ASSERT_NE(sum, nullptr);
    for (size_t i = 0; i < 1024; i++) {
        if (i) strbuf_append_str(sum, " + ");
        strbuf_append_str(sum, "proportional-column-width(1)");
    }
    double weight = 0.0;
    RadiantFoExpression flat = radiant_fo_expression(pool, sum->str, 10000, 64, nullptr, nullptr, &weight);
    ASSERT_EQ(flat.status, TYPESET_OK) << flat.reason; EXPECT_DOUBLE_EQ(weight, 1024); strbuf_free(sum);
    EXPECT_EQ(radiant_fo_expression(pool, "1pt + proportional-column-width(1)", 3, 64, nullptr, nullptr, &weight).status, TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(radiant_fo_expression(pool, "proportional-column-width(1)", 1000, 64).status, TYPESET_INVALID);
    mem_pool_destroy(pool);
}

TEST(FoExpressions, ScaleListsRetainTypedArithmeticKeywordsAndExpressionBudgets) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "test.fo.scale-list"); ASSERT_NE(pool, nullptr);
    RadiantFoExpression expression = radiant_fo_expression(pool, "any 25% * 2 min(200%, 150%) 300%", 1000, 32,
        nullptr, nullptr, nullptr, FO_EXPRESSION_SCALES);
    ASSERT_EQ(expression.status, TYPESET_OK) << expression.reason;
    ASSERT_NE(expression.value, nullptr); ASSERT_EQ(expression.value->type, CSS_VALUE_TYPE_LIST);
    ASSERT_EQ(expression.value->data.list.count, 4);
    EXPECT_STREQ(css_value_identifier_name(expression.value->data.list.values[0]), "any");
    CssMathEvaluationContext context = {}; context.preserve_percentages = true;
    const double percentages[] = {50, 150, 300};
    for (size_t i = 0; i < 3; i++) {
        CssMathResult result = css_math_evaluate(expression.value->data.list.values[i + 1], &context);
        EXPECT_EQ(result.type, CSS_MATH_PERCENT); EXPECT_TRUE(result.resolved); EXPECT_DOUBLE_EQ(result.percentage, percentages[i]);
    }
    for (const char* text : {"", "any50%", "50%any", "50%, 100%", "(50%)any", "any unknown", "any 1e309%"}) {
        SCOPED_TRACE(text); EXPECT_EQ(radiant_fo_expression(pool, text, 1000, 32, nullptr, nullptr, nullptr, FO_EXPRESSION_SCALES).status, TYPESET_INVALID);
    }
    EXPECT_EQ(radiant_fo_expression(pool, "any 50% 100%", 3, 32, nullptr, nullptr, nullptr, FO_EXPRESSION_SCALES).status, TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(radiant_fo_expression(pool, "50%", 100, 0, nullptr, nullptr, nullptr, FO_EXPRESSION_SCALES).status, TYPESET_BUDGET_EXHAUSTED);
    mem_pool_destroy(pool);
}

TEST(FoExpressions, DecorationComponentsRetainArithmeticKeywordsHexAndComputedRgb) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "test.fo.components"); ASSERT_NE(pool, nullptr);
    RadiantFoExpression expression = radiant_fo_expression(pool, "1pt + 2pt solid rgb(50% + 25%, 10pt div 1pt, 0)",
        1000, 32, nullptr, nullptr, nullptr, FO_EXPRESSION_COMPONENTS);
    ASSERT_EQ(expression.status, TYPESET_OK) << expression.reason;
    ASSERT_EQ(expression.value->type, CSS_VALUE_TYPE_LIST); ASSERT_EQ(expression.value->data.list.count, 3);
    EXPECT_STREQ(css_value_identifier_name(expression.value->data.list.values[1]), "solid");
    CssMathEvaluationContext context = {}; context.preserve_percentages = true;
    CssMathResult width = css_math_evaluate(expression.value->data.list.values[0], &context);
    EXPECT_EQ(width.type, CSS_MATH_LENGTH); EXPECT_TRUE(width.resolved); EXPECT_DOUBLE_EQ(width.value, 4);
    CssValue* color = expression.value->data.list.values[2]; ASSERT_EQ(color->type, CSS_VALUE_TYPE_FUNCTION);
    EXPECT_DOUBLE_EQ(css_math_evaluate(color->data.function->args[0], &context).value, 191.25);
    EXPECT_DOUBLE_EQ(css_math_evaluate(color->data.function->args[1], &context).value, 10);
    expression = radiant_fo_expression(pool, "1pt + 2pt solid #d04020", 1000, 32, nullptr, nullptr, nullptr, FO_EXPRESSION_COMPONENTS);
    ASSERT_EQ(expression.status, TYPESET_OK) << expression.reason;
    EXPECT_EQ(expression.value->data.list.values[2]->type, CSS_VALUE_TYPE_COLOR);
    for (const char* text : {"", "1pt solid #zzzz", "1pt 2pt, 3pt", "1pt solid rgb(1px,2,3)",
        "1pt solid rgb(1 div 0,2,3)", "1pt (2pt)solid", "1pt solid rgb(1e309,0,0)"}) {
        SCOPED_TRACE(text); EXPECT_EQ(radiant_fo_expression(pool, text, 1000, 32,
            nullptr, nullptr, nullptr, FO_EXPRESSION_COMPONENTS).status, TYPESET_INVALID);
    }
    EXPECT_EQ(radiant_fo_expression(pool, "1pt solid red", 3, 32, nullptr, nullptr, nullptr,
        FO_EXPRESSION_COMPONENTS).status, TYPESET_BUDGET_EXHAUSTED);
    mem_pool_destroy(pool);
}

TEST(FoExpressions, ArithmeticUsesCommonTypedMathWithXslPrecedenceRoundingAndRemainders) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "test.fo.expressions"); ASSERT_NE(pool, nullptr);
    struct Case { const char* source; CssMathType type; double value; };
    const Case cases[] = {
        {"2 + 3 * 4", CSS_MATH_NUMBER, 14}, {"(2 + 3) * 4", CSS_MATH_NUMBER, 20},
        {"20 div 2 div 5", CSS_MATH_NUMBER, 2}, {"2+3", CSS_MATH_NUMBER, 5},
        {"2 -3", CSS_MATH_NUMBER, -1}, {"-(2 + 3)", CSS_MATH_NUMBER, -5},
        {"-7 mod 3", CSS_MATH_NUMBER, -1}, {"7 mod -3", CSS_MATH_NUMBER, 1},
        {"floor(-2.1)", CSS_MATH_NUMBER, -3}, {"ceiling(-2.1)", CSS_MATH_NUMBER, -2},
        {"round(-2.5)", CSS_MATH_NUMBER, -2}, {"round(2.5)", CSS_MATH_NUMBER, 3},
        {"max(2, min(6, 3))", CSS_MATH_NUMBER, 3}, {"abs(-2pt) * 3", CSS_MATH_LENGTH, 8},
        {"1in - 6pt * 2", CSS_MATH_LENGTH, 80}, {"20% * 2 + 10%", CSS_MATH_PERCENT, 50},
        {"7pt mod 2pt", CSS_MATH_LENGTH, 4.0 / 3.0},
        {"1in div 12pt", CSS_MATH_NUMBER, 6},
        {"2pt * 3pt div 1pt", CSS_MATH_LENGTH, 8},
        {"(2pt * 3pt + 4pt * 3pt) div 2pt", CSS_MATH_LENGTH, 12},
        {"(2 div 1pt) * 3pt", CSS_MATH_NUMBER, 6},
        {"(2pt * 3pt) div (1pt * 2pt)", CSS_MATH_NUMBER, 3},
        {"abs(-2pt * 3pt) div 1pt", CSS_MATH_LENGTH, 8},
        {"min(2pt * 3pt, 4pt * 3pt) div 1pt", CSS_MATH_LENGTH, 8},
        {"(7pt * 3pt mod (2pt * 3pt)) div 1pt", CSS_MATH_LENGTH, 4},
        {"floor(1in div 10pt)", CSS_MATH_NUMBER, 7}
    };
    CssMathEvaluationContext context = {}; context.preserve_percentages = true;
    for (const auto& item : cases) {
        SCOPED_TRACE(item.source);
        RadiantFoExpression expression = radiant_fo_expression(pool, item.source, 1000, 32);
        ASSERT_EQ(expression.status, TYPESET_OK) << expression.reason; ASSERT_NE(expression.value, nullptr);
        CssMathResult value = css_math_evaluate(expression.value, &context);
        EXPECT_EQ(value.type, item.type); ASSERT_TRUE(value.resolved);
        EXPECT_NEAR(item.type == CSS_MATH_PERCENT ? value.percentage : value.value, item.value, 1e-9);
    }
    const char* invalid[] = {"", "2 3", "1 / 2", "2 +", "2 + pi", "1 div 0", "1 mod 0", "2 + 1pt",
        "min(1)", "max(1,2,3)", "floor(1pt)", "ceiling(1%)", "round(1,2)", "floor(2", "Floor(2)",
        "min(1,2) garbage", "sqrt(4)", "1pt * 2pt", "10pt-2pt", "1deg + 2deg", "/*comment*/ 2",
        "1 div 1pt", "1pt * 1pt + 1pt", "min(1pt * 1pt, 1pt) div 1pt",
        "1pt * 1pt mod 1pt", "(1pt * 1pt) div (1pt - 1pt)", "10% * 2pt div 1pt"};
    for (const char* text : invalid) {
        SCOPED_TRACE(text); RadiantFoExpression expression = radiant_fo_expression(pool, text, 1000, 32);
        EXPECT_EQ(expression.status, TYPESET_INVALID); EXPECT_EQ(expression.value, nullptr); EXPECT_NE(expression.reason, nullptr);
    }
    EXPECT_EQ(radiant_fo_expression(pool, "1 + 2 + 3", 3, 32).status, TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(radiant_fo_expression(pool, "(((1)))", 100, 2).status, TYPESET_BUDGET_EXHAUSTED);
    StrBuf* long_sum = strbuf_new(); ASSERT_NE(long_sum, nullptr);
    strbuf_append_str(long_sum, "1");
    for (size_t i = 1; i < 1024; i++) strbuf_append_str(long_sum, " + 1");
    RadiantFoExpression expression = radiant_fo_expression(pool, long_sum->str, 4096, 32);
    ASSERT_EQ(expression.status, TYPESET_OK) << expression.reason;
    EXPECT_DOUBLE_EQ(css_math_evaluate(expression.value, &context).value, 1024);
    strbuf_free(long_sum);
    mem_pool_destroy(pool);
}

TEST(FoExpressions, RgbChannelsUseCommonMathAndRejectNonNumericOrNonfiniteResults) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "test.fo.colors"); ASSERT_NE(pool, nullptr);
    struct Case { const char* text; uint8_t r, g, b; };
    const Case cases[] = {{"rgb(255 div 2, floor(127.9), ceiling(63.2))", 128, 127, 64},
        {"rgb(max(0, 300), -10, round(127.5))", 255, 0, 128},
        {"rgb(1in div 1pt, (2pt * 3pt) div (1pt * 2pt), abs(-4))", 72, 3, 4},
        {"rgb(50%, 10%, 110%)", 128, 26, 255},
        {"rgb(100% div 50%, floor(50%), max(10%, 20%))", 2, 127, 51},
        {"rgb(abs(-50%) + 0.5, round(50%), 10% * 2 - 1)", 128, 128, 50},
        {"rgb(100% * 1pt div 1pt, 50% mod 100, -10%)", 255, 28, 0}};
    for (const Case& item : cases) {
        SCOPED_TRACE(item.text);
        RadiantFoExpression expression = radiant_fo_expression(pool, item.text, 1000, 32);
        ASSERT_EQ(expression.status, TYPESET_OK) << expression.reason; ASSERT_NE(expression.value, nullptr);
        CssComputedColor color = {}; ASSERT_TRUE(css_color_compute(expression.value, &color));
        uint8_t r, g, b, a; ASSERT_TRUE(css_color_to_rgba(&color, &r, &g, &b, &a));
        EXPECT_EQ(r, item.r); EXPECT_EQ(g, item.g); EXPECT_EQ(b, item.b); EXPECT_EQ(a, 255);
    }
    for (const char* invalid : {"rgb(1,2)", "rgb(1,2,3,4)", "rgb(1pt,2,3)",
            "rgb(1 div 0,2,3)", "rgb(1e308 * 1e308,2,3)", "rgb(rgb(1,2,3),2,3)",
            "rgb(1,2,3) + 1", "rgb(1,2,3) div 2", "rgb(1,2,3) trailing"}) {
        SCOPED_TRACE(invalid);
        RadiantFoExpression expression = radiant_fo_expression(pool, invalid, 1000, 32);
        EXPECT_EQ(expression.status, TYPESET_INVALID); EXPECT_EQ(expression.value, nullptr); EXPECT_NE(expression.reason, nullptr);
    }
    EXPECT_EQ(radiant_fo_expression(pool, "rgb(1,2,3)", 3, 32).status, TYPESET_BUDGET_EXHAUSTED);
    mem_pool_destroy(pool);
}

TEST(FoExpressions, RgbPercentagesUseTheirFunctionBaseThroughNestedNumericExpressions) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "test.fo.color.percent"); ASSERT_NE(pool, nullptr);
    const struct { const char* text; uint8_t expected[3]; } colors[] = {
        {"rgb(86.66666666666667%, 100%, 173.33333333333334% div 2)", {221, 255, 221}},
        {"rgb(100% * 2 div 3, round(66.50980392156863%), 100%)", {170, 170, 255}}
    };
    for (const auto& color : colors) {
        SCOPED_TRACE(color.text);
        RadiantFoExpression expression = radiant_fo_expression(pool, color.text, 1000, 32);
        ASSERT_EQ(expression.status, TYPESET_OK);
        CssDeclaration declaration = {}; declaration.value = expression.value;
        const char* text = css_serialize_declaration_value(&declaration, pool);
        SCOPED_TRACE(text);
        CssDeclaration* reparsed = css_parse_property_value_declaration("color", 5, text, strlen(text), pool);
        ASSERT_NE(reparsed, nullptr); ASSERT_TRUE(css_declaration_is_supported(reparsed));
        CssComputedColor computed; ASSERT_TRUE(css_color_compute(reparsed->value, &computed));
        uint8_t r, g, b, a; ASSERT_TRUE(css_color_to_rgba(&computed, &r, &g, &b, &a));
        EXPECT_EQ(r, color.expected[0]); EXPECT_EQ(g, color.expected[1]); EXPECT_EQ(b, color.expected[2]);
    }
    for (const char* invalid : {"rgb(100% * 1pt, 0, 0)", "rgb(100% div 0%, 0, 0)",
            "rgb(1e308%, 0, 0)", "floor(50%)", "rgb(100%, 0%, 0%) + 50%"}) {
        SCOPED_TRACE(invalid);
        EXPECT_EQ(radiant_fo_expression(pool, invalid, 1000, 32).status, TYPESET_INVALID);
    }
    RadiantFoExpression percentage = radiant_fo_expression(pool, "50% + 25%", 100, 32);
    ASSERT_EQ(percentage.status, TYPESET_OK);
    CssMathEvaluationContext context = {}; context.preserve_percentages = true;
    CssMathResult value = css_math_evaluate(percentage.value, &context);
    EXPECT_EQ(value.type, CSS_MATH_PERCENT); EXPECT_DOUBLE_EQ(value.percentage, 75);
    EXPECT_EQ(radiant_fo_expression(pool, "rgb(100%, 0%, 0%)", 3, 32).status, TYPESET_BUDGET_EXHAUSTED);
    mem_pool_destroy(pool);
}

TEST(RadiantFlowTraits, SpaceResolutionAppliesConditionalRunsForcePrecedenceAndRangeIntersection) {
    RadiantSpaceSpec spaces[] = {{10, 10, 10, 0, false, false, true},
        {4, 4, 4, 0, false, true, true}, {5, 5, 5, 0, false, false, true}};
    EXPECT_FLOAT_EQ(radiant_spaces_resolve(spaces, 3, true, false).optimum, 5.0f);
    EXPECT_FLOAT_EQ(radiant_spaces_resolve(spaces, 3, false, true).optimum, 10.0f);
    spaces[0].force = spaces[2].force = true;
    EXPECT_FLOAT_EQ(radiant_spaces_resolve(spaces, 3, false, false).optimum, 15.0f);
    spaces[0] = {2, 8, 12, 3, false, true, true}; spaces[1] = {6, 8, 10, 3, false, true, true};
    RadiantSpaceSpec intersection = radiant_spaces_resolve(spaces, 2, false, false);
    EXPECT_FLOAT_EQ(intersection.minimum, 6.0f); EXPECT_FLOAT_EQ(intersection.optimum, 8.0f); EXPECT_FLOAT_EQ(intersection.maximum, 10.0f);
    spaces[2] = {1, 1, 1, 4, false, true, true};
    EXPECT_FLOAT_EQ(radiant_spaces_resolve(spaces, 3, false, false).optimum, 1.0f);
    EXPECT_GT(radiant_keep_compare({INT32_MIN, RADIANT_KEEP_NUMBER}, {}), 0);
    EXPECT_GT(radiant_keep_compare({0, RADIANT_KEEP_ALWAYS}, {INT32_MAX, RADIANT_KEEP_NUMBER}), 0);
}
