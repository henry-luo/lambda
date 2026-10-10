#include "../radiant/layout_table.hpp"
#include <gtest/gtest.h>
#include "../radiant/view.hpp"
#include "../radiant/view_tree_model.hpp"
#include "../radiant/view_tree_css.hpp"
#include "../radiant/typeset.hpp"
#include "../radiant/typeset_marks.hpp"
#include "../radiant/typeset_regions.hpp"
#include "../radiant/layout_paged.hpp"
#include "../radiant/page_document.hpp"
#include "../radiant/page_fo.hpp"
#include "../radiant/page_fo_expression.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/render.hpp"
#include "../lambda/input/css/css_paged_media.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/dom/dom.h"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lib/mem_factory.h"
#include "../lib/memtrack.h"
#include "../lib/tagged.hpp"
#include "../lib/file.h"
#include "../lib/font/font.h"
#include "../lib/ref_count.h"
#include <math.h>

TEST(ViewModelOptionsTest, PageSelectionsParseStrictPhysicalRangesWithoutPartialPublication) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_RENDER, "test.page.selection"); ASSERT_NE(pool, nullptr);
    ViewPageSelection selection = {};
    ASSERT_EQ(view_page_selection_parse(pool, nullptr, &selection), VIEW_MODEL_OK); EXPECT_TRUE(selection.all);
    ASSERT_EQ(view_page_selection_parse(pool, " none ", &selection), VIEW_MODEL_OK); EXPECT_FALSE(selection.all);
    ASSERT_EQ(view_page_selection_parse(pool, " 4 - 5, 2,4 ", &selection), VIEW_MODEL_OK);
    ASSERT_EQ(selection.range_count, 3u); EXPECT_FALSE(selection.all);
    EXPECT_EQ(selection.ranges[0].first, 4u); EXPECT_EQ(selection.ranges[0].last, 5u);
    EXPECT_EQ(selection.ranges[1].first, 2u); EXPECT_EQ(selection.ranges[2].first, 4u);
    ViewPageSelection retained = selection;
    const char* invalid[] = {"", " ", "0", "1,", "1, ", "1,,2", "-1", "2-1", "1-0", "1-all", "all,1",
        "1x2", "1.5", "1e2", "1 2", "1;2", "4294967296", "1-4294967296", "18446744073709551616"};
    for (const char* text : invalid) {
        SCOPED_TRACE(text); EXPECT_FALSE(view_page_selection_text_valid(text));
        EXPECT_EQ(view_page_selection_parse(pool, text, &selection), VIEW_MODEL_INVALID_PAGE_RANGE);
        EXPECT_EQ(selection.ranges, retained.ranges); EXPECT_EQ(selection.range_count, retained.range_count);
    }
    EXPECT_TRUE(view_page_selection_text_valid("4294967295"));
    EXPECT_FALSE(view_page_selection_text_valid(nullptr));
    EXPECT_EQ(view_page_selection_parse(nullptr, "all", &selection), VIEW_MODEL_INVALID_ARGUMENT);
    EXPECT_EQ(view_page_selection_parse(pool, "all", nullptr), VIEW_MODEL_INVALID_ARGUMENT);
    mem_pool_destroy(pool);
}

TEST(ViewModelOptionsTest, PagedRenderOptionsKeepPreviewGeometrySeparateFromPhysicalSelection) {
    RenderPagedOptions options = render_paged_options_default(); const char* error = nullptr;
    EXPECT_EQ(options.preview.rows, 1u); EXPECT_EQ(options.preview.scale, 1.0f);
    EXPECT_FALSE(options.block_remote_resources);
    ASSERT_TRUE(render_paged_option_apply(&options, "--block-remote-resources", nullptr, &error));
    EXPECT_TRUE(options.block_remote_resources);
    EXPECT_EQ(render_paged_option_arity("--unknown"), -1); EXPECT_EQ(render_paged_option_arity("--book"), 0);
    EXPECT_EQ(render_paged_option_arity("--pages"), 1);
    EXPECT_TRUE(render_paged_input_supported("document.pdf?version=1"));
    ASSERT_TRUE(render_paged_option_apply(&options, "--import-pages", "2,49-53", &error));
    ASSERT_TRUE(render_paged_option_apply(&options, "--import-page-limit", "53", &error));
    EXPECT_STREQ(options.import_pages, "2,49-53"); EXPECT_EQ(options.import_page_limit, 53u);
    ASSERT_TRUE(render_paged_option_apply(&options, "--pages", "1,3-5", &error));
    ASSERT_TRUE(render_paged_option_apply(&options, "--export-pages", "2,4", &error));
    EXPECT_STREQ(options.preview_pages, "1,3-5"); EXPECT_STREQ(options.export_pages, "2,4");
    ASSERT_TRUE(render_paged_option_apply(&options, "--page-grid", "2x3", &error));
    EXPECT_EQ(options.preview.rows, 2u); EXPECT_EQ(options.preview.columns, 3u);
    ASSERT_TRUE(render_paged_option_apply(&options, "--page-scale", ".25", &error));
    ASSERT_TRUE(render_paged_option_apply(&options, "--thumbnail-page", "5", &error));
    EXPECT_EQ(options.preview.scale, .25f); EXPECT_EQ(options.thumbnail_page, 5u);
    ASSERT_TRUE(render_paged_option_apply(&options, "--book-page", "2", &error));
    EXPECT_EQ(options.preview.arrangement, VIEW_PAGES_BOOK); EXPECT_EQ(options.preview.anchor_page, 2u);
    const struct { const char* name; const char* value; } invalid[] = {
        {"--pages", "1,"}, {"--import-pages", "none"}, {"--import-page-limit", "0"}, {"--import-page-limit", "1.5"},
        {"--page-grid", "0x2"}, {"--page-grid", "2x3x4"}, {"--page-grid", "2"},
        {"--page-fill", "diagonal"}, {"--page-groups", "row"}, {"--page-scale", "nan"},
        {"--page-scale", "inf"}, {"--page-scale", "1px"}, {"--page-scale", "1e100"},
        {"--page-scale", "1e-100"}, {"--page-scale", "0"}, {"--page-padding", "-1"},
        {"--thumbnail-page", "0"}, {"--book-page", "1.5"}, {"--book-page", "4294967296"}, {"--pages", nullptr}};
    for (const auto& argument : invalid) {
        SCOPED_TRACE(argument.name);
        SCOPED_TRACE(argument.value ? argument.value : "missing");
        EXPECT_FALSE(render_paged_option_apply(&options, argument.name, argument.value, &error));
        EXPECT_NE(error, nullptr); EXPECT_EQ(options.preview.scale, .25f); EXPECT_EQ(options.thumbnail_page, 5u);
    }
    ASSERT_TRUE(render_paged_option_apply(&options, "--page-padding", "0", &error)); EXPECT_EQ(error, nullptr);
}

TEST(PagedCssTest, TypedSelectorsMarginsAndOriginalStrings) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.paged.css");
    ASSERT_NE(pool, nullptr);
    const char* css = "@page chapter:first, :blank:left { size: A4 landscape; margin: 12mm 18mm; "
        "@top-center { content: \"Chapter: \" string(chapter); color: blue } "
        "@unknown { content: \"skip\"; @bottom-left { content: \"also skip\" } } "
        "margin-bottom: 24mm; @bottom-right { content: counter(page) \" / \" counter(pages) } }";
    CssRule* rule = css_parse_rule_text(css, strlen(css), pool);
    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->type, CSS_RULE_PAGE);
    ASSERT_NE(rule->page, nullptr);
    EXPECT_EQ(rule->page->selector_count, 2u);
    EXPECT_STREQ(rule->page->selectors[0].name, "chapter");
    EXPECT_EQ(rule->page->selectors[0].pseudos, CSS_PAGE_FIRST);
    EXPECT_EQ(rule->page->selectors[1].pseudos, CSS_PAGE_BLANK | CSS_PAGE_LEFT);
    EXPECT_EQ(rule->page->declaration_count, 3u);
    ASSERT_EQ(rule->page->area_count, 2u);
    EXPECT_EQ(rule->page->areas[0].box, CSS_PAGE_TOP_CENTER);
    EXPECT_EQ(rule->page->areas[1].box, CSS_PAGE_BOTTOM_RIGHT);
    EXPECT_NE(strstr(rule->data.generic_rule.content, "\"Chapter: \""), nullptr);
    EXPECT_NE(strstr(rule->page->areas[1].declarations[0]->value_text, "\" / \""), nullptr);
    EXPECT_TRUE(css_page_selector_matches(&rule->page->selectors[0], "chapter", CSS_PAGE_FIRST | CSS_PAGE_RIGHT));
    EXPECT_FALSE(css_page_selector_matches(&rule->page->selectors[0], "Chapter", CSS_PAGE_FIRST));
    EXPECT_GT(css_page_specificity_compare(&rule->page->selectors[0], &rule->page->selectors[1]), 0);
    mem_pool_destroy(pool);
}

TEST(PagedCssTest, InvalidSelectorDropsWholeRuleAndRepeatedPseudosCount) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.paged.css.invalid");
    ASSERT_NE(pool, nullptr);
    const char* invalid[] = {"@page , :left {}", "@page chapter:unknown {}", "@page auto {}", "@page :left, {}"};
    for (const char* text : invalid) EXPECT_EQ(css_parse_rule_text(text, strlen(text), pool), nullptr);
    const char* css = "@page :first:first:left:left { margin: 0 }";
    CssRule* rule = css_parse_rule_text(css, strlen(css), pool);
    ASSERT_NE(rule, nullptr);
    ASSERT_NE(rule->page, nullptr);
    EXPECT_EQ(rule->page->selectors[0].state_specificity, 2u);
    EXPECT_EQ(rule->page->selectors[0].side_specificity, 2u);
    mem_pool_destroy(pool);
}

TEST(PagedCssTest, FootnoteAreasRemainDistinctFromMarginsAndUnknownNestedRules) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.paged.css.footnote"); ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    const char* text = "@page { margin: 10px; @footnote { max-height: 24px; border-top: 2px solid blue } "
        "@bottom-left { content: 'footer' } @footnote { padding: 4px 5px } "
        "@footnote invalid { padding: 99px } @unknown { @footnote { padding: 88px } } }";
    CssRule* rule = css_parse_rule_text(text, strlen(text), pool); ASSERT_NE(rule, nullptr); ASSERT_NE(rule->page, nullptr);
    EXPECT_EQ(rule->page->declaration_count, 1u); ASSERT_EQ(rule->page->area_count, 3u);
    EXPECT_EQ(rule->page->areas[0].kind, CSS_PAGE_AREA_FOOTNOTE);
    EXPECT_EQ(rule->page->areas[0].declaration_count, 2u);
    EXPECT_EQ(rule->page->areas[1].kind, CSS_PAGE_AREA_MARGIN);
    EXPECT_EQ(rule->page->areas[1].box, CSS_PAGE_BOTTOM_LEFT);
    EXPECT_EQ(rule->page->areas[2].kind, CSS_PAGE_AREA_FOOTNOTE);
    EXPECT_TRUE(css_property_shorthand_contains(CSS_PROPERTY_BORDER_TOP, CSS_PROPERTY_BORDER_TOP_STYLE));
    EXPECT_FALSE(css_property_shorthand_contains(CSS_PROPERTY_BORDER_TOP, CSS_PROPERTY_BORDER_LEFT_STYLE));
    EXPECT_TRUE(css_property_shorthand_contains(CSS_PROPERTY_BORDER, CSS_PROPERTY_BORDER_LEFT_COLOR));
    mem_pool_destroy(pool);
}

TEST(PagedCssTest, PublishingIdentifiersKeepCaseEvenWhenTheySpellOrdinaryKeywords) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.paged.css.ident"); ASSERT_NE(pool, nullptr);
    const char* declarations[] = {"content: STRING(Red) STRING(red)", "position: RUNNING(Blue)",
        "counter-reset: Red 4 red 2", "string-set: Blue 'Upper', blue 'Lower'", "page: Red"};
    for (size_t i = 0; i < 5; i++) {
        CssDeclaration* declaration = css_parse_declaration_text(declarations[i], strlen(declarations[i]), pool);
        ASSERT_NE(declaration, nullptr); ASSERT_NE(declaration->value, nullptr);
        const CssValue* value = declaration->value;
        if (i == 0) {
            ASSERT_EQ(value->type, CSS_VALUE_TYPE_LIST); ASSERT_EQ(value->data.list.count, 2);
            EXPECT_STREQ(css_value_identifier_name(value->data.list.values[0]->data.function->args[0]), "Red");
            EXPECT_STREQ(css_value_identifier_name(value->data.list.values[1]->data.function->args[0]), "red");
        } else if (i == 1) EXPECT_STREQ(css_value_identifier_name(value->data.function->args[0]), "Blue");
        else if (i == 2) {
            ASSERT_EQ(value->type, CSS_VALUE_TYPE_LIST);
            EXPECT_STREQ(css_value_identifier_name(value->data.list.values[0]), "Red");
            EXPECT_STREQ(css_value_identifier_name(value->data.list.values[2]), "red");
        } else if (i == 3) {
            ASSERT_EQ(value->type, CSS_VALUE_TYPE_LIST); EXPECT_TRUE(value->data.list.comma_separated);
            EXPECT_STREQ(css_value_identifier_name(value->data.list.values[0]->data.list.values[0]), "Blue");
            EXPECT_STREQ(css_value_identifier_name(value->data.list.values[1]->data.list.values[0]), "blue");
        } else EXPECT_STREQ(css_value_identifier_name(value), "Red");
    }
    const char* keywords[] = {"display: BLOCK", "page: AUTO", "string-set: NONE"};
    const CssEnum expected[] = {CSS_VALUE_BLOCK, CSS_VALUE_AUTO, CSS_VALUE_NONE};
    for (size_t i = 0; i < 3; i++) {
        CssDeclaration* declaration = css_parse_declaration_text(keywords[i], strlen(keywords[i]), pool); ASSERT_NE(declaration, nullptr);
        ASSERT_NE(declaration->value, nullptr); ASSERT_EQ(declaration->value->type, CSS_VALUE_TYPE_KEYWORD);
        EXPECT_EQ(declaration->value->data.keyword, expected[i]);
    }
    mem_pool_destroy(pool);
}

TEST(TypesetTest, OrderedGlueAndSignedPenaltiesRemainDistinctFromLegality) {
    TypesetItem items[5] = {};
    items[0].kind = TYPESET_BOX; items[0].metrics = {20.0f, 8.0f, 2.0f, 8.0f, {}, nullptr};
    items[1].kind = TYPESET_GLUE; items[1].glue = {5.0f, 2.0f, 1.0f, 0, 0, true, true};
    items[2].kind = TYPESET_BOX; items[2].metrics = items[0].metrics;
    items[3].kind = TYPESET_GLUE; items[3].glue = {5.0f, 1.0f, 2.0f, 2, 0, true, true};
    items[4].kind = TYPESET_BOX; items[4].metrics = items[0].metrics;
    TypesetPacking stretch = typeset_pack_glue(items, 0, 5, 70.0f, 100.0f);
    EXPECT_EQ(stretch.order, 2);
    EXPECT_FLOAT_EQ(typeset_glue_advance(&items[1].glue, &stretch), 5.0f);
    EXPECT_FLOAT_EQ(typeset_glue_advance(&items[3].glue, &stretch), 35.0f);
    TypesetPacking shrink = typeset_pack_glue(items, 0, 5, 70.0f, 65.0f);
    EXPECT_FLOAT_EQ(shrink.ratio, -1.0f);
    EXPECT_FLOAT_EQ(shrink.residual, -2.0f);
    items[1].boundary = {TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_LINE, -1000, 0};
    items[3].boundary = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_LINE, -10, 0};
    TypesetParagraph paragraph = {items, 5, 10.0f, nullptr, nullptr, nullptr};
    TypesetLineCandidate scratch[5] = {}, line = {};
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 50.0f, scratch, 5, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 4u);
    EXPECT_EQ(line.penalty, -10);
    ASSERT_EQ(typeset_next_line(&paragraph, line.next, 30.0f, scratch, 5, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 5u);
}

TEST(TypesetTest, BeforeBoundariesExcludeUnconsumedMetricsAndKeepTheirNativePenalty) {
    TypesetItem items[3] = {};
    items[0].metrics = {20.0f, 8.0f, 2.0f, 8.0f, {}, nullptr};
    items[1].metrics = {40.0f, 42.0f, 8.0f, 42.0f, {}, nullptr};
    items[2].metrics = items[0].metrics; items[2].metrics.advance = 15.0f;
    items[1].has_before = true; items[1].before = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_LINE, -23, 71};
    items[2].has_before = true; items[2].before = {TYPESET_BREAK_FORCED, TYPESET_BREAK_LINE, -99, 72};
    TypesetParagraph paragraph = {items, 3, 10.0f, nullptr, nullptr, nullptr, nullptr, 8.0f};
    TypesetLineCandidate scratch[3] = {}, line = {};
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 30.0f, scratch, 3, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 1u); EXPECT_EQ(line.paint_end, 1u);
    EXPECT_FLOAT_EQ(line.width, 20.0f); EXPECT_FLOAT_EQ(line.height, 10.0f); EXPECT_FLOAT_EQ(line.depth, 2.0f);
    EXPECT_EQ(line.penalty, -23); EXPECT_FALSE(line.forced);
    ASSERT_EQ(typeset_next_line(&paragraph, 1, 60.0f, scratch, 3, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 2u); EXPECT_EQ(line.paint_end, 2u); EXPECT_TRUE(line.forced);
    EXPECT_FLOAT_EQ(line.height, 50.0f); EXPECT_EQ(line.penalty, -99);
    ASSERT_EQ(typeset_next_line(&paragraph, 2, 30.0f, scratch, 3, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 3u); EXPECT_FALSE(line.forced); EXPECT_FLOAT_EQ(line.height, 10.0f);
}

TEST(TypesetTest, BeforeBoundariesPreserveGlueTrimmingAndCandidateCapacity) {
    TypesetItem items[3] = {};
    items[0].metrics = {20.0f, 8.0f, 2.0f, 8.0f, {}, nullptr};
    items[1].kind = TYPESET_GLUE; items[1].glue = {5.0f, 0.0f, 0.0f, 0, 0, true, true};
    items[2].metrics = items[0].metrics;
    items[2].has_before = true; items[2].before = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_LINE, 0, 0};
    TypesetParagraph paragraph = {items, 3, 10.0f, nullptr, nullptr, nullptr};
    TypesetLineCandidate scratch[2] = {}, line = {};
    EXPECT_EQ(typeset_line_alternatives(&paragraph, 0, 30.0f, scratch, 2, nullptr), 2u);
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 30.0f, scratch, 2, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 2u); EXPECT_EQ(line.paint_end, 1u); EXPECT_FLOAT_EQ(line.width, 20.0f);
    items[2].before.legality = TYPESET_BREAK_FORCED;
    EXPECT_EQ(typeset_line_alternatives(&paragraph, 0, 30.0f, scratch, 2, nullptr), 1u);
    EXPECT_TRUE(scratch[0].forced); EXPECT_EQ(scratch[0].next, 2u);
    items[2].before.scope = TYPESET_BREAK_PAGE;
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 30.0f, scratch, 2, &line), TYPESET_OK);
    EXPECT_FALSE(line.forced);
}

TEST(TypesetTest, ConditionalGlueRunsTrimBeforeUnpaintedBreaksAndRetainNativePaintPayloads) {
    TypesetItem items[4] = {};
    items[0].metrics = {20.0f, 8.0f, 2.0f, 8.0f, {}, nullptr};
    for (size_t i = 1; i < 3; i++) {
        items[i].kind = TYPESET_GLUE; items[i].glue = {5.0f, 2.0f, 1.0f, 0, 0, false, true};
        items[i].boundary.legality = TYPESET_BREAK_FORBIDDEN;
    }
    items[3].kind = TYPESET_PENALTY; items[3].boundary = {TYPESET_BREAK_FORCED, TYPESET_BREAK_LINE, -17, 0};
    TypesetParagraph paragraph = {items, 4, 10.0f, nullptr, nullptr, nullptr}; TypesetLineCandidate scratch[4] = {}, line = {};
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 25.0f, scratch, 4, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 4u); EXPECT_EQ(line.paint_end, 1u); EXPECT_FLOAT_EQ(line.width, 20.0f);
    EXPECT_EQ(line.penalty, -17); EXPECT_TRUE(line.forced); EXPECT_FALSE(line.overflow);
    TypesetRecord paint = {}; items[3].paint = lam::up(&paint);
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 35.0f, scratch, 4, &line), TYPESET_OK);
    EXPECT_EQ(line.paint_end, 4u); EXPECT_FLOAT_EQ(line.width, 30.0f);
    EXPECT_EQ(items[3].paint.get(), &paint); EXPECT_FLOAT_EQ(items[1].glue.natural, 5.0f);
}

static size_t alternate_lines(const TypesetParagraph*, size_t first, float,
        TypesetLineCandidate* candidates, size_t capacity, void*) {
    if (capacity < 2) return 2;
    candidates[0] = {}; candidates[0].first = first; candidates[0].next = 1;
    candidates[0].paint_end = 1; candidates[0].width = 10.0f; candidates[0].height = 12.0f; candidates[0].cost = 1.0;
    candidates[1] = candidates[0]; candidates[1].next = candidates[1].paint_end = 2; candidates[1].cost = 20.0;
    return 2;
}

TEST(TypesetTest, NativeProviderSuppliesAlternativesToDifferentPolicies) {
    TypesetItem items[2] = {};
    TypesetParagraph paragraph = {items, 2, 12.0f, alternate_lines, typeset_choose_lowest_cost, nullptr};
    TypesetLineCandidate scratch[2] = {}, line = {};
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 100.0f, scratch, 2, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 1u);
    paragraph.choose = typeset_choose_furthest_line;
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 100.0f, scratch, 2, &line), TYPESET_OK);
    EXPECT_EQ(line.next, 2u);
    EXPECT_EQ(typeset_next_line(&paragraph, 0, 100.0f, scratch, 1, &line), TYPESET_BUDGET_EXHAUSTED);
}

static bool width_relative_native_metrics(const TypesetParagraph*, size_t, float width,
        TypesetMetrics* metrics, void*) {
    *metrics = {width * 0.5f, width * 0.25f, 0.0f, width * 0.25f, {}, nullptr};
    return true;
}

TEST(TypesetTest, WidthSpecificMetricsDoNotChangeSourceItemsAndKeepTheStrutDescent) {
    TypesetItem item = {}; item.kind = TYPESET_BOX; item.metrics.advance = 17.0f;
    TypesetParagraph paragraph = {&item, 1, 12.0f, nullptr, nullptr, nullptr};
    paragraph.measure = width_relative_native_metrics; paragraph.minimum_baseline = 9.0f;
    TypesetLineCandidate scratch[1] = {}, wide = {}, narrow = {};
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 120.0f, scratch, 1, &wide), TYPESET_OK);
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 80.0f, scratch, 1, &narrow), TYPESET_OK);
    EXPECT_FLOAT_EQ(wide.width, 60.0f); EXPECT_FLOAT_EQ(wide.height, 33.0f); EXPECT_FLOAT_EQ(wide.baseline, 30.0f);
    EXPECT_FLOAT_EQ(narrow.width, 40.0f); EXPECT_FLOAT_EQ(narrow.height, 23.0f); EXPECT_FLOAT_EQ(narrow.baseline, 20.0f);
    EXPECT_FLOAT_EQ(item.metrics.advance, 17.0f); EXPECT_EQ(item.paint, nullptr);
}

static TypesetStatus copy_resume(void*, const TypesetResume* cursor, TypesetResume* saved) {
    *saved = *cursor; return TYPESET_OK;
}

struct NativeRegionFixture : TypesetRecord { uint64_t scaled_points; size_t lines; float line_height; };
static TypesetStatus native_region_measure(void* context, const TypesetResume* start,
        const TypesetRegionConstraints* constraints, bool split, Pool*, TypesetRegionSlice* slice) {
    NativeRegionFixture* native = (NativeRegionFixture*)context;
    size_t first = start->state[0], count = 0;
    while (first + count < native->lines && (count + 1) * native->line_height <= constraints->available_height) {
        count++;
        if (split && constraints->minimum) break;
    }
    if (constraints->retain_tail && first + count == native->lines) {
        if (!split || count < 2) return TYPESET_UNPLACEABLE;
        count--;
    }
    if (!count || (!split && first + count != native->lines)) return TYPESET_UNPLACEABLE;
    *slice = {};
    slice->end = *start; slice->end.serial += count; slice->end.state[0] += count;
    slice->metrics.height = count * native->line_height;
    slice->metrics.exact = lam::up((const TypesetRecord*)native);
    slice->paint = lam::up((const TypesetRecord*)native);
    slice->complete = first + count == native->lines;
    return TYPESET_OK;
}

static TypesetRegionMaterial native_region_material(NativeRegionFixture* record, uint64_t generation,
        uint64_t identity, bool split = false) {
    TypesetRegionMaterial material = {};
    material.identity = identity;
    material.source = {record->provider, generation, identity, TYPESET_PROVIDER_OFFSETS, lam::up((const TypesetRecord*)record)};
    material.start = {record->provider, generation, 0, {0, 0, 0, 0}};
    material.context = record; material.measure = native_region_measure; material.split = split;
    return material;
}

TEST(TypesetTest, RegionIdentityIncludesTheProducerNamespace) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.region.namespaces"); ASSERT_NE(pool, nullptr);
    NativeRegionFixture records[] = {{{81}, 111, 1, 10}, {{82}, 222, 1, 10}};
    TypesetRegionMaterial materials[] = {native_region_material(&records[0], 1, 7), native_region_material(&records[1], 1, 7)};
    const TypesetRegionMaterial* anchors[] = {&materials[0], &materials[1]};
    TypesetRegionQueue queue = {pool, nullptr, 0, nullptr, 0, 0, 8};
    TypesetRegionConstraints constraints = {100, 80, 1}; TypesetRegionPlan plan = {};
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 2, &constraints, 0, 0, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 2u); EXPECT_EQ(plan.placements[0].slice.metrics.exact.get(), &records[0]);
    EXPECT_EQ(plan.placements[1].slice.metrics.exact.get(), &records[1]);
    typeset_region_plan_dispose(&plan); materials[1].source.provider = materials[1].start.provider = 81;
    EXPECT_EQ(typeset_region_plan(&queue, anchors, 2, &constraints, 0, 0, &plan), TYPESET_INVALID);
    EXPECT_EQ(queue.count, 0u); typeset_region_plan_dispose(&plan); mem_pool_destroy(pool);
}

TEST(TypesetTest, NoteReservationIsTransactionalAndSharesSpaceAcrossAnchors) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.regions");
    ASSERT_NE(pool, nullptr);
    NativeRegionFixture records[] = {{{51}, UINT64_C(9007199254740993), 5, 10.0f}, {{51}, 123456789, 2, 10.0f}};
    TypesetRegionMaterial materials[2] = {};
    for (size_t i = 0; i < 2; i++) materials[i] = native_region_material(&records[i], 8, i + 1, true);
    TypesetRegionQueue queue = {pool, nullptr, 0, nullptr, 0, 0, 8};
    const TypesetRegionMaterial* anchors[] = {&materials[0], &materials[1]};
    TypesetRegionConstraints constraints = {100.0f, 60.0f, 1, nullptr, false};
    TypesetRegionPlan rejected = {}, chosen = {}, stale = {};
    EXPECT_EQ(typeset_region_plan(&queue, anchors, 2, &constraints, 45.0f, 5.0f, &rejected), TYPESET_UNPLACEABLE);
    EXPECT_EQ(queue.count, 0u); EXPECT_EQ(queue.version, 0u); EXPECT_EQ(rejected.scratch, nullptr);
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 2, &constraints, 20.0f, 5.0f, &chosen), TYPESET_OK);
    ASSERT_EQ(chosen.count, 2u);
    EXPECT_EQ(chosen.placements[0].slice.end.state[0], 2u);
    EXPECT_EQ(chosen.placements[1].slice.end.state[0], 1u);
    EXPECT_FLOAT_EQ(chosen.reserved_height, 35.0f);
    EXPECT_FLOAT_EQ(chosen.placements[0].rect.y, 30.0f);
    EXPECT_EQ(chosen.placements[0].slice.metrics.exact.get(), &records[0]);
    EXPECT_EQ(records[0].scaled_points, UINT64_C(9007199254740993));
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 2, &constraints, 20.0f, 5.0f, &stale), TYPESET_OK);
    ASSERT_EQ(typeset_region_commit(&queue, &chosen), TYPESET_OK);
    EXPECT_EQ(typeset_region_commit(&queue, &stale), TYPESET_STALE);
    EXPECT_EQ(queue.count, 2u); EXPECT_EQ(queue.entries[0].cursor.state[0], 2u);
    typeset_region_plan_dispose(&chosen); typeset_region_plan_dispose(&stale);
    constraints.page_number++;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 5.0f, &chosen), TYPESET_OK);
    EXPECT_EQ(chosen.count, 2u); EXPECT_EQ(chosen.pending_count, 0u);
    ASSERT_EQ(typeset_region_commit(&queue, &chosen), TYPESET_OK);
    EXPECT_EQ(queue.count, 0u);
    typeset_region_plan_dispose(&chosen);
    mem_pool_destroy(pool);
}

TEST(TypesetTest, OccupiedRegionFloorsAndSignedSeparatorsPreserveMaterialMetrics) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.region.floor"); ASSERT_NE(pool, nullptr);
    NativeRegionFixture record = {{61}, UINT64_C(9007199254740993), 1, 10.0f};
    TypesetRegionMaterial material = native_region_material(&record, 2, 11);
    TypesetRegionQueue queue = {pool, nullptr, 0, nullptr, 0, 0, 8};
    const TypesetRegionMaterial* anchors[] = {&material};
    TypesetRegionConstraints constraints = {100.0f, 60.0f, 1, nullptr, false};
    constraints.minimum_height = 45.0f; constraints.defer_anchors = true;
    TypesetRegionPlan plan = {};
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 1, &constraints, 20.0f, 5.0f, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 0u); EXPECT_EQ(plan.pending_count, 1u); EXPECT_FLOAT_EQ(plan.reserved_height, 0.0f);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    constraints.page_number++;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 5.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 1u); EXPECT_FLOAT_EQ(plan.reserved_height, 45.0f);
    EXPECT_FLOAT_EQ(plan.placements[0].rect.y, 20.0f); EXPECT_FLOAT_EQ(plan.placements[0].rect.height, 10.0f);
    EXPECT_EQ(plan.placements[0].slice.metrics.exact.get(), &record);
    EXPECT_EQ(record.scaled_points, UINT64_C(9007199254740993)); typeset_region_plan_dispose(&plan);
    constraints.minimum_height = 0.0f;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 54.0f, -4.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 1u); EXPECT_FLOAT_EQ(plan.reserved_height, 6.0f);
    EXPECT_FLOAT_EQ(plan.placements[0].rect.y, 50.0f); EXPECT_FLOAT_EQ(plan.placements[0].rect.height, 10.0f);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    constraints.minimum_height = 45.0f;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 5.0f, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 0u); EXPECT_FLOAT_EQ(plan.reserved_height, 0.0f);
    typeset_region_plan_dispose(&plan); mem_pool_destroy(pool);
}

TEST(TypesetTest, ContinuationsHoldAnUnsplittableTailInSourceOrder) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.region.tail");
    ASSERT_NE(pool, nullptr);
    NativeRegionFixture records[] = {{{61}, 1, 4, 10.0f}, {{61}, 2, 4, 10.0f}};
    TypesetRegionMaterial materials[2] = {};
    TypesetRegionPending* pending = (TypesetRegionPending*)pool_calloc(pool, 2 * sizeof(TypesetRegionPending));
    ASSERT_NE(pending, nullptr);
    for (size_t i = 0; i < 2; i++) {
        materials[i] = native_region_material(&records[i], 9, i + 1);
        pending[i] = {&materials[i], materials[i].start};
    }
    TypesetRegionQueue queue = {pool, nullptr, 0, pending, 2, 2, 8};
    TypesetRegionConstraints constraints = {100.0f, 60.0f, 2, nullptr, false};
    TypesetRegionPlan plan = {};
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 5.0f, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 1u); ASSERT_EQ(plan.pending_count, 1u);
    EXPECT_EQ(plan.pending[0].material, &materials[1]); EXPECT_EQ(plan.pending[0].cursor.serial, 0u);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 5.0f, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 1u); EXPECT_EQ(plan.pending_count, 0u);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    EXPECT_EQ(queue.count, 0u);
    mem_pool_destroy(pool);
}

TEST(TypesetTest, RegionRollbackRetainsExactPayloadsAndInvalidatesRejectedPlans) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.region.rollback"); ASSERT_NE(pool, nullptr);
    NativeRegionFixture record = {{61}, UINT64_C(9007199254740993), 5, 10.0f};
    TypesetRegionMaterial material = native_region_material(&record, 9, 1, true);
    TypesetRegionPending pending = {&material, material.start, 1};
    TypesetRegionQueue queue = {pool, nullptr, 0, &pending, 1, 1, 8};
    TypesetRegionConstraints constraints = {100.0f, 40.0f, 1};
    TypesetRegionPlan plan = {}, retained = {}, rejected = {};
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 10.0f, 0.0f, &plan), TYPESET_OK);
    ASSERT_EQ(typeset_region_plan_retain(&plan, &retained), TYPESET_OK);
    const TypesetRecord* payload = retained.placements[0].slice.metrics.exact;
    TypesetRegionCheckpoint checkpoint = {};
    ASSERT_EQ(typeset_region_checkpoint(&queue, pool, &checkpoint), TYPESET_OK);
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 0.0f, &rejected), TYPESET_OK);
    ASSERT_EQ(typeset_region_commit(&queue, &rejected), TYPESET_OK);
    EXPECT_EQ(queue.entries[0].cursor.state[0], 4u);
    typeset_region_plan_dispose(&plan);
    ASSERT_EQ(typeset_region_restore(&checkpoint, &retained), TYPESET_OK);
    ASSERT_EQ(queue.count, 1u); EXPECT_EQ(queue.entries[0].cursor.state[0], 0u);
    EXPECT_GT(queue.version, rejected.version);
    EXPECT_EQ(typeset_region_commit(&queue, &rejected), TYPESET_STALE);
    EXPECT_EQ(retained.placements[0].slice.metrics.exact.get(), payload);
    EXPECT_EQ(record.scaled_points, UINT64_C(9007199254740993));
    ASSERT_EQ(typeset_region_commit(&queue, &retained), TYPESET_OK);
    EXPECT_EQ(queue.entries[0].cursor.state[0], 3u);
    EXPECT_EQ(typeset_region_restore(&checkpoint), TYPESET_INVALID);
    typeset_region_plan_dispose(&retained); typeset_region_plan_dispose(&rejected); mem_pool_destroy(pool);
}

TEST(TypesetTest, DeferredFloatsKeepTheirIdentityAndEarliestPageUntilTheyFit) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.deferred"); ASSERT_NE(pool, nullptr);
    NativeRegionFixture records[] = {{{71}, 111, 3, 10.0f}, {{71}, 222, 3, 10.0f}};
    TypesetRegionMaterial materials[2] = {};
    const TypesetRegionMaterial* anchors[2] = {};
    for (size_t i = 0; i < 2; i++) {
        materials[i] = native_region_material(&records[i], 10, i + 1); materials[i].delay_pages = 1;
        anchors[i] = &materials[i];
    }
    TypesetRegionQueue queue = {pool, nullptr, 0, nullptr, 0, 0, 8};
    TypesetRegionConstraints constraints = {100.0f, 60.0f, 1, nullptr, false, true, true};
    TypesetRegionPlan plan = {};
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 2, &constraints, 40.0f, 0.0f, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 0u); EXPECT_EQ(plan.pending_count, 2u);
    EXPECT_EQ(plan.pending[0].earliest_page, 2u);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    constraints.page_number = 2;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 40.0f, 0.0f, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 0u); EXPECT_EQ(plan.pending[0].cursor.serial, 0u);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    constraints.page_number = 3;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 0.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 2u); EXPECT_EQ(plan.pending_count, 0u);
    EXPECT_EQ(plan.placements[0].material->identity, 1u); EXPECT_EQ(plan.placements[1].material->identity, 2u);
    EXPECT_FLOAT_EQ(plan.placements[0].rect.y, 0.0f); EXPECT_FLOAT_EQ(plan.placements[1].rect.y, 30.0f);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    EXPECT_EQ(queue.count, 0u); mem_pool_destroy(pool);
}

TEST(TypesetTest, RegionClearanceHoldsMaterialDespiteSpareCapacity) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.clearance"); ASSERT_NE(pool, nullptr);
    NativeRegionFixture records[] = {{{81}, 111, 1, 10.0f}, {{81}, 222, 1, 10.0f}};
    TypesetRegionMaterial materials[] = {native_region_material(&records[0], 11, 1), native_region_material(&records[1], 11, 2)};
    materials[1].clear_before = true;
    const TypesetRegionMaterial* anchors[] = {&materials[0], &materials[1]};
    TypesetRegionQueue queue = {pool, nullptr, 0, nullptr, 0, 0, 8};
    TypesetRegionConstraints constraints = {100.0f, 80.0f, 1, nullptr, false, true, true};
    TypesetRegionPlan plan = {};
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 2, &constraints, 12.0f, 0.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 1u); ASSERT_EQ(plan.pending_count, 1u);
    EXPECT_EQ(plan.pending[0].material->identity, 2u); EXPECT_EQ(plan.pending[0].cursor.serial, 0u);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    constraints.page_number = 2;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 12.0f, 0.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 1u); EXPECT_EQ(plan.placements[0].material->identity, 2u);
    EXPECT_EQ(plan.pending_count, 0u);
    typeset_region_plan_dispose(&plan); mem_pool_destroy(pool);
}

TEST(TypesetTest, DeferrableNoteAnchorsSurviveAZeroBudgetWithoutHidingImpossibleFreshPages) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.note.defer"); ASSERT_NE(pool, nullptr);
    NativeRegionFixture record = {{91}, UINT64_C(9007199254740993), 1, 10.0f};
    TypesetRegionMaterial material = native_region_material(&record, 12, 1, true);
    const TypesetRegionMaterial* anchors[] = {&material};
    TypesetRegionQueue queue = {pool, nullptr, 0, nullptr, 0, 0, 8};
    TypesetRegionConstraints constraints = {100.0f, 0.0f, 1, nullptr, false, false, false, 80.0f, true};
    TypesetRegionPlan plan = {};
    EXPECT_EQ(typeset_region_plan(&queue, anchors, 1, &constraints, 0.0f, 0.0f, &plan), TYPESET_UNPLACEABLE);
    material.defer_anchor = true;
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 1, &constraints, 0.0f, 0.0f, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 0u); ASSERT_EQ(plan.pending_count, 1u);
    EXPECT_EQ(plan.pending[0].cursor.serial, 0u); EXPECT_EQ(plan.pending[0].material, &material);
    EXPECT_FLOAT_EQ(plan.reserved_height, 0.0f);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    constraints.occupied = false; constraints.page_number = 2;
    EXPECT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 0.0f, &plan), TYPESET_UNPLACEABLE);
    constraints.available_height = 80.0f;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 0.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 1u); EXPECT_EQ(plan.placements[0].slice.metrics.exact.get(), &record);
    EXPECT_EQ(plan.pending_count, 0u); typeset_region_plan_dispose(&plan); mem_pool_destroy(pool);
}

TEST(TypesetTest, RetainedTailPlansPreserveNativeExactMaterialAndReinsertLegalContinuations) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.reinsert-tail"); ASSERT_NE(pool, nullptr);
    NativeRegionFixture record = {{91}, UINT64_C(9007199254740993), 4, 10.0f};
    TypesetRegionMaterial material = native_region_material(&record, 12, 1, true);
    const TypesetRegionMaterial* anchors[] = {&material};
    TypesetRegionQueue queue = {pool, nullptr, 0, nullptr, 0, 0, 8};
    TypesetRegionConstraints constraints = {100.0f, 60.0f, 1}; constraints.retain_tail = true;
    TypesetRegionPlan plan = {}, retained = {};
    ASSERT_EQ(typeset_region_plan(&queue, anchors, 1, &constraints, 0.0f, 0.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 1u); ASSERT_EQ(plan.pending_count, 1u);
    EXPECT_FLOAT_EQ(plan.reserved_height, 30); EXPECT_EQ(plan.pending[0].cursor.serial, 3u);
    EXPECT_EQ(plan.placements[0].slice.metrics.exact.get(), &record);
    ASSERT_EQ(typeset_region_plan_retain(&plan, &retained), TYPESET_OK); typeset_region_plan_dispose(&plan);
    EXPECT_EQ(((const NativeRegionFixture*)retained.placements[0].slice.metrics.exact.get())->scaled_points, UINT64_C(9007199254740993));
    ASSERT_EQ(typeset_region_commit(&queue, &retained), TYPESET_OK); typeset_region_plan_dispose(&retained);
    ASSERT_EQ(queue.count, 1u); EXPECT_EQ(queue.entries[0].material, &material);
    constraints.page_number = 2; constraints.retain_tail = false;
    ASSERT_EQ(typeset_region_plan(&queue, nullptr, 0, &constraints, 0.0f, 0.0f, &plan), TYPESET_OK);
    ASSERT_EQ(plan.count, 1u); EXPECT_TRUE(plan.placements[0].slice.complete); EXPECT_EQ(plan.pending_count, 0u);
    EXPECT_EQ(plan.placements[0].start.serial, 3u); EXPECT_FLOAT_EQ(plan.reserved_height, 10);
    ASSERT_EQ(typeset_region_commit(&queue, &plan), TYPESET_OK); typeset_region_plan_dispose(&plan);
    material.split = false; constraints.retain_tail = true;
    EXPECT_EQ(typeset_region_plan(&queue, anchors, 1, &constraints, 0.0f, 0.0f, &plan), TYPESET_UNPLACEABLE);
    EXPECT_EQ(plan.scratch, nullptr); EXPECT_EQ(queue.count, 0u);
    mem_pool_destroy(pool);
}

TEST(TypesetTest, RunningMarksSelectPageValuesAndRestoreDiscardedTrials) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.marks");
    ASSERT_NE(pool, nullptr);
    TypesetMarkStore store = {41, 7, pool, nullptr, 0, 0};
    TypesetRecord native = {41};
    TypesetMark mark = {};
    mark.kind = TYPESET_MARK_NATIVE; mark.name = "chapter"; mark.value = lam::up(&native);
    mark.source = {41, 7, 99, TYPESET_PROVIDER_OFFSETS, nullptr};
    mark.text = "Alpha"; mark.page_number = 1; mark.at_page_start = true;
    ASSERT_EQ(typeset_mark_append(&store, &mark), TYPESET_OK);
    TypesetMarkCheckpoint saved = typeset_marks_checkpoint(&store);
    mark.text = "Discarded"; mark.page_number = 2; mark.at_page_start = false;
    ASSERT_EQ(typeset_mark_append(&store, &mark), TYPESET_OK);
    EXPECT_STREQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 2, TYPESET_MARK_FIRST)->text, "Discarded");
    EXPECT_STREQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 2, TYPESET_MARK_START)->text, "Alpha");
    ASSERT_EQ(typeset_marks_restore(&store, saved), TYPESET_OK);
    EXPECT_STREQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 3, TYPESET_MARK_LAST)->text, "Alpha");
    mark.text = "Beta"; mark.page_number = 2; mark.at_page_start = true;
    ASSERT_EQ(typeset_mark_append(&store, &mark), TYPESET_OK);
    mark.text = "Gamma"; mark.at_page_start = false;
    ASSERT_EQ(typeset_mark_append(&store, &mark), TYPESET_OK);
    EXPECT_STREQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 2, TYPESET_MARK_FIRST)->text, "Beta");
    EXPECT_STREQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 2, TYPESET_MARK_START)->text, "Beta");
    EXPECT_STREQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 2, TYPESET_MARK_LAST)->text, "Gamma");
    EXPECT_EQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 2, TYPESET_MARK_FIRST_EXCEPT), nullptr);
    EXPECT_EQ(typeset_mark_select(&store, TYPESET_MARK_NATIVE, "chapter", 3, TYPESET_MARK_FIRST)->value.get(), &native);
    mark.source.generation++;
    EXPECT_EQ(typeset_mark_append(&store, &mark), TYPESET_STALE);
    saved.generation++;
    EXPECT_EQ(typeset_marks_restore(&store, saved), TYPESET_STALE);
    mem_pool_destroy(pool);
}

TEST(TypesetTest, CounterSnapshotsOutliveMutableScopesAndKeepLongNestedValues) {
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_LAYOUT, "test.counter.mutable"); ASSERT_NE(arena, nullptr);
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.counter.frozen"); ASSERT_NE(pool, nullptr);
    CounterContext* counters = counter_context_create(arena); ASSERT_NE(counters, nullptr);
    counter_reset(counters, "Chapter 7 chapter 8");
    for (size_t i = 0; i < 300; i++) { counter_push_scope(counters); counter_reset(counters, "Chapter 2"); }
    CounterSnapshot* snapshot = counter_snapshot_create(counters, pool); ASSERT_NE(snapshot, nullptr);
    counter_increment(counters, "Chapter 4"); EXPECT_EQ(counter_get_value(counters, "Chapter"), 6);
    counter_context_destroy(counters); mem_arena_destroy(arena);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    ASSERT_TRUE(counter_snapshot_append(snapshot, "Chapter", ".", CSS_VALUE_DECIMAL, text));
    EXPECT_EQ(text->length, 601u); EXPECT_EQ(text->str[0], '7');
    for (size_t i = 1; i < text->length; i++) EXPECT_EQ(text->str[i], i % 2 ? '.' : '2');
    strbuf_reset(text);
    ASSERT_TRUE(counter_snapshot_append(snapshot, "chapter", nullptr, CSS_VALUE_DECIMAL, text)); EXPECT_STREQ(text->str, "8");
    strbuf_reset(text);
    ASSERT_TRUE(counter_snapshot_append(snapshot, "Missing", nullptr, CSS_VALUE_DECIMAL, text)); EXPECT_STREQ(text->str, "0");
    strbuf_free(text); mem_pool_destroy(pool);
}
TEST(TypesetTest, StyleCounterScopesKeepReadsAndIsolateNestedMutations) {
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_LAYOUT, "test.counter.containment"); ASSERT_NE(arena, nullptr);
    CounterContext* counters = counter_context_create(arena); ASSERT_NE(counters, nullptr);
    counter_reset(counters, "N 7"); counters->quote_depth = 2;
    {
        CounterStyleScope boundary; ASSERT_TRUE(boundary.enter(counters, true));
        EXPECT_EQ(counter_get_value(counters, "N"), 7);
        ASSERT_TRUE(counter_push_scope(counters));
        counter_reset(counters, "Other 3");
        counter_increment(counters, "N 2");
        counter_pop_scope_propagate(counters, true, true);
        EXPECT_EQ(counter_get_value(counters, "N"), 2);
        char values[64] = {};
        EXPECT_GT(counters_format(counters, "N", ".", CSS_VALUE_DECIMAL, values, sizeof(values)), 0);
        EXPECT_STREQ(values, "7.2");
        {
            CounterStyleScope nested; ASSERT_TRUE(nested.enter(counters, true));
            counter_set(counters, "N 5"); counters->quote_depth = 4;
            EXPECT_EQ(counter_get_value(counters, "N"), 5);
        }
        EXPECT_EQ(counter_get_value(counters, "N"), 2);
        EXPECT_EQ(counters->quote_depth, 2);
        counters->quote_depth = 1;
    }
    EXPECT_EQ(counter_get_value(counters, "N"), 7);
    EXPECT_EQ(counter_get_value(counters, "Other"), 0);
    EXPECT_EQ(counters->quote_depth, 2);
    counter_context_destroy(counters); mem_arena_destroy(arena);
}

TEST(TypesetTest, CounterMeasurementCheckpointsRestoreValuesResetsAndUnfinishedFrames) {
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_LAYOUT, "test.counter.checkpoint"); ASSERT_NE(arena, nullptr);
    CounterContext* counters = counter_context_create(arena); ASSERT_NE(counters, nullptr);
    counter_reset(counters, "N 7"); counters->quote_depth = 2;
    CounterScope* original = counters->current_scope; size_t frames = counters->frame_stack->size();
    Arena* scratch_arena = mem_arena_create(nullptr, MEM_ROLE_LAYOUT, "test.counter.checkpoint.scratch");
    ASSERT_NE(scratch_arena, nullptr);
    ScratchArena scratch; scratch_init(&scratch, scratch_arena);
    {
        ScratchScope storage(&scratch); CounterCheckpoint checkpoint;
        ASSERT_TRUE(checkpoint.enter(counters, &scratch, &storage.mark));
        counter_increment(counters, "N 3 New 9"); counters->quote_depth = 4;
        ASSERT_TRUE(counter_push_scope(counters)); counter_reset(counters, "N 20");
        counter_pop_scope_propagate(counters, true, true);
        EXPECT_EQ(counter_get_value(counters, "N"), 20);
        ASSERT_TRUE(counter_push_scope(counters));
    }
    EXPECT_EQ(counters->current_scope.get(), original);
    EXPECT_EQ(counters->frame_stack->size(), frames);
    EXPECT_EQ(counter_get_value(counters, "N"), 7);
    EXPECT_EQ(counter_get_value(counters, "New"), 0);
    EXPECT_EQ(counters->quote_depth, 2);
    counter_context_destroy(counters); mem_arena_destroy(scratch_arena); mem_arena_destroy(arena);
}

TEST(TypesetTest, CounterListsKeepEveryImplicitValue) {
    Arena* arena = mem_arena_create(nullptr, MEM_ROLE_LAYOUT, "test.counter.implicit"); ASSERT_NE(arena, nullptr);
    CounterContext* counters = counter_context_create(arena); ASSERT_NE(counters, nullptr);
    counter_reset(counters, "A 4 B 5 C 6");
    counter_reset(counters, "A B C");
    for (const char* name : {"A", "B", "C"}) EXPECT_EQ(counter_get_value(counters, name), 0);
    counter_increment(counters, "A B C");
    for (const char* name : {"A", "B", "C"}) EXPECT_EQ(counter_get_value(counters, name), 1);
    counter_set(counters, "A B C");
    for (const char* name : {"A", "B", "C"}) EXPECT_EQ(counter_get_value(counters, name), 0);
    counter_context_destroy(counters); mem_arena_destroy(arena);
}

static TypesetStatus advance_flow(void*, const TypesetResume* cursor,
        TypesetContribution* contribution, TypesetResume* next) {
    *next = *cursor; next->serial++; contribution->kind = TYPESET_CONTRIBUTION_INSERTION; return TYPESET_OK;
}
static size_t choose_page(void*, const TypesetPageCandidate*, size_t) { return 0; }
static TypesetAssemblyAction hold_page(void*, const TypesetPageCandidate*) { return TYPESET_ASSEMBLY_HOLD; }

TEST(TypesetTest, CheckpointsAndAssemblyDoNotCommitProviderSideEffects) {
    TypesetFlowProvider provider = {77, 3, nullptr, advance_flow, copy_resume, copy_resume};
    TypesetResume cursor = {77, 3, 4, {1, 2, 3, 4}}, saved = {}, next = {}, restored = {};
    TypesetContribution contribution = {};
    ASSERT_EQ(typeset_flow_checkpoint(&provider, &cursor, &saved), TYPESET_OK);
    ASSERT_EQ(typeset_flow_next(&provider, &cursor, &contribution, &next), TYPESET_OK);
    EXPECT_EQ(next.serial, 5u);
    ASSERT_EQ(typeset_flow_restore(&provider, &saved, &restored), TYPESET_OK);
    EXPECT_EQ(restored.serial, 4u);
    TypesetPageCandidate page = {}; page.start = cursor; page.end = next;
    TypesetPagePolicy policy = {nullptr, choose_page, hold_page, nullptr};
    TypesetAssemblyAction action = TYPESET_ASSEMBLY_FINALIZE;
    size_t selected = SIZE_MAX;
    ASSERT_EQ(typeset_page_select(&policy, &page, 1, &selected, &action), TYPESET_OK);
    EXPECT_EQ(action, TYPESET_ASSEMBLY_HOLD);
    page.boundary.legality = TYPESET_BREAK_FORBIDDEN;
    EXPECT_EQ(typeset_page_select(&policy, &page, 1, &selected, &action), TYPESET_INVALID);
    cursor.generation++;
    EXPECT_EQ(typeset_flow_next(&provider, &cursor, &contribution, &next), TYPESET_STALE);
}

struct NativePageFixture {
    size_t expanded, committed;
    size_t marks, pending;
    TypesetAssemblyAction action;
    TypesetStatus failure, commit_failure;
    NativeRegionFixture native;
};
static TypesetStatus native_page_next(void* context, const TypesetResume* cursor,
        TypesetContribution* contribution, TypesetResume* next) {
    NativePageFixture* fixture = (NativePageFixture*)context;
    if (cursor->state[0] == 4) return TYPESET_DONE;
    if (fixture->failure != TYPESET_OK && cursor->state[0] == 2) return fixture->failure;
    fixture->expanded++;
    fixture->marks++; fixture->pending++;
    *contribution = {}; contribution->kind = TYPESET_CONTRIBUTION_BOX;
    contribution->metrics.height = 10.0f;
    fixture->native.provider = cursor->provider;
    fixture->native.scaled_points = UINT64_C(9007199254740993);
    contribution->metrics.exact = lam::up((const TypesetRecord*)&fixture->native);
    contribution->boundary = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_PAGE,
        cursor->state[0] == 1 ? -50 : 0, 0};
    *next = *cursor; next->serial++; next->state[0]++; next->state[1] = fixture->expanded;
    next->state[2] = fixture->marks; next->state[3] = fixture->pending;
    return TYPESET_OK;
}
static TypesetStatus native_page_restore(void* context, const TypesetResume* cursor, TypesetResume* restored) {
    ((NativePageFixture*)context)->expanded = cursor->state[1];
    ((NativePageFixture*)context)->marks = cursor->state[2];
    ((NativePageFixture*)context)->pending = cursor->state[3];
    *restored = *cursor; return TYPESET_OK;
}
static size_t native_page_choose(void*, const TypesetPageCandidate* candidates, size_t count) {
    size_t selected = 0;
    for (size_t i = 1; i < count; i++) if (candidates[i].cost < candidates[selected].cost) selected = i;
    return selected;
}
static TypesetAssemblyAction native_page_assemble(void* context, const TypesetPageCandidate*) {
    return ((NativePageFixture*)context)->action;
}
static TypesetStatus native_page_committed(void* context, const TypesetPageCandidate*) {
    NativePageFixture* fixture = (NativePageFixture*)context;
    if (fixture->commit_failure != TYPESET_OK) return fixture->commit_failure;
    fixture->committed++; return TYPESET_OK;
}

TEST(TypesetTest, PageBuilderRestoresRejectedExpansionAndCommitsSelectedCheckpointOnce) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.page"); ASSERT_NE(pool, nullptr);
    NativePageFixture fixture = {};
    TypesetFlowProvider provider = {81, 9, &fixture, native_page_next, copy_resume, native_page_restore};
    TypesetPagePolicy policy = {&fixture, native_page_choose, native_page_assemble, native_page_committed};
    TypesetResume cursor = {81, 9, 0, {}}, next = {};
    TypesetPageConstraints constraints = {35.0f, 8, 8}; TypesetPagePlan plan = {};
    ASSERT_EQ(typeset_page_plan(&provider, &cursor, &constraints, nullptr, &policy, pool, &plan), TYPESET_OK);
    EXPECT_EQ(plan.count, 2u); EXPECT_FALSE(plan.complete);
    EXPECT_FLOAT_EQ(plan.candidate.body_height, 20.0f);
    EXPECT_EQ(fixture.expanded, 0u); EXPECT_EQ(fixture.committed, 0u);
    EXPECT_EQ(fixture.marks, 0u); EXPECT_EQ(fixture.pending, 0u);
    EXPECT_EQ(plan.contributions[0].metrics.exact.get(), &fixture.native);
    EXPECT_EQ(fixture.native.scaled_points, UINT64_C(9007199254740993));
    ASSERT_EQ(typeset_page_commit(&plan, &next), TYPESET_OK);
    EXPECT_EQ(next.state[0], 2u); EXPECT_EQ(fixture.expanded, 2u); EXPECT_EQ(fixture.committed, 1u);
    EXPECT_EQ(fixture.marks, 2u); EXPECT_EQ(fixture.pending, 2u);
    EXPECT_EQ(typeset_page_commit(&plan, &next), TYPESET_NO_PROGRESS); EXPECT_EQ(fixture.committed, 1u);
    mem_pool_destroy(pool);
}

TEST(TypesetTest, HeldReinsertedFailedAndBudgetedPageTrialsLeaveTheProviderUnchanged) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.page.rollback"); ASSERT_NE(pool, nullptr);
    NativePageFixture fixture = {};
    TypesetFlowProvider provider = {82, 9, &fixture, native_page_next, copy_resume, native_page_restore};
    TypesetPagePolicy policy = {&fixture, native_page_choose, native_page_assemble, native_page_committed};
    TypesetResume cursor = {82, 9, 0, {}}, next = {};
    TypesetPageConstraints constraints = {35.0f, 8, 8};
    for (TypesetAssemblyAction action : {TYPESET_ASSEMBLY_HOLD, TYPESET_ASSEMBLY_REINSERT}) {
        fixture.action = action; TypesetPagePlan plan = {};
        ASSERT_EQ(typeset_page_plan(&provider, &cursor, &constraints, nullptr, &policy, pool, &plan), TYPESET_OK);
        EXPECT_EQ(plan.action, action); EXPECT_EQ(typeset_page_commit(&plan, &next), TYPESET_NO_PROGRESS);
        EXPECT_EQ(fixture.expanded, 0u); EXPECT_EQ(fixture.committed, 0u);
    }
    fixture.action = TYPESET_ASSEMBLY_FINALIZE; fixture.failure = TYPESET_STALE;
    TypesetPagePlan failed = {};
    EXPECT_EQ(typeset_page_plan(&provider, &cursor, &constraints, nullptr, &policy, pool, &failed), TYPESET_STALE);
    EXPECT_EQ(fixture.expanded, 0u); EXPECT_EQ(failed.scratch, nullptr);
    fixture.failure = TYPESET_OK; constraints.max_candidates = 1;
    EXPECT_EQ(typeset_page_plan(&provider, &cursor, &constraints, nullptr, &policy, pool, &failed), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(fixture.expanded, 0u); EXPECT_EQ(fixture.committed, 0u);
    EXPECT_EQ(fixture.marks, 0u); EXPECT_EQ(fixture.pending, 0u);
    mem_pool_destroy(pool);
}

TEST(TypesetTest, FailedCommitAndStalePlansCannotPublishTrialState) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.page.commit"); ASSERT_NE(pool, nullptr);
    NativePageFixture fixture = {};
    TypesetFlowProvider provider = {83, 9, &fixture, native_page_next, copy_resume, native_page_restore};
    TypesetPagePolicy policy = {&fixture, native_page_choose, native_page_assemble, native_page_committed};
    TypesetResume cursor = {83, 9, 0, {}}, next = {};
    TypesetPageConstraints constraints = {35.0f, 8, 8}; TypesetPagePlan plan = {};
    ASSERT_EQ(typeset_page_plan(&provider, &cursor, &constraints, nullptr, &policy, pool, &plan), TYPESET_OK);
    fixture.commit_failure = TYPESET_OUT_OF_MEMORY;
    EXPECT_EQ(typeset_page_commit(&plan, &next), TYPESET_OUT_OF_MEMORY);
    EXPECT_FALSE(plan.committed); EXPECT_EQ(fixture.expanded, 0u); EXPECT_EQ(fixture.committed, 0u);
    EXPECT_EQ(fixture.marks, 0u); EXPECT_EQ(fixture.pending, 0u);
    fixture.commit_failure = TYPESET_OK; provider.generation++;
    EXPECT_EQ(typeset_page_commit(&plan, &next), TYPESET_STALE);
    EXPECT_EQ(fixture.expanded, 0u); EXPECT_EQ(fixture.committed, 0u);
    mem_pool_destroy(pool);
}

struct LateClosureFixture { DomElement* prelude; DomElement* moved; DomElement* note; DomElement* floating; };

TEST(ResourceAdmissionTest, NormalizedNetworkSchemesDoNotDenyLocalOrEmbeddedSources) {
    const char* local[] = {"assets/image.png", "./font.woff2", "../sheet.css", "file:///local/script.js",
        "data:text/plain,inline", "builtin:wpt-testharness.js"};
    for (const char* source : local) {
        SCOPED_TRACE(source);
        EXPECT_TRUE(input_resource_policy_admits(INPUT_RESOURCE_LOCAL_ONLY, source));
    }
    const char* remote[] = {"http://example.test/a.js", "HTTPS://example.test/a.css", "ftp://example.test/image.png",
        "ftps://example.test/font.ttf", "ws://example.test/socket", "wss://example.test/socket"};
    for (const char* source : remote) {
        SCOPED_TRACE(source);
        EXPECT_FALSE(input_resource_policy_admits(INPUT_RESOURCE_LOCAL_ONLY, source));
        EXPECT_TRUE(input_resource_policy_admits(INPUT_RESOURCE_ALLOW_NETWORK, source));
    }
    EXPECT_FALSE(input_resource_policy_admits(INPUT_RESOURCE_LOCAL_ONLY, nullptr));
}

TEST(ResourceAdmissionTest, DocumentAndChildHostPoliciesAreIndependent) {
    DomDocument parent, sibling, child;
    EXPECT_EQ(parent.resource_policy, INPUT_RESOURCE_ALLOW_NETWORK);
    DocumentJsHostConfig config = {}; config.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
    document_apply_js_host_config(&parent, &config);
    DocumentJsHostConfig inherited = document_js_host_config_inherit(nullptr, &parent);
    document_apply_js_host_config(&child, &inherited);
    EXPECT_EQ(child.resource_policy, INPUT_RESOURCE_LOCAL_ONLY);
    EXPECT_EQ(sibling.resource_policy, INPUT_RESOURCE_ALLOW_NETWORK);
}

TEST(ResourceAdmissionTest, ASharedWarmImageCacheCannotBypassTheSelectedDocumentPolicy) {
    UiContext ui = {}; DomDocument allowed, denied;
    denied.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
    const char* remote = "http://example.test/retained.png";
    allowed.url = lam::own(url_parse(remote)); denied.url = lam::own(url_parse(remote));
    ASSERT_NE(allowed.url, nullptr); ASSERT_NE(denied.url, nullptr);
    ui.document = lam::up(&allowed);
    ImageSurface* pixels = image_surface_create(4, 4); ASSERT_NE(pixels, nullptr);
    ASSERT_EQ(image_cache_adopt(&ui, remote, pixels), pixels);
    EXPECT_EQ(load_document_image(&denied, &ui, remote), nullptr);
    EXPECT_EQ(load_document_image(&allowed, &ui, remote), pixels);
    EXPECT_EQ(load_document_image(&denied, &ui, remote), nullptr);
    EXPECT_EQ(load_document_image(&denied, &ui, "/retained.png"), nullptr);
    image_cache_cleanup(&ui);
    url_destroy(allowed.url); allowed.url = nullptr;
    url_destroy(denied.url); denied.url = nullptr;
}

class SecondaryViewTest : public ::testing::Test {
protected:
    Pool* input_pool = nullptr;
    Input* input = nullptr;
    DomDocument doc;
    DomElement* source = nullptr;
    bool vector_engine = false;

    void SetUp() override {
        input_pool = pool_create();
        ASSERT_NE(input_pool, nullptr);
        input = Input::create(input_pool);
        ASSERT_NE(input, nullptr);
        ASSERT_TRUE(doc.init(input));
        MarkBuilder builder(input);
        Element* backing = builder.element("p").final().element;
        ASSERT_NE(backing, nullptr);
        source = DomElement::create(&doc, "p", backing);
        ASSERT_NE(source, nullptr);
        doc.root = lam::up(source);
        source->x = 11.25f;
        source->y = 23.5f;
        source->width = 640.0f;
        source->height = 91.75f;
        doc.view_tree = view_tree_shell_create(&doc);
        ASSERT_NE(doc.view_tree, nullptr);
        doc.view_tree->init((MemContext*)doc.services.mem_ctx);
        doc.view_tree->root = lam::up((View*)source);
    }

    void TearDown() override {
        view_tree_secondary_release_all(&doc);
        view_tree_shell_destroy(&doc, doc.view_tree);
        doc.destroy();
        pool_destroy(input_pool);
        if (vector_engine) rdt_engine_term();
    }

    void init_vector_engine() {
        if (!vector_engine) { rdt_engine_init(0); vector_engine = true; }
    }

    ViewTree* secondary(ViewPresentation presentation = VIEW_PRESENTATION_PAGED) {
        ViewEnvironment environment = view_environment_default(presentation);
        ViewTree* tree = view_tree_secondary_create(&doc, &environment);
        EXPECT_NE(tree, nullptr);
        return tree;
    }

    void stylesheet(const char* text) {
        CssEngine* engine = css_engine_create(doc.document_pool);
        ASSERT_NE(engine, nullptr);
        doc.services.cached_css_engine = engine;
        css_engine_set_viewport(engine, 1000.0, 700.0);
        doc.stylesheets = lam::own_arr((CssStylesheet**)pool_calloc(doc.document_pool, sizeof(CssStylesheet*)));
        ASSERT_NE(doc.stylesheets, nullptr);
        doc.stylesheets.get()[0] = css_parse_stylesheet(engine, text, nullptr);
        ASSERT_NE(doc.stylesheets.get()[0], nullptr);
        doc.stylesheet_count = doc.stylesheet_capacity = 1;
    }

    void pages(ViewTree* tree, size_t count, ViewPageSide first = VIEW_PAGE_RIGHT) {
        for (size_t i = 0; i < count; i++) {
            ViewPageSide side = i % 2 ? (first == VIEW_PAGE_RIGHT ? VIEW_PAGE_LEFT : VIEW_PAGE_RIGHT) : first;
            ASSERT_NE(view_tree_page_append(tree, 100.0f, 200.0f,
                {10.0f, 20.0f, 80.0f, 160.0f}, side), nullptr);
        }
        ASSERT_TRUE(view_tree_model_commit(tree));
    }

    void preview_document() {
        // these embedding fixtures have no UiContext to own the vector-engine lifecycle.
        init_vector_engine();
        stylesheet("@page { size: 120px 160px; margin: 20px; @bottom-center { content: counter(page) } } "
            "p { margin: 0; font: 12px/16px Arial, sans-serif } div { height: 80px } div + div { break-before: page }");
        const char* labels[] = {"Page one", "Page two", "Page three", "Page four", "Page five"};
        const char* styles[] = {"background: #ff0000", "background: #00ff00", "background: #0000ff",
            "background: #ff8000", "background: #8000ff"};
        for (size_t i = 0; i < 5; i++) ASSERT_NE(block(labels[i], styles[i]), nullptr);
    }

    DomElement* block(const char* text, const char* css = nullptr, const char* tag = "div", DomElement* parent = nullptr) {
        MarkBuilder builder(input);
        DomElement* element = DomElement::create(&doc, tag, builder.element(tag).final().element);
        if (!element || !(parent ? parent : source)->DomNode::append_child(element)) return nullptr;
        if (css && !element->set_attribute("style", css)) return nullptr;
        if (text) {
            DomText* content = DomText::create_copy(text, strlen(text), element);
            if (!content || !element->DomNode::append_child(content)) return nullptr;
        }
        return element;
    }
    DomElement* image(const char* css = nullptr, DomElement* parent = nullptr, const char* url = nullptr);
    DomElement* identified_block(const char* id, DomElement* parent = nullptr) {
        DomElement* element = block(nullptr, nullptr, "div", parent);
        return element && element->set_attribute("id", id) ? element : nullptr;
    }
    DomElement* page_control(const char* tag, DomElement* parent = nullptr, const char* css = nullptr) {
        if (!source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE)) return nullptr;
        return block(nullptr, css, tag, parent);
    }
    DomElement* page_master(const char* name, const char* css) {
        DomElement* master = page_control("r:page-master", nullptr, css);
        return master && master->set_attribute("name", name) ? master : nullptr;
    }
    DomElement* page_sequence(const char* master, const char* initial = "auto", const char* end = "no-force") {
        DomElement* sequence = page_control("r:page-sequence");
        return sequence && sequence->set_attribute("master-reference", master) &&
            sequence->set_attribute("initial-page-number", initial) && sequence->set_attribute("force-page-count", end) ? sequence : nullptr;
    }
    DomElement* master_choice(DomElement* run, const char* master, const char* position = "any",
            const char* parity = "any", const char* blank = "any") {
        DomElement* choice = page_control("r:master-rule", run);
        return choice && choice->set_attribute("master-reference", master) && choice->set_attribute("page-position", position) &&
            choice->set_attribute("odd-or-even", parity) && choice->set_attribute("blank-or-not-blank", blank) ? choice : nullptr;
    }
    DomElement* page_region(DomElement* master, const char* role, const char* name,
            const char* extent = nullptr, const char* css = nullptr) {
        DomElement* region = page_control("r:region", master, css);
        return region && region->set_attribute("role", role) && region->set_attribute("name", name) &&
            (!extent || region->set_attribute("extent", extent)) ? region : nullptr;
    }
    DomElement* static_content(DomElement* sequence, const char* name, const char* text = nullptr) {
        DomElement* binding = page_control("r:static-content", sequence);
        return binding && binding->set_attribute("region-name", name) &&
            (!text || block(text, nullptr, "div", binding)) ? binding : nullptr;
    }
    DomElement* formatting_root(const char* xml) {
        XmlParseOptions options = {true, true, nullptr, nullptr};
        parse_xml_with_options(input, xml, &options);
        if (input->parse_failed) return nullptr;
        ElementReader root = ElementReader(input->root).childAt(0).asElement();
        return root.isValid() ? build_dom_tree_from_element(const_cast<Element*>(root.element()), &doc, nullptr) : nullptr;
    }
    DomElement* install_fo_translation(RadiantFoTranslation* translated) {
        if (!translated || translated->diagnostic.status != TYPESET_OK) return nullptr;
        DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr);
        if (!generated || !radiant_page_set_origins(&doc, translated->origins)) return nullptr;
        doc.root = lam::up(generated); return generated;
    }
    DomElement* terminal_sequence(const char* normal_css, const char* last_css, bool ordinary = true) {
        if (!page_master("normal", normal_css) || !page_master("last", last_css)) return nullptr;
        DomElement* program = page_control("r:sequence-master");
        if (!program || !program->set_attribute("name", "chapter")) return nullptr;
        DomElement* run = page_control("r:master-run", program);
        if (!run || !master_choice(run, "last", "last") || (ordinary && !master_choice(run, "normal"))) return nullptr;
        return page_sequence("chapter");
    }
    DomElement* authored_note(DomElement* parent, const char* call_text, const char* body_text,
            DomElement** call = nullptr, DomElement** body = nullptr) {
        DomElement* note = page_control("r:note", parent);
        DomElement* call_node = note ? block(call_text, nullptr, "r:note-call", note) : nullptr;
        DomElement* body_node = call_node ? page_control("r:note-body", note) : nullptr;
        if (!body_node || !block(body_text, nullptr, "div", body_node)) return nullptr;
        if (call) *call = call_node;
        if (body) *body = body_node;
        return note;
    }
    DomElement* svg_image(const char* attributes, const char* css = nullptr, DomElement* parent = nullptr);
    DomElement* fixed_page(DomElement* container, const char* width, const char* height) {
        DomElement* page = page_control("r:fixed-page", container);
        return page && page->set_attribute("width", width) && page->set_attribute("height", height) ? page : nullptr;
    }
    bool table_rows(DomElement* section, size_t count, const char* a = "Body A",
            const char* b = "Body B", const char* first_colspan = nullptr) {
        for (size_t i = 0; i < count; i++) {
            DomElement* row = block(nullptr, nullptr, "tr", section);
            if (!row || !block(a, nullptr, "td", row) || !block(b, nullptr, "td", row)) return false;
            if (first_colspan && !row->first_child->as_element()->set_attribute("colspan", first_colspan)) return false;
        }
        return true;
    }
    DomElement* fixed_table(size_t rows, DomElement** header = nullptr, DomElement** footer = nullptr,
            const char* first_colspan = nullptr) {
        DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-collapse: separate; border-spacing: 0", "table");
        if (!table) return nullptr;
        const char* tags[] = {"thead", "tfoot", "tbody"};
        for (size_t group = 0; group < 3; group++) {
            DomElement* section = block(nullptr, group == 0 ? "background-color: #cce0ff" :
                group == 1 ? "background-color: #ddffdd" : nullptr, tags[group], table);
            if (!section) return nullptr;
            if (!group && header) *header = section;
            if (group == 1 && footer) *footer = section;
            size_t count = group == 2 ? rows : 1;
            if (!table_rows(section, count, group == 0 ? "Head A" : group == 1 ? "Foot A" : "Body A",
                group == 0 ? "Head B" : group == 1 ? "Foot B" : "Body B", first_colspan)) return nullptr;
        }
        return table;
    }
    DomElement* rowspan_table(DomElement** spanning, DomElement** lower) {
        DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
        DomElement* group = table ? block(nullptr, nullptr, "tbody", table) : nullptr;
        DomElement* first = group ? block(nullptr, nullptr, "tr", group) : nullptr;
        *spanning = first ? block("Span", nullptr, "td", first) : nullptr;
        if (!*spanning || !(*spanning)->set_attribute("rowspan", "2") || !block("Upper", nullptr, "td", first)) return nullptr;
        DomElement* second = block(nullptr, nullptr, "tr", group);
        *lower = second ? block("Lower", nullptr, "td", second) : nullptr;
        return *lower ? table : nullptr;
    }
    DomText* table_cell_text(DomElement* cell, const char* text) {
        while (cell->first_child) if (!cell->DomNode::remove_child(cell->first_child)) return nullptr;
        DomText* content = DomText::create_copy(text, strlen(text), cell);
        return content && cell->DomNode::append_child(content) ? content : nullptr;
    }
    bool late_closure_fixture(LateClosureFixture* fixture, bool floating = false) {
        stylesheet("@page { size: 220px 100px; margin: 10px; @top-center { content: string(Title); font-size: 7px } } "
            "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 } "
            "p { counter-reset: N } .call::before { content: counter(N) ': ' }");
        fixture->prelude = block("Prelude", "height: 20px; string-set: Title 'Prelude'");
        fixture->moved = block(nullptr, floating ? "padding-bottom: 16px; break-inside: avoid; counter-increment: N; string-set: Title 'Moved'" :
            "padding-bottom: 20px; break-inside: avoid; counter-increment: N; string-set: Title 'Moved'");
        if (!fixture->prelude || !fixture->moved || !fixture->moved->set_attribute("id", "moved")) return false;
        DomElement* call = block("Call ", nullptr, "div", fixture->moved);
        if (!call || !call->set_attribute("class", "call")) return false;
        fixture->note = block("Note A\nNote B", "float: footnote; footnote-policy: line; color: blue", "span", call);
        fixture->floating = floating ? block("Float", "float: top; float-reference: page; color: red", "div", fixture->moved) : nullptr;
        return fixture->note && (!floating || fixture->floating) &&
            block(nullptr, floating ? "padding-bottom: 16px" : "padding-bottom: 20px", "div", fixture->moved);
    }
    void reference_width_boundary(size_t chapters, size_t label_offset = 0);
    void native_publishing_session(bool nested);
};
static LayoutViewNode* source_fragment(ViewTree* tree, DomNode* source, ViewFragmentRole role, bool box, bool last = false);
static LayoutViewNode* source_glyph_text(ViewTree* tree, DomNode* source, const char* text);
static uint32_t occurrence_page(const LayoutViewNode* occurrence);
static void append_fragment_text(const LayoutViewNode* node, StrBuf* text);
static uint32_t snapshot_pixel(const ImageSurface* surface, size_t x, size_t y);
static void expect_same_page_pixels(ViewTree* left, ViewTree* right, uint32_t number);
static void expect_same_surface_pixels(const ImageSurface* left, const ImageSurface* right);

static const char* paged_split_png = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAKCAYAAAC0VX7mAAAAKUlEQVR4Aa3BMQEAAAiAMKR/Z63gwTYLy8OwfEhMYhKTmMQkJjGJSewAawgDEgrHt2EAAAAASUVORK5CYII=";

DomElement* SecondaryViewTest::image(const char* css, DomElement* parent, const char* url) {
    DomElement* element = block(nullptr, css, "img", parent);
    return element && element->set_attribute("src", url ? url : paged_split_png) ? element : nullptr;
}

TEST_F(SecondaryViewTest, PictureSelectionPrecedesFallbackAndPreservesDataUrlCommas) {
    DomElement* picture = block(nullptr, nullptr, "picture"); ASSERT_NE(picture, nullptr);
    DomElement* candidate = block(nullptr, nullptr, "source", picture); ASSERT_NE(candidate, nullptr);
    ASSERT_TRUE(candidate->set_attribute("srcset", "data:image/svg+xml,%3Csvg/%3E 1x"));
    DomElement* img = image(nullptr, picture, "fallback.png"); ASSERT_NE(img, nullptr);
    lam::Temp<char> selected(layout_resolve_replaced_image_source(img));
    ASSERT_NE(selected.get(), nullptr);
    EXPECT_STREQ(selected.get(), "data:image/svg+xml,%3Csvg/%3E");
}

TEST_F(SecondaryViewTest, ResponsiveCandidatesNormalizeSizesAndRecoverInvalidDescriptors) {
    stylesheet("p { margin: 0 }");
    CssEngine* engine = (CssEngine*)doc.services.cached_css_engine;
    DomElement* img = image(nullptr, nullptr, "fallback.png"); ASSERT_NE(img, nullptr);
    const struct { const char* srcset; const char* sizes; float width, desired; const char* url; float density; } cases[] = {
        {"small.png 1x, large.png 2x", "", 300, 2, "large.png", 2},
        {"large.png 2x", "", 300, 1, "fallback.png", 1},
        {"small.png 200w, large.png 600w", "(max-width: 400px) 100vw, 50vw", 300, 2, "large.png", 2},
        {"small.png 200w, large.png 600w", "(max-width: 400px) 100vw, 50vw", 800, 1, "large.png", 1.5f},
        {"small.png 200w, large.png 600w", "calc(100vw - 100px)", 300, 1, "small.png", 1},
        {"bad.png 1w 2x, also-bad.png -2x, good.png 2x", "", 300, 2, "good.png", 2},
        {"bad.png fn(a,b), good.png 2x", "", 300, 2, "good.png", 2},
        {"first.png 2x, duplicate.png 2x", "", 300, 2, "first.png", 2},
        {"bad.png +2x, good.png .5e1x", "", 300, 5, "good.png", 5},
        {"small.png 200w, large.png 600w", "20%, 200px", 300, 1, "small.png", 1},
    };
    for (const auto& row : cases) {
        SCOPED_TRACE(row.srcset);
        SCOPED_TRACE(row.sizes);
        css_engine_set_viewport(engine, row.width, 200);
        engine->context.device_pixel_ratio = row.desired;
        ASSERT_TRUE(img->set_attribute("srcset", row.srcset)); ASSERT_TRUE(img->set_attribute("sizes", row.sizes));
        float density = 0.0f;
        lam::Temp<char> selected(layout_resolve_replaced_image_source(img, engine, &density));
        ASSERT_NE(selected.get(), nullptr); EXPECT_STREQ(selected.get(), row.url); EXPECT_FLOAT_EQ(density, row.density);
    }
}

TEST_F(SecondaryViewTest, PictureSourcesUseTheSelectedEnvironmentAndOnlyPrecedingSupportedSources) {
    stylesheet("p { margin: 0 }"); CssEngine* engine = (CssEngine*)doc.services.cached_css_engine;
    DomElement* picture = block(nullptr, nullptr, "picture"); ASSERT_NE(picture, nullptr);
    DomElement* unsupported = block(nullptr, nullptr, "source", picture); ASSERT_NE(unsupported, nullptr);
    ASSERT_TRUE(unsupported->set_attribute("type", "image/avif")); ASSERT_TRUE(unsupported->set_attribute("srcset", "unsupported.avif"));
    DomElement* print = block(nullptr, nullptr, "source", picture); ASSERT_NE(print, nullptr);
    ASSERT_TRUE(print->set_attribute("media", "print")); ASSERT_TRUE(print->set_attribute("type", "IMAGE/PNG"));
    ASSERT_TRUE(print->set_attribute("srcset", "print.png 2x")); ASSERT_TRUE(print->set_attribute("width", "40"));
    DomElement* img = image(nullptr, picture, "fallback.png"); ASSERT_NE(img, nullptr);
    DomElement* late = block(nullptr, nullptr, "source", picture); ASSERT_NE(late, nullptr);
    ASSERT_TRUE(late->set_attribute("srcset", "late.png"));
    for (bool printing : {true, false, true}) {
        engine->context.print_media = printing; float density = 0; DomElement* dimensions = nullptr;
        lam::Temp<char> selected(layout_resolve_replaced_image_source(img, engine, &density, &dimensions));
        EXPECT_STREQ(selected.get(), printing ? "print.png" : "fallback.png");
        EXPECT_EQ(dimensions, printing ? print : img); EXPECT_FLOAT_EQ(density, printing ? 2.0f : 1.0f);
    }
}

DomElement* SecondaryViewTest::svg_image(const char* attributes, const char* css, DomElement* parent) {
    StrBuf* url = strbuf_new();
    if (!url) return nullptr;
    strbuf_append_str(url, "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' ");
    strbuf_append_str(url, attributes);
    strbuf_append_str(url, "%3E%3Crect width='100%25' height='100%25' fill='red'/%3E%3C/svg%3E");
    DomElement* result = image(css, parent, url->str);
    strbuf_free(url); return result;
}

static LayoutViewNode* source_image_fragment(ViewTree* tree, DomNode* source) {
    ViewNodeState* state = view_tree_node_state(tree, source, false);
    for (LayoutViewNode* node = state ? state->first_occurrence.get() : nullptr; node; node = node->next_occurrence)
        if (node->image_box) return node;
    return nullptr;
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

TEST_F(SecondaryViewTest, NativeLabelBodyGridRemeasuresEachPageAndRetainsTheExactSeparatedCells) {
    init_vector_engine();
    stylesheet("@page{size:200px 100px;margin:10px}@page :left{size:160px 100px}"
        "table{width:100%;border-spacing:0;border-collapse:separate}tr{height:60px}"
        "td{padding:0;vertical-align:top;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    ASSERT_TRUE(source->set_attribute("r:provisional-distance-between-starts", "25%"));
    ASSERT_TRUE(source->set_attribute("r:provisional-label-separation", "5%"));
    DomElement* labels[3] = {}; DomElement* bodies[3] = {}; DomElement* items[3] = {};
    for (size_t i = 0; i < 3; i++) {
        items[i] = block(nullptr, nullptr, "table"); ASSERT_NE(items[i], nullptr);
        ASSERT_TRUE(items[i]->set_attribute("r:label-body-grid", "true"));
        DomElement* row = block(nullptr, nullptr, "tr", items[i]); ASSERT_NE(row, nullptr);
        labels[i] = block("A", "background:red", "td", row); ASSERT_NE(labels[i], nullptr);
        bodies[i] = block("Body", "background:blue", "td", row); ASSERT_NE(bodies[i], nullptr);
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    for (size_t i = 0; i < 3; i++) {
        LayoutViewNode* label = source_fragment(tree, labels[i], VIEW_FRAGMENT_BODY, true); ASSERT_NE(label, nullptr);
        LayoutViewNode* body = source_fragment(tree, bodies[i], VIEW_FRAGMENT_BODY, true); ASSERT_NE(body, nullptr);
        float width = i == 1 ? 140.0f : 180.0f;
        EXPECT_EQ(occurrence_page(label), i + 1); EXPECT_FLOAT_EQ(label->rect.x, 10.0f);
        EXPECT_FLOAT_EQ(label->rect.width, width * 0.2f); EXPECT_FLOAT_EQ(body->rect.x, 10.0f + width * 0.25f);
        EXPECT_FLOAT_EQ(body->rect.width, width * 0.75f);
    }
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(items[0]->set_attribute("r:provisional-label-separation", "40%")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "label/body geometry overlaps or leaves a nonpositive part width");
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* pixels = render_secondary_page_snapshot(preview, 2, 1.0f); ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(snapshot_pixel(pixels, 15, 30), 0xff0000ffu); EXPECT_EQ(snapshot_pixel(pixels, 40, 30), 0xffffffffu);
    EXPECT_EQ(snapshot_pixel(pixels, 50, 30), 0xffff0000u); image_surface_destroy(pixels);
}

TEST_F(SecondaryViewTest, NativeLabelBodyLengthsInheritTheirOriginalComputedFont) {
    stylesheet("@page{size:200px 100px;margin:10px}table{width:100%;border-spacing:0;border-collapse:separate;font:20px/24px Arial}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* list = block(nullptr, "font:10px/12px Arial", "div"); ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->set_attribute("r:provisional-distance-between-starts", "3em"));
    ASSERT_TRUE(list->set_attribute("r:provisional-label-separation", "0.5em"));
    DomElement* item = block(nullptr, nullptr, "table", list); ASSERT_NE(item, nullptr);
    ASSERT_TRUE(item->set_attribute("r:label-body-grid", "true"));
    DomElement* row = block(nullptr, nullptr, "tr", item); ASSERT_NE(row, nullptr);
    DomElement* label = block("A", nullptr, "td", row); ASSERT_NE(label, nullptr); ASSERT_NE(block("Body", nullptr, "td", row), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewCssStyle* style = view_css_resolve(tree, item); ASSERT_NE(style, nullptr); ASSERT_NE(style->label_body, nullptr);
    EXPECT_FLOAT_EQ(style->font.font_size, 20.0f); EXPECT_FLOAT_EQ(style->label_body->distance.value, 30.0f);
    EXPECT_FLOAT_EQ(style->label_body->separation.value, 5.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, label, VIEW_FRAGMENT_BODY, true)->rect.width, 25.0f);
    ASSERT_TRUE(label->set_attribute("r:label-body-grid", "true")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "label/body grid requires a table formatting context");
}

TEST_F(SecondaryViewTest, NativeLabelBodyContextBindsBeforeItemPropertyOverrides) {
    stylesheet("@page{size:200px 100px;margin:10px}table{width:100%;border-spacing:0;border-collapse:separate;font:20px/24px Arial}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* list = block(nullptr, "font:10px/12px Arial", "div"); ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->set_attribute("r:label-body-context", "true"));
    ASSERT_TRUE(list->set_attribute("r:provisional-distance-between-starts", "3em"));
    ASSERT_TRUE(list->set_attribute("r:provisional-label-separation", "0.5em"));
    DomElement* item = block(nullptr, nullptr, "table", list); ASSERT_NE(item, nullptr);
    ASSERT_TRUE(item->set_attribute("r:label-body-grid", "true"));
    ASSERT_TRUE(item->set_attribute("r:provisional-distance-between-starts", "100px"));
    ASSERT_TRUE(item->set_attribute("r:provisional-label-separation", "10%"));
    DomElement* row = block(nullptr, nullptr, "tr", item); ASSERT_NE(row, nullptr);
    DomElement* label = block("A", nullptr, "td", row); ASSERT_NE(label, nullptr);
    DomElement* body = block("Body", nullptr, "td", row); ASSERT_NE(body, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, item); ASSERT_NE(style, nullptr);
    EXPECT_FLOAT_EQ(style->label_body->distance.value, 100.0f);
    EXPECT_EQ(radiant_label_body_geometry(style), view_css_resolve(tree, list)->label_body.get());
    EXPECT_FLOAT_EQ(source_fragment(tree, label, VIEW_FRAGMENT_BODY, true)->rect.width, 25.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, body, VIEW_FRAGMENT_BODY, true)->rect.x, 40.0f);
}

TEST_F(SecondaryViewTest, NativeLabelBodyGridRejectsUnsupportedShapesWithoutPublishingPages) {
    stylesheet("@page{size:200px 100px;margin:10px}table{width:100%;border-spacing:0;border-collapse:separate}td{padding:0;vertical-align:top;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    const char* reasons[] = {"label/body grid requires one two-cell row without columns, captions or grid spacing",
        "label/body grid requires one two-cell row without columns, captions or grid spacing",
        "label/body grid requires one two-cell row without columns, captions or grid spacing",
        "label/body grid horizontal decoration requires reference-relative indent geometry"};
    DomElement* previous = nullptr;
    for (size_t i = 0; i < 4; i++) {
        SCOPED_TRACE(i); if (previous) ASSERT_TRUE(previous->set_attribute("style", "display:none"));
        DomElement* item = block(nullptr, i == 2 ? "border-spacing:2px" : i == 3 ? "padding-left:2px" : nullptr, "table"); ASSERT_NE(item, nullptr);
        ASSERT_TRUE(item->set_attribute("r:label-body-grid", "true")); previous = item;
        DomElement* row = block(nullptr, nullptr, "tr", item); ASSERT_NE(row, nullptr);
        DomElement* label = block("A", nullptr, "td", row); ASSERT_NE(label, nullptr);
        if (i) ASSERT_NE(block("Body", nullptr, "td", row), nullptr);
        if (i == 1) ASSERT_TRUE(label->set_attribute("colspan", "2"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_STREQ(diagnostic.reason, reasons[i]); EXPECT_EQ(tree->model->page_count, 0u);
    }
}

TEST_F(SecondaryViewTest, NativeLabelBodyContinuationRetainsEverySourceByteOnceAcrossChangingWidths) {
    stylesheet("@page{size:200px 100px;margin:10px}@page :left{size:160px 100px}"
        "table{width:100%;border-spacing:0;border-collapse:separate}td{padding:0;vertical-align:top}"
        "div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* item = block(nullptr, nullptr, "table"); ASSERT_NE(item, nullptr);
    ASSERT_TRUE(item->set_attribute("r:label-body-grid", "true")); ASSERT_TRUE(item->set_attribute("r:provisional-distance-between-starts", "25%"));
    DomElement* row = block(nullptr, nullptr, "tr", item); ASSERT_NE(row, nullptr);
    DomElement* label = block(nullptr, nullptr, "td", row); ASSERT_NE(label, nullptr);
    DomElement* label_text = block("Term", nullptr, "div", label); ASSERT_NE(label_text, nullptr);
    DomElement* body = block(nullptr, nullptr, "td", row); ASSERT_NE(body, nullptr);
    DomElement* body_text = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ", nullptr, "div", body); ASSERT_NE(body_text, nullptr);
    ASSERT_TRUE(label->set_attribute("id", "term")); ASSERT_TRUE(label->set_attribute("r:area-source", "descendants"));
    ASSERT_TRUE(body->set_attribute("id", "definition")); ASSERT_TRUE(body->set_attribute("r:area-source", "descendants"));
    DomElement* citations = block(nullptr, nullptr, "div"); ASSERT_NE(citations, nullptr);
    DomElement* term_page = block(nullptr, nullptr, "r:folio-ref", citations); ASSERT_NE(term_page, nullptr);
    ASSERT_TRUE(term_page->set_attribute("ref-id", "term")); ASSERT_TRUE(term_page->set_attribute("edge", "last"));
    DomElement* body_page = block(nullptr, nullptr, "r:folio-ref", citations); ASSERT_NE(body_page, nullptr);
    ASSERT_TRUE(body_page->set_attribute("ref-id", "definition")); ASSERT_TRUE(body_page->set_attribute("edge", "last"));

    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 3u);
    for (DomElement* element : {label_text, body_text}) {
        DomText* source_text = element->first_child->as_text(); ASSERT_NE(source_text, nullptr);
        ViewNodeState* state = view_tree_node_state(tree, source_text, false); ASSERT_NE(state, nullptr); size_t bytes = 0;
        for (LayoutViewNode* text = state->first_occurrence; text; text = text->next_occurrence) {
            EXPECT_EQ(text->text_start, bytes); bytes += text->text_length;
        }
        EXPECT_EQ(bytes, source_text->length);
    }
    ViewNodeState* state = view_tree_node_state(tree, body, false); ASSERT_NE(state, nullptr); size_t count = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        if (!fragment->paint_box) continue; count++;
        EXPECT_FLOAT_EQ(fragment->rect.x, occurrence_page(fragment) == 2 ? 45.0f : 55.0f);
    }
    EXPECT_EQ(count, 3u);
    EXPECT_NE(source_glyph_text(tree, term_page, "1"), nullptr);
    EXPECT_NE(source_glyph_text(tree, body_page, "3"), nullptr);
}

TEST_F(SecondaryViewTest, FoListsLowerToCommonNativeGridsAndKeepOriginalPartDiagnostics) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:list-block provisional-distance-between-starts='30pt' provisional-label-separation='6pt'><f:list-item padding-top='3pt'>"
        "<f:list-item-label end-indent='label-end()' padding-top='from-parent(padding-top)'><f:block>A.</f:block></f:list-item-label>"
        "<f:list-item-body start-indent='body-start()'><f:block>Body</f:block></f:list-item-body>"
        "</f:list-item></f:list-block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    DomElement* original_item = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original_item, nullptr);
    RadiantFoOptions options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* item = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(item, nullptr);
    EXPECT_STREQ(item->tag_name, "table"); EXPECT_STREQ(item->get_attribute("r:label-body-grid"), "true");
    DomElement* row = item->first_child_element(); ASSERT_NE(row, nullptr); EXPECT_STREQ(row->tag_name, "tr");
    DomElement* label = row->first_child_element(); DomElement* body = row->last_child_element();
    ASSERT_NE(label, nullptr); ASSERT_NE(body, nullptr); EXPECT_STREQ(label->tag_name, "td");
    ViewTree* tree = secondary(); PagedLayoutOptions paged = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &paged, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_STREQ(row->get_attribute("r:style-transparent"), "true");
    const ViewCssStyle* label_style = view_css_resolve(tree, label); ASSERT_NE(label_style, nullptr);
    EXPECT_EQ(label_style->parent.get(), view_css_resolve(tree, item));
    ASSERT_NE(label_style->padding[0], nullptr); EXPECT_DOUBLE_EQ(label_style->padding[0]->data.length.value, 4);
    EXPECT_FLOAT_EQ(source_fragment(tree, label, VIEW_FRAGMENT_BODY, true)->rect.width, 32.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, body, VIEW_FRAGMENT_BODY, true)->rect.x, 40.0f);
    options.max_nodes = translated->node_count - 1; translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(translated->root, nullptr);
    ASSERT_TRUE(original_item->first_child_element()->set_attribute("end-indent", "30pt")); options = radiant_fo_options_default();
    translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
    EXPECT_STREQ(translated->diagnostic.qname, "f:list-item-label"); EXPECT_STREQ(translated->diagnostic.property, "end-indent");
}

TEST_F(SecondaryViewTest, NativeImageContentSizingPositionsClipsAndRetainsTheExactSelectedObject) {
    init_vector_engine();
    stylesheet("@page { size:200px 120px; margin:10px } img { display:block; width:60px; height:40px; object-position:100% 100%; overflow:hidden }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* image = svg_image("width='30px' height='15px'"); ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->set_attribute("r:content-width", "200%"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    const PaintImageBox* box = node->image_box; ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->content_rect.width, 60.0f); EXPECT_FLOAT_EQ(box->content_rect.height, 40.0f);
    EXPECT_FLOAT_EQ(box->image_rect.width, 60.0f); EXPECT_FLOAT_EQ(box->image_rect.height, 30.0f);
    EXPECT_FLOAT_EQ(box->image_rect.x, 10.0f); EXPECT_FLOAT_EQ(box->image_rect.y, 20.0f); EXPECT_FALSE(box->overflow_visible);
    ImageSurface* snapshot = render_secondary_page_snapshot(tree, 1, 1.0f); ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot_pixel(snapshot, 15, 15), 0xffffffffu); EXPECT_EQ(snapshot_pixel(snapshot, 15, 25), 0xff0000ffu); image_surface_destroy(snapshot);
    ViewPreviewOptions preview_options = view_preview_options_default(); ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(image->set_attribute("r:content-width", "300%")); ASSERT_TRUE(image->set_attribute("style", "overflow:visible"));
    ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr); EXPECT_TRUE(node->image_box->overflow_visible);
    EXPECT_FLOAT_EQ(node->image_box->image_rect.width, 90.0f); EXPECT_FLOAT_EQ(node->image_box->image_rect.x, -20.0f);
    ASSERT_TRUE(image->set_attribute("r:scaling", "invalid")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "invalid native image content size or scaling");
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    snapshot = render_secondary_page_snapshot(preview, 1, 1.0f); ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot_pixel(snapshot, 15, 15), 0xffffffffu); EXPECT_EQ(snapshot_pixel(snapshot, 15, 25), 0xff0000ffu); image_surface_destroy(snapshot);
}

TEST_F(SecondaryViewTest, NativeImageContentInheritanceComputesLengthsInTheOriginalParentFont) {
    stylesheet("@page { size:200px 120px; margin:10px } div { font:10px/12px Arial } img { font-size:20px }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr); ASSERT_NE(parent, nullptr); ASSERT_TRUE(parent->set_attribute("r:content-width", "3em"));
    DomElement* image = this->image(nullptr, parent); ASSERT_NE(image, nullptr); ASSERT_TRUE(image->set_attribute("r:content-width", "inherit"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->content_rect.width, 30.0f); EXPECT_FLOAT_EQ(node->image_box->content_rect.height, 15.0f);
    ViewCssStyle* style = view_css_resolve(tree, image); ASSERT_NE(style, nullptr); ASSERT_NE(style->image_spec, nullptr);
    EXPECT_FLOAT_EQ(style->font.font_size, 20.0f); EXPECT_FLOAT_EQ(style->image_spec->axes[0].value, 30.0f);
}

TEST_F(SecondaryViewTest, NativeImageAllowedScalesInheritAcrossBlocksRemeasureAndRetainPaint) {
    init_vector_engine();
    stylesheet("@page{size:200px 120px;margin:10px}img{display:block;width:120px;height:60px;overflow:hidden;object-position:0% 0%}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    ASSERT_TRUE(source->set_attribute("r:allowed-width-scale", "any calc(25% * 2) 150% 300%"));
    DomElement* parent = block(nullptr); ASSERT_NE(parent, nullptr);
    DomElement* image = svg_image("width='60px' height='30px'", nullptr, parent); ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->set_attribute("r:content-width", "scale-to-fit"));
    ASSERT_TRUE(image->set_attribute("r:content-height", "scale-to-fit"));
    ASSERT_TRUE(image->set_attribute("r:allowed-height-scale", "150% 100% any"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->image_rect.width, 90); EXPECT_FLOAT_EQ(node->image_box->image_rect.height, 45);
    EXPECT_FLOAT_EQ(node->image_box->image_rect.x, 10); EXPECT_FLOAT_EQ(node->image_box->image_rect.y, 10);
    ViewCssStyle* style = view_css_resolve(tree, image); ASSERT_NE(style->image_spec, nullptr);
    EXPECT_EQ(style->image_spec->allowed[0].count, 3u); EXPECT_TRUE(style->image_spec->allowed[0].any);
    EXPECT_FLOAT_EQ(style->image_spec->allowed[0].values[0], .5f);
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(image->set_attribute("style", "width:45px;height:30px")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->image_rect.width, 30); EXPECT_FLOAT_EQ(node->image_box->image_rect.height, 15);
    ASSERT_TRUE(image->set_attribute("r:allowed-height-scale", "100%"));
    ASSERT_TRUE(image->set_attribute("r:allowed-width-scale", "50%")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* pixels = render_secondary_page_snapshot(preview, 1, 1.0f); ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(snapshot_pixel(pixels, 15, 15), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(pixels, 95, 50), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(pixels, 105, 50), 0xffffffffu); image_surface_destroy(pixels);
}

TEST_F(SecondaryViewTest, NativeComputedImageQueriesRetainOwnerUnitsAndSubstituteScaleLists) {
    stylesheet("@page{size:200px 120px;margin:10px}p{font-size:10px}div,img{font-size:20px}img{display:block;width:120px;height:60px}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    ASSERT_TRUE(source->set_attribute("r:content-width", "3em")); ASSERT_TRUE(source->set_attribute("r:scaling", "non-uniform"));
    ASSERT_TRUE(source->set_attribute("r:allowed-width-scale", "150% calc(25% * 2) any"));
    ASSERT_TRUE(source->set_attribute("r:allowed-height-scale", "100% 150%"));
    DomElement* parent = block(nullptr); ASSERT_NE(parent, nullptr);
    DomElement* image = svg_image("width='60px' height='30px'", nullptr, parent); ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->set_attribute("r:property-bindings", "--scales:ancestor(2,allowed-width-scale);--other:parent(allowed-height-scale);"
        "--axis:ancestor(2,content-width);--mode:ancestor(2,scaling)"));
    ASSERT_TRUE(image->set_attribute("r:allowed-width-scale", "var(--scales)"));
    ASSERT_TRUE(image->set_attribute("r:allowed-height-scale", "any var(--other)"));
    ASSERT_TRUE(image->set_attribute("r:content-width", "var(--axis)"));
    ASSERT_TRUE(image->set_attribute("r:content-height", "100%")); ASSERT_TRUE(image->set_attribute("r:scaling", "var(--mode)"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->image_rect.width, 30); EXPECT_FLOAT_EQ(node->image_box->image_rect.height, 30);
    ViewCssStyle* style = view_css_resolve(tree, image); ASSERT_NE(style->image_spec, nullptr);
    EXPECT_TRUE(style->image_spec->non_uniform); EXPECT_EQ(style->image_spec->allowed[0].count, 2u);
    const CssValue* width = view_css_computed_property(tree, style, "content-width"); ASSERT_NE(width, nullptr);
    EXPECT_EQ(width->type, CSS_VALUE_TYPE_LENGTH); EXPECT_DOUBLE_EQ(width->data.length.value, 30);
    const CssValue* scales = view_css_computed_property(tree, style, "allowed-width-scale"); ASSERT_NE(scales, nullptr);
    ASSERT_EQ(scales->type, CSS_VALUE_TYPE_LIST); ASSERT_EQ(scales->data.list.count, 3);
    EXPECT_DOUBLE_EQ(scales->data.list.values[0]->data.percentage.value, 50);
    EXPECT_DOUBLE_EQ(scales->data.list.values[1]->data.percentage.value, 150);
    EXPECT_STREQ(css_value_identifier_name(scales->data.list.values[2]), "any");
    for (const char* name : {"content-width", "content-height", "scaling", "allowed-width-scale", "allowed-height-scale"}) {
        EXPECT_TRUE(view_css_computed_property_supported(name));
        const CssValue* initial = view_css_computed_property(tree, nullptr, name); ASSERT_NE(initial, nullptr);
        EXPECT_STREQ(css_value_identifier_name(initial), !strcmp(name, "scaling") ? "uniform" : !strncmp(name, "allowed-", 8) ? "any" : "auto");
    }
    ASSERT_TRUE(source->set_attribute("r:content-width", "scale-to-fit")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->image_rect.width, 90); EXPECT_FLOAT_EQ(node->image_box->image_rect.height, 30);
}

TEST_F(SecondaryViewTest, NativeImageAllowedScalesRejectInvalidDomainsBeforePublishingPages) {
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* image = this->image(nullptr); ASSERT_NE(image, nullptr);
    const char* invalid[] = {"", "50%,100%", "50", "-25%", "1px", "inherit 50%", "'any'", "bogus", "calc(50% / 0)", "1e309%"};
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    for (const char* value : invalid) {
        SCOPED_TRACE(value); ASSERT_TRUE(image->set_attribute("r:allowed-width-scale", value)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_EQ(tree->model->page_count, 0u);
    }
}

TEST_F(SecondaryViewTest, FoExternalGraphicsLowerToNativeContentSizingAndKeepResourcePolicies) {
    StrBuf* xml = strbuf_new(); ASSERT_NE(xml, nullptr);
    strbuf_append_str(xml, "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'><f:block content-width='3em'><f:external-graphic src=\"url('");
    strbuf_append_str(xml, paged_split_png);
    strbuf_append_str(xml, "')\" width='60pt' height='30pt' content-width='inherit' font-size='15pt' text-align='center' display-align='after'/></f:block></f:flow></f:page-sequence></f:root>");
    DomElement* fo = formatting_root(xml->str); strbuf_free(xml); ASSERT_NE(fo, nullptr);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* image = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(image, nullptr);
    EXPECT_STREQ(image->tag_name, "img"); EXPECT_STREQ(image->get_attribute("src"), paged_split_png);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    const PaintImageBox* box = node->image_box; EXPECT_FLOAT_EQ(box->content_rect.width, 80.0f); EXPECT_FLOAT_EQ(box->content_rect.height, 40.0f);
    EXPECT_FLOAT_EQ(box->image_rect.width, 30.0f); EXPECT_FLOAT_EQ(box->image_rect.height, 15.0f);
    EXPECT_FLOAT_EQ(box->image_rect.x - box->content_rect.x, 25.0f); EXPECT_FLOAT_EQ(box->image_rect.y - box->content_rect.y, 25.0f);
    EXPECT_FALSE(box->overflow_visible);
    ASSERT_TRUE(generated->set_attribute("xml:base", "https://example.test/books/")); ASSERT_TRUE(image->set_attribute("src", "graphic.png"));
    doc.resource_policy = INPUT_RESOURCE_LOCAL_ONLY; ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, original);
    const char* url = radiant_page_resource_url(image, "graphic.png", doc.document_pool, 256); ASSERT_NE(url, nullptr);
    EXPECT_STREQ(url, "https://example.test/books/graphic.png");
    ASSERT_TRUE(image->set_attribute("xml:base", "../images/"));
    url = radiant_page_resource_url(image, "graphic.png", doc.document_pool, 256); ASSERT_NE(url, nullptr);
    EXPECT_STREQ(url, "https://example.test/images/graphic.png");
    url = radiant_page_resource_url(image, "/shared/graphic.png", doc.document_pool, 256); ASSERT_NE(url, nullptr);
    EXPECT_STREQ(url, "https://example.test/shared/graphic.png");
}

TEST_F(SecondaryViewTest, FoImageScaleExpressionsAndWholeParentQueriesUseTheSharedNativeSizingModel) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' allowed-width-scale='any 25% * 2 75% * 2 300%' "
        "allowed-height-scale='min(200%, 150%) any'><f:layout-master-set>"
        "<f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master>"
        "</f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'>"
        "<f:flow flow-name='xsl-region-body'><f:block><f:instream-foreign-object width='60pt' height='30pt' "
        "content-width='scale-to-fit' content-height='scale-to-fit' allowed-width-scale='inherited-property-value()' "
        "allowed-height-scale='from-parent()'><svg xmlns='http://www.w3.org/2000/svg' width='30pt' height='15pt'>"
        "<rect width='40' height='20' fill='red'/></svg></f:instream-foreign-object></f:block></f:flow></f:page-sequence></f:root>");
    ASSERT_NE(fo, nullptr);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element();
    ASSERT_NE(original, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* image = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element();
    ASSERT_NE(image, nullptr); EXPECT_STREQ(image->get_attribute("r:allowed-width-scale"), "inherit");
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->image_rect.width, 60); EXPECT_FLOAT_EQ(node->image_box->image_rect.height, 30);
    fo_options.max_nodes = translated->node_count - 1;
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(translated->root, nullptr);
    ASSERT_TRUE(image->set_attribute("r:allowed-width-scale", "50")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, original);
}

TEST_F(SecondaryViewTest, FoGraphicPropertyQueriesBindComputedAncestorsAndExpandWholeScaleLists) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-size='7.5pt' content-width='3em' scaling='non-uniform' "
        "allowed-width-scale='50% 150% any' allowed-height-scale='100% 150% any'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'>"
        "<f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block font-size='15pt'><f:wrapper><f:instream-foreign-object width='60pt' height='30pt' "
        "content-width='from-nearest-specified-value(content-width)' content-height='100%' "
        "scaling='from-nearest-specified-value(scaling)' allowed-width-scale='from-nearest-specified-value()' "
        "allowed-height-scale='from-parent()'><svg xmlns='http://www.w3.org/2000/svg' width='30pt' height='15pt'>"
        "<rect width='40' height='20' fill='red'/></svg></f:instream-foreign-object></f:wrapper></f:block>"
        "<f:block><f:instream-foreign-object width='60pt' height='30pt' content-width='scale-to-fit' content-height='scale-to-fit' "
        "allowed-width-scale='any from-parent(allowed-width-scale)' allowed-height-scale='from-parent(allowed-width-scale)'>"
        "<svg xmlns='http://www.w3.org/2000/svg' width='30pt' height='15pt'><rect width='40' height='20' fill='red'/></svg>"
        "</f:instream-foreign-object></f:block></f:flow></f:page-sequence></f:root>");
    ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* flow = generated->last_child_element()->last_child_element(); ASSERT_NE(flow, nullptr);
    DomElement* first = flow->first_child_element()->first_child_element()->first_child_element(); ASSERT_NE(first, nullptr);
    DomElement* second = flow->last_child_element()->first_child_element(); ASSERT_NE(second, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* first_fragment = source_image_fragment(tree, first); ASSERT_NE(first_fragment, nullptr);
    EXPECT_FLOAT_EQ(first_fragment->image_box->image_rect.width, 30); EXPECT_FLOAT_EQ(first_fragment->image_box->image_rect.height, 20);
    LayoutViewNode* second_fragment = source_image_fragment(tree, second); ASSERT_NE(second_fragment, nullptr);
    EXPECT_FLOAT_EQ(second_fragment->image_box->image_rect.width, 60); EXPECT_FLOAT_EQ(second_fragment->image_box->image_rect.height, 30);
    const ViewCssStyle* style = view_css_resolve(tree, second); ASSERT_NE(style->image_spec, nullptr);
    EXPECT_EQ(style->image_spec->allowed[0].count, 2u); EXPECT_TRUE(style->image_spec->allowed[0].any);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element();
    ASSERT_NE(original, nullptr); ASSERT_TRUE(original->set_attribute("content-width", "inherited-property-value()"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
    EXPECT_STREQ(translated->diagnostic.property, "content-width");
}

TEST_F(SecondaryViewTest, FoDecorationArithmeticUsesCssExpansionAndAbsoluteLonghandPrecedence) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-size='7.5pt' font-family='Arial' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'>"
        "<f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block font-size='15pt' padding='1pt + 2pt 4pt div 2 0 2pt' border='1pt + 2pt solid rgb(from-parent(font-size) div 1pt * 10, 0, 0)' "
        "border-width='4pt div 2' border-top='5pt solid blue' border-before-width='3pt' border-top-width='1pt'>"
        "<f:block border='(3pt div 4) solid #d04020'>X</f:block></f:block>"
        "</f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* block = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(block, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    const ViewCssStyle* style = view_css_resolve(tree, block); ASSERT_NE(style, nullptr);
    const char* names[] = {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
        "padding-top", "padding-right", "padding-bottom", "padding-left"};
    const float expected[] = {4.0f/3, 8.0f/3, 8.0f/3, 8.0f/3, 4, 8.0f/3, 0, 8.0f/3};
    for (size_t i = 0; i < 8; i++) {
        SCOPED_TRACE(names[i]);
        const CssValue* value = view_css_computed_property(tree, style, names[i]); ASSERT_NE(value, nullptr);
        ASSERT_EQ(value->type, CSS_VALUE_TYPE_LENGTH); EXPECT_NEAR(value->data.length.value, expected[i], .0001);
    }
    for (size_t i = 0; i < 4; i++) EXPECT_EQ(style->border_style[i], CSS_VALUE_SOLID);
    EXPECT_EQ(style->border_color[0].b, 255); EXPECT_EQ(style->border_color[0].r, 0);
    EXPECT_EQ(style->border_color[1].r, 75); EXPECT_EQ(style->border_color[1].g, 0); EXPECT_EQ(style->border_color[1].b, 0);
    const ViewCssStyle* child_style = view_css_resolve(tree, block->first_child_element()); ASSERT_NE(child_style, nullptr);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(child_style->border_style[i], CSS_VALUE_SOLID);
        EXPECT_FLOAT_EQ(child_style->border_width[i]->data.length.value, 1);
        EXPECT_EQ(child_style->border_color[i].r, 208);
    }
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    fo_options.max_nodes = translated->node_count - 1;
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(translated->root, nullptr);
    fo_options = radiant_fo_options_default();
    for (const char* text : {"1pt + 2pt 3pt 4pt 5pt 6pt", "1pt + 2pt bogus", "rgb(1,2,3) 2pt", "1pt + 2pt inherit"}) {
        SCOPED_TRACE(text); ASSERT_TRUE(original->set_attribute("padding", text));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
        EXPECT_STREQ(translated->diagnostic.property, "padding");
    }
    EXPECT_EQ(tree->model->page_count, 1u);
}

TEST_F(SecondaryViewTest, SvgXmlIntakeUsesTheExpandedRootName) {
    const char* admitted[] = {"<svg/>", "<svg xmlns='http://www.w3.org/2000/svg'/>",
        "<s:svg xmlns:s='http://www.w3.org/2000/svg'/>",
        "<s:svg xmlns='urn:foreign' xmlns:s='http://www.w3.org/2000/svg'/>"};
    for (const char* xml : admitted) { SCOPED_TRACE(xml); EXPECT_NE(parse_svg_document(input, xml), nullptr); }
    const char* rejected[] = {"<s:svg/>", "<svg xmlns='urn:foreign'/>",
        "<s:svg xmlns:s='urn:foreign'/>", "<svg xmlns=''/>", "<s:rect xmlns:s='http://www.w3.org/2000/svg'/>"};
    for (const char* xml : rejected) { SCOPED_TRACE(xml); EXPECT_EQ(parse_svg_document(input, xml), nullptr); }
}

TEST_F(SecondaryViewTest, QualifiedSvgGeometryUsesNamespaceMembershipAndNamespacedReferences) {
    init_vector_engine();
    Element* root = parse_svg_document(input,
        "<s:svg xmlns='urn:foreign' xmlns:s='http://www.w3.org/2000/svg' xmlns:h='http://www.w3.org/1999/xlink'>"
        "<s:defs><s:rect id='shape' width='20' height='10'/></s:defs><s:use h:href='#shape' x='10' y='5'/>"
        "<s:rect xmlns:s='urn:foreign' width='500' height='500'/>"
        "<s:g><s:path d='M40 0h10v10h-10z'/></s:g></s:svg>"); ASSERT_NE(root, nullptr);
    doc.root = lam::up(build_dom_tree_from_element(root, &doc, nullptr)); ASSERT_NE(doc.root, nullptr);
    float left, top, right, bottom;
    ASSERT_TRUE(dom_svg_element_geometry_bounds(doc.root, &left, &top, &right, &bottom));
    EXPECT_FLOAT_EQ(left, 10.0f); EXPECT_FLOAT_EQ(top, 0.0f);
    EXPECT_FLOAT_EQ(right, 50.0f); EXPECT_FLOAT_EQ(bottom, 15.0f);
}

TEST_F(SecondaryViewTest, FoInstreamSvgUsesTheCommonImageOwnerAndTranslationBudgets) {
    init_vector_engine();
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' xmlns:link='http://www.w3.org/1999/xlink' xmlns:s='http://www.w3.org/2000/svg'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'><f:block><f:instream-foreign-object width='60pt' height='30pt' content-width='150%' text-align='center' display-align='after'>"
        "<s:svg width='20' height='10'><s:style>.shape{fill:url(#paint)}</s:style>"
        "<s:defs><s:linearGradient id='paint'><s:stop offset='0' stop-color='red'/><s:stop offset='1' stop-color='red'/></s:linearGradient>"
        "<s:clipPath id='clip'><s:rect width='20' height='10'/></s:clipPath><s:rect id='shape' class='shape' width='20' height='10'/></s:defs>"
        "<s:use link:href='#shape' clip-path='url(#clip)'/><s:rect x='10' width='10' height='10' fill='url(#paint)'/>"
        "<s:rect xmlns:s='urn:foreign' width='20' height='10' fill='green'/>"
        "<x:style xmlns:x='urn:foreign'>.shape{fill:green}</x:style></s:svg>"
        "</f:instream-foreign-object></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    RadiantFoOptions options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* image = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(image, nullptr);
    EXPECT_STREQ(image->tag_name, "img"); ASSERT_NE(image->get_attribute("src"), nullptr);
    EXPECT_EQ(strncmp(image->get_attribute("src"), "data:image/svg+xml,", 19), 0);
    ViewTree* tree = secondary(); PagedLayoutOptions paged = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &paged, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    Element* svg_root = rdt_picture_get_svg_root(node->image_box->image->pic); ASSERT_NE(svg_root, nullptr);
    ElementReader svg(svg_root);
    EXPECT_STREQ(svg.get_attr_string("xmlns:s"), "http://www.w3.org/2000/svg");
    EXPECT_STREQ(svg.childAt(0).asElement().childAt(0).cstring(), ".shape{fill:url(#paint)}");
    EXPECT_FLOAT_EQ(node->image_box->image_rect.width, 30.0f); EXPECT_FLOAT_EQ(node->image_box->image_rect.height, 15.0f);
    ImageSurface* pixels = render_secondary_page_snapshot(tree, 1, 1.0f); ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(snapshot_pixel(pixels, 30, 30), 0xff0000ffu); EXPECT_EQ(snapshot_pixel(pixels, 45, 30), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(pixels, 10, 10), 0xffffffffu); image_surface_destroy(pixels);
    options.max_nodes = translated->node_count - 1;
    translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(translated->root, nullptr);
    options = radiant_fo_options_default();
    DomElement* extra = block(nullptr, nullptr, "svg", original); ASSERT_NE(extra, nullptr); ASSERT_TRUE(extra->set_attribute("xmlns", "http://www.w3.org/2000/svg"));
    translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
    EXPECT_STREQ(translated->diagnostic.reason, "instream foreign object requires exactly one SVG root");
}

TEST_F(SecondaryViewTest, NativeLineStackingRetainsRawAllocationAndLeadingAsDistinctPolicies) {
    init_vector_engine();
    stylesheet("@page { size:100px 100px; margin:10px } div { font:10px/20px Arial } img { width:40px; height:40px }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* images[2] = {};
    for (size_t i = 0; i < 2; i++) {
        DomElement* parent = block(nullptr); ASSERT_NE(parent, nullptr);
        images[i] = image(nullptr, parent); ASSERT_NE(images[i], nullptr);
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    const char* policies[] = {"line-height", "max-height", "font-height", "css"};
    for (size_t i = 0; i < 4; i++) {
        SCOPED_TRACE(policies[i]); ASSERT_TRUE(source->set_attribute("r:line-stacking-strategy", policies[i]));
        ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ViewCssStyle* parent = view_css_resolve(tree, images[0]->parent_element()); ASSERT_NE(parent, nullptr);
        const FontMetrics* font = font_get_metrics(parent->font.font_handle); ASSERT_NE(font, nullptr);
        float half_leading = (20.0f - font->ascender + font->descender) * 0.5f;
        float baseline = i == 2 ? font->ascender + half_leading : 40.0f + (i == 1 ? half_leading : 0.0f);
        float height = i == 2 ? 20.0f : i == 1 ? 40.0f - font->descender + 2.0f * half_leading : 40.0f - font->descender + half_leading;
        EXPECT_EQ(tree->model->page_count, i == 2 ? 1u : 2u);
        LayoutViewNode* node = source_image_fragment(tree, images[0]); ASSERT_NE(node, nullptr);
        EXPECT_NEAR(node->image_box->content_rect.y, 10.0f + baseline - 40.0f, 0.001f);
        ASSERT_NE(node->parent, nullptr); EXPECT_NEAR(node->parent->rect.height, height, 0.001f);
        EXPECT_EQ(parent->flow_traits ? parent->flow_traits->line_stacking : RADIANT_LINE_STACK_CSS,
            i == 1 ? RADIANT_LINE_STACK_MAX : i == 2 ? RADIANT_LINE_STACK_FONT : RADIANT_LINE_STACK_CSS);
    }
    ASSERT_TRUE(source->set_attribute("r:line-stacking-strategy", "invalid")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "line stacking requires line-height, max-height or font-height");
}

TEST_F(SecondaryViewTest, NativeNominalTextGeometryComputesFontPercentagesAndInheritedBaselinesOnce) {
    stylesheet("@page { size:200px 120px; margin:10px } div { font:10px/20px Arial } span { font-size:20px }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block("P"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:line-stacking-strategy", "max-height"));
    ASSERT_TRUE(parent->set_attribute("r:text-altitude", "0.8em")); ASSERT_TRUE(parent->set_attribute("r:text-depth", "25%"));
    DomElement* child = block("C", nullptr, "span", parent); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:text-altitude", "inherit")); ASSERT_TRUE(child->set_attribute("r:text-depth", "inherit"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    float altitude, depth;
    ASSERT_TRUE(radiant_text_metrics(view_css_resolve(tree, child), &altitude, &depth));
    EXPECT_FLOAT_EQ(altitude, 8.0f); EXPECT_FLOAT_EQ(depth, 2.5f);
    LayoutViewNode* a = source_glyph_text(tree, parent->first_child, "P"), *b = source_glyph_text(tree, child->first_child, "C");
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(a->glyph_run->baseline_y, 22.75f); EXPECT_FLOAT_EQ(b->glyph_run->baseline_y, 22.75f);
    ASSERT_TRUE(child->set_attribute("r:text-altitude", "50%")); ASSERT_TRUE(child->set_attribute("r:text-depth", "use-font-metrics"));
    ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    ASSERT_TRUE(radiant_text_metrics(style, &altitude, &depth));
    EXPECT_FLOAT_EQ(altitude, 10.0f); EXPECT_FLOAT_EQ(depth, font_get_metrics(style->font.font_handle)->typo_descender);
    ASSERT_TRUE(child->set_attribute("r:text-depth", "-1pt")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "nominal text geometry requires finite nonnegative values");
}

TEST_F(SecondaryViewTest, NativeZeroAltitudeAndNegativeLeadingKeepTheExplicitBaseline) {
    stylesheet("@page { size:100px 100px; margin:10px } div { font:10px/0px Arial } img { width:40px; height:40px }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:line-stacking-strategy", "max-height"));
    ASSERT_TRUE(parent->set_attribute("r:text-altitude", "0pt")); ASSERT_TRUE(parent->set_attribute("r:text-depth", "10px"));
    DomElement* image = this->image(nullptr, parent); ASSERT_NE(image, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->content_rect.y, 5.0f); EXPECT_FLOAT_EQ(node->parent->rect.height, 40.0f);
    ASSERT_TRUE(parent->set_attribute("r:line-stacking-strategy", "font-height")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    node = source_image_fragment(tree, image); ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->image_box->content_rect.y, -35.0f); EXPECT_FLOAT_EQ(node->parent->rect.height, 0.0f);
}

TEST_F(SecondaryViewTest, ResponsiveImagesKeepIndependentDensityAndReselectWithoutChangingSharedPixels) {
    ASSERT_TRUE(create_dir("temp/paged-media-responsive"));
    init_vector_engine();
    stylesheet("@page { size: 120px 80px; margin: 0 } p { margin: 0 } img { display: block; object-fit: none }");
    DomElement* img = image(); ASSERT_NE(img, nullptr);
    StrBuf* candidates = strbuf_new(); ASSERT_NE(candidates, nullptr);
    strbuf_append_str(candidates, paged_split_png); strbuf_append_str(candidates, " 2x");
    ASSERT_TRUE(img->set_attribute("srcset", candidates->str)); strbuf_free(candidates);
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED);
    environment.device_scale = 2.0f;
    ViewTree* dense = view_tree_secondary_create(&doc, &environment), *normal = secondary();
    ASSERT_NE(dense, nullptr); ASSERT_NE(normal, nullptr);
    PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(dense, &options, nullptr), TYPESET_OK);
    ASSERT_EQ(layout_secondary_view(normal, &options, nullptr), TYPESET_OK);
    LayoutViewNode* high = source_image_fragment(dense, img), *low = source_image_fragment(normal, img);
    ASSERT_NE(high, nullptr); ASSERT_NE(low, nullptr);
    EXPECT_FLOAT_EQ(high->rect.width, 10); EXPECT_FLOAT_EQ(high->rect.height, 5);
    EXPECT_FLOAT_EQ(low->rect.width, 20); EXPECT_FLOAT_EQ(low->rect.height, 10);
    EXPECT_FLOAT_EQ(high->image_box->image_rect.width, 10);
    EXPECT_EQ(high->image_box->image->width, 20); EXPECT_EQ(low->image_box->image->width, 20);
    EXPECT_EQ(img->embed, nullptr);
    EXPECT_TRUE(render_secondary_view_to_pdf(dense, "temp/paged-media-responsive/density.pdf"));
    ImageSurface* pixels = render_secondary_page_snapshot(dense, 1, 2.0f); ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(pixels->width, 240); EXPECT_EQ(pixels->height, 160);
    save_surface_to_png(pixels, "temp/paged-media-responsive/density.png"); image_surface_destroy(pixels);
    ASSERT_TRUE(view_tree_model_reset(dense)); dense->model->environment.device_scale = 1.0f;
    ASSERT_EQ(layout_secondary_view(dense, &options, nullptr), TYPESET_OK);
    EXPECT_FLOAT_EQ(source_image_fragment(dense, img)->rect.width, 20);
    EXPECT_FLOAT_EQ(source_image_fragment(normal, img)->rect.width, 20);
}

TEST_F(SecondaryViewTest, BrowsingImageReselectionRechecksAdmissionBeforeReusingAnEmbed) {
    stylesheet("p { margin: 0 }"); CssEngine* engine = (CssEngine*)doc.services.cached_css_engine;
    UiContext ui = {}; ui.document = lam::up(&doc);
    LayoutContext layout = {}; layout.doc = lam::up(&doc); layout.ui_context = lam::up(&ui);
    layout.pool = lam::up(doc.view_tree->prop_pool.get());
    DomElement* img = image(); ASSERT_NE(img, nullptr);
    img->view_type = RDT_VIEW_BLOCK;
    ViewBlock* block = lam::view_as_block(img); ASSERT_NE(block, nullptr);
    StrBuf* candidates = strbuf_new(); ASSERT_NE(candidates, nullptr);
    strbuf_append_str(candidates, paged_split_png); strbuf_append_str(candidates, " 2x");
    ASSERT_TRUE(img->set_attribute("srcset", candidates->str)); strbuf_free(candidates);
    engine->context.device_pixel_ratio = 2;
    ImageSurface* loaded = layout_ensure_replaced_image_surface(&layout, block, img); ASSERT_NE(loaded, nullptr);
    float width = 0, height = 0; ASSERT_TRUE(layout_image_intrinsic_size(img, loaded, &width, &height));
    EXPECT_FLOAT_EQ(width, 10); EXPECT_FLOAT_EQ(height, 5);
    engine->context.device_pixel_ratio = 1;
    ASSERT_EQ(layout_ensure_replaced_image_surface(&layout, block, img), loaded);
    ASSERT_TRUE(layout_image_intrinsic_size(img, loaded, &width, &height)); EXPECT_FLOAT_EQ(width, 20);
    ASSERT_TRUE(img->set_attribute("srcset", "https://example.test/warm.png 1x"));
    doc.url = lam::own(url_parse("https://example.test/document.html")); ASSERT_NE(doc.url, nullptr);
    ImageSurface* remote = image_surface_create(4, 4); ASSERT_NE(remote, nullptr);
    ASSERT_EQ(image_cache_adopt(&ui, "https://example.test/warm.png", remote), remote);
    ASSERT_EQ(layout_ensure_replaced_image_surface(&layout, block, img), remote);
    doc.resource_policy = INPUT_RESOURCE_LOCAL_ONLY;
    EXPECT_EQ(layout_ensure_replaced_image_surface(&layout, block, img), nullptr);
    EXPECT_EQ(block->embedp()->img, nullptr);
    image_cache_cleanup(&ui);
}

TEST_F(SecondaryViewTest, SvgImageFactsRetainFractionalAxesAndIndependentRatioProvenance) {
    init_vector_engine();
    const char* roots[] = {
        "width='16.25' height='8.125'",
        "width='25.5'",
        "height='18.25'",
        "width='25.5' viewBox='0 0 30.75 10.25'",
        "height='18.25' viewBox='0 0 30.75 10.25'",
        "viewBox='0 0 30.75 10.25'",
        "width='16.25' height='8.125' viewBox='0 0 10 10'"
    };
    const float widths[] = {16.25f, 25.5f, 0.0f, 25.5f, 0.0f, 0.0f, 16.25f};
    const float heights[] = {8.125f, 0.0f, 18.25f, 0.0f, 18.25f, 0.0f, 8.125f};
    const float ratios[] = {2.0f, 0.0f, 0.0f, 3.0f, 3.0f, 3.0f, 2.0f};
    const float used_widths[] = {16.25f, 25.5f, 300.0f, 25.5f, 54.75f, 300.0f, 16.25f};
    const float used_heights[] = {8.125f, 150.0f, 18.25f, 8.5f, 18.25f, 100.0f, 8.125f};
    const float none_widths[] = {16.25f, 25.5f, 40.0f, 25.5f, 54.75f, 40.0f, 16.25f};
    const float none_heights[] = {8.125f, 20.0f, 18.25f, 8.5f, 18.25f, 40.0f / 3.0f, 8.125f};
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr);
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        SCOPED_TRACE(roots[i]);
        DomElement* element = svg_image(roots[i]); ASSERT_NE(element, nullptr);
        ImageSurface* image = load_document_image_resource(&doc, &tree->model->image_resources, element->get_attribute("src"));
        ASSERT_NE(image, nullptr);
        ReplacedIntrinsicFacts facts = {}; layout_replaced_image_facts(&facts, image, true);
        EXPECT_EQ(facts.has_natural_width, widths[i] > 0.0f);
        EXPECT_EQ(facts.has_natural_height, heights[i] > 0.0f);
        EXPECT_EQ(facts.has_natural_aspect_ratio, ratios[i] > 0.0f);
        EXPECT_FLOAT_EQ(facts.natural_width, widths[i]); EXPECT_FLOAT_EQ(facts.natural_height, heights[i]);
        EXPECT_FLOAT_EQ(facts.natural_aspect_ratio, ratios[i]);
        EXPECT_FLOAT_EQ(facts.width, used_widths[i]); EXPECT_FLOAT_EQ(facts.height, used_heights[i]);
        float width = 0.0f, height = 0.0f;
        ASSERT_TRUE(layout_image_intrinsic_size(source, image, &width, &height));
        EXPECT_FLOAT_EQ(width, used_widths[i]); EXPECT_FLOAT_EQ(height, used_heights[i]);
        ReplacedSizeConstraints constraints = {NAN, NAN, 0.0f, 0.0f, INFINITY, INFINITY,
            facts.natural_aspect_ratio, 0.0f, 0.0f, true};
        ASSERT_TRUE(layout_replaced_content_size(&facts, &constraints, 340.0f, &width, &height));
        EXPECT_FLOAT_EQ(width, used_widths[i]); EXPECT_FLOAT_EQ(height, used_heights[i]);
        if (ratios[i] > 0.0f) {
            EmbedProp object = {}; object.object_fit = CSS_VALUE_CONTAIN;
            Rect rect = render_media_object_rect(&object, image, {0.0f, 0.0f, 40.0f, 20.0f}, 1.0f);
            EXPECT_FLOAT_EQ(rect.width, 40.0f); EXPECT_FLOAT_EQ(rect.height, 40.0f / ratios[i]);
        }
        EmbedProp object = {};
        Rect filled = render_media_object_rect(&object, image, {0.0f, 0.0f, 40.0f, 20.0f}, 1.0f);
        EXPECT_FLOAT_EQ(filled.width, 40.0f); EXPECT_FLOAT_EQ(filled.height, 20.0f);
        object.object_fit = CSS_VALUE_NONE;
        Rect none = render_media_object_rect(&object, image, {0.0f, 0.0f, 40.0f, 20.0f}, 1.0f);
        EXPECT_FLOAT_EQ(none.width, none_widths[i]); EXPECT_FLOAT_EQ(none.height, none_heights[i]);
        EXPECT_FLOAT_EQ(none.x, (40.0f - none_widths[i]) * .5f);
        object.object_fit = CSS_VALUE_SCALE_DOWN;
        Rect down = render_media_object_rect(&object, image, {0.0f, 0.0f, 80.0f, 40.0f}, 2.0f);
        EXPECT_FLOAT_EQ(down.width, (i == 4 ? 40.0f : none_widths[i]) * 2.0f);
        EXPECT_FLOAT_EQ(down.height, (i == 4 ? 40.0f / 3.0f : none_heights[i]) * 2.0f);
    }
}

TEST_F(SecondaryViewTest, SvgIntrinsicAxesFlowThroughIndependentPhysicalPages) {
    init_vector_engine();
    stylesheet("@page { size: 360px 240px; margin: 10px } p { margin: 0 } img { display: block; break-before: page }");
    const char* roots[] = {"width='16.25' height='8.125'", "width='25.5'", "height='18.25'",
        "width='25.5' viewBox='0 0 30.75 10.25'", "height='18.25' viewBox='0 0 30.75 10.25'",
        "viewBox='0 0 30.75 10.25'", "width='25.5'", "height='18.25'"};
    const char* css[] = {nullptr, nullptr, nullptr, nullptr, nullptr, "width: 51px", "width: 51px", "height: 36.5px"};
    const float widths[] = {16.25f, 25.5f, 300.0f, 25.5f, 54.75f, 51.0f, 51.0f, 300.0f};
    const float heights[] = {8.125f, 150.0f, 18.25f, 8.5f, 18.25f, 17.0f, 150.0f, 36.5f};
    DomElement* images[8] = {};
    for (size_t i = 0; i < 8; i++) { images[i] = svg_image(roots[i], css[i]); ASSERT_NE(images[i], nullptr); }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 8u);
    for (size_t i = 0; i < 8; i++) {
        SCOPED_TRACE(i);
        LayoutViewNode* fragment = source_image_fragment(tree, images[i]); ASSERT_NE(fragment, nullptr);
        EXPECT_EQ(occurrence_page(fragment), i + 1);
        EXPECT_FLOAT_EQ(fragment->rect.width, widths[i]); EXPECT_FLOAT_EQ(fragment->rect.height, heights[i]);
        EXPECT_FLOAT_EQ(fragment->image_box->image_rect.width, widths[i]);
        EXPECT_FLOAT_EQ(fragment->image_box->image_rect.height, heights[i]);
        EXPECT_EQ(images[i]->embed, nullptr); EXPECT_EQ(images[i]->width, 0.0f);
    }
    ASSERT_TRUE(create_dir("temp/paged-media-svg-intrinsics"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-svg-intrinsics/native-axis-pages.pdf"));
    ImageSurface* snapshot = render_secondary_page_snapshot(tree, 2, 2.0f); ASSERT_NE(snapshot, nullptr);
    Color inside = {snapshot_pixel(snapshot, 24, 24)}, outside = {snapshot_pixel(snapshot, 75, 24)};
    EXPECT_EQ(inside.r, 255); EXPECT_EQ(inside.g, 0); EXPECT_EQ(outside.r, 255); EXPECT_EQ(outside.g, 255);
    save_surface_to_png(snapshot, "temp/paged-media-svg-intrinsics/native-width-only.png"); image_surface_destroy(snapshot);
}

TEST_F(SecondaryViewTest, SvgImageMetadataUsesAbsoluteUnitsWithoutPromotingRelativeOrInvalidAxes) {
    init_vector_engine();
    const char* roots[] = {"width='12pt' height='6pt'", "width='1.016q' height='0.508q'",
        "width='25pxbad' height='18.25' viewBox='0 0 30.75 10.25'",
        "width='25%25' height='18.25' viewBox='0 0 30.75 10.25'",
        "width='25em' height='18.25' viewBox='0 0 30.75 10.25'",
        "width='auto' height='18.25' viewBox='0 0 30.75 10.25'", "width=' 12pt  ' height=' 6pt  '"};
    const float widths[] = {16.0f, .96f, 0.0f, 0.0f, 0.0f, 0.0f, 16.0f};
    const float heights[] = {8.0f, .48f, 18.25f, 18.25f, 18.25f, 18.25f, 8.0f};
    const float ratios[] = {2.0f, 2.0f, 3.0f, 3.0f, 3.0f, 3.0f, 2.0f};
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr);
    for (size_t i = 0; i < 7; i++) {
        SCOPED_TRACE(roots[i]); DomElement* element = svg_image(roots[i]); ASSERT_NE(element, nullptr);
        ImageSurface* image = load_document_image_resource(&doc, &tree->model->image_resources, element->get_attribute("src"));
        ASSERT_NE(image, nullptr); ReplacedIntrinsicFacts facts = {}; layout_replaced_image_facts(&facts, image, true);
        EXPECT_EQ(facts.has_natural_width, widths[i] > 0.0f); EXPECT_TRUE(facts.has_natural_height);
        EXPECT_FLOAT_EQ(facts.natural_width, widths[i]); EXPECT_FLOAT_EQ(facts.natural_height, heights[i]);
        EXPECT_FLOAT_EQ(facts.natural_aspect_ratio, ratios[i]);
        EXPECT_GT(image->width, 0); EXPECT_GT(image->height, 0);
    }
}

TEST_F(SecondaryViewTest, AdjacentImagesWrapAtAtomicBoundariesAndResumeOnTheNextPage) {
    init_vector_engine();
    stylesheet("@page { size: 100px 60px; margin: 10px } div { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 }");
    DomElement* body = block(nullptr); ASSERT_NE(body, nullptr);
    DomElement* images[3] = {};
    for (size_t i = 0; i < 3; i++) {
        images[i] = image("width: 50px; height: 20px; white-space: nowrap", body);
        ASSERT_NE(images[i], nullptr);
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    for (size_t i = 0; i < 3; i++) {
        LayoutViewNode* fragment = source_image_fragment(tree, images[i]); ASSERT_NE(fragment, nullptr);
        EXPECT_EQ(occurrence_page(fragment), i + 1);
        EXPECT_FLOAT_EQ(fragment->rect.x, 10.0f); EXPECT_FLOAT_EQ(fragment->rect.width, 50.0f);
        EXPECT_EQ(fragment->next_occurrence, nullptr);
        ImageSurface* snapshot = render_secondary_page_snapshot(tree, i + 1, 2.0f); ASSERT_NE(snapshot, nullptr);
        Color red = {snapshot_pixel(snapshot, 24, 24)}; EXPECT_GT(red.r, 200); EXPECT_LT(red.b, 30);
        image_surface_destroy(snapshot);
        EXPECT_EQ(images[i]->embed, nullptr);
    }
    ASSERT_TRUE(create_dir("temp/paged-media-image-wrap"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-image-wrap/adjacent-images.pdf"));
}

TEST_F(SecondaryViewTest, AtomicWrapUsesTheCommonAncestorAndPreservesUnicodeNoBreakControls) {
    init_vector_engine();
    stylesheet("@page { size: 100px 160px; margin: 10px } div, p { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 }");
    const char* neighbors[] = {"A", "\xc2\xa0", "\xe2\x81\xa0", "\xe2\x80\x8d", "\xe2\x80\xaf", "\xe2\x80\x87", "\xe2\x80\x91"};
    for (size_t side = 0; side < 2; side++) for (size_t i = 0; i < sizeof(neighbors) / sizeof(neighbors[0]); i++) {
        SCOPED_TRACE(side);
        SCOPED_TRACE(i);
        StrBuf* label = strbuf_new(); ASSERT_NE(label, nullptr);
        if (!side && i) strbuf_append_char(label, 'A');
        strbuf_append_str(label, neighbors[i]);
        if (side && i) strbuf_append_char(label, 'A');
        DomElement* body = block(!side ? label->str : nullptr); ASSERT_NE(body, nullptr);
        DomElement* atom = image("width: 80px; height: 10px; white-space: nowrap", body); ASSERT_NE(atom, nullptr);
        DomElement* word = side ? block(label->str, nullptr, "span", body) : body;
        strbuf_free(label); ASSERT_NE(word, nullptr);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        TypesetStatus status = layout_secondary_view(tree, &options, &diagnostic);
        if (i < 2) {
            ASSERT_EQ(status, TYPESET_OK) << diagnostic.reason;
            LayoutViewNode* text = source_fragment(tree, word->first_child, VIEW_FRAGMENT_BODY, false);
            LayoutViewNode* fragment = source_image_fragment(tree, atom); ASSERT_NE(text, nullptr); ASSERT_NE(fragment, nullptr);
            if (side) EXPECT_GT(text->rect.y, fragment->rect.y);
            else EXPECT_GT(fragment->rect.y, text->rect.y);
            EXPECT_FLOAT_EQ(fragment->rect.x, 10.0f);
        } else EXPECT_EQ(status, TYPESET_UNPLACEABLE);
        ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
        ASSERT_TRUE(source->DomNode::remove_child(body));
    }
    DomElement* body = block(nullptr); ASSERT_NE(body, nullptr);
    DomElement* nowrap = block(nullptr, "white-space: nowrap", "span", body); ASSERT_NE(nowrap, nullptr);
    ASSERT_NE(image("width: 50px; height: 10px; white-space: normal", nowrap), nullptr);
    ASSERT_NE(image("width: 50px; height: 10px; white-space: normal", nowrap), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    EXPECT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_UNPLACEABLE);
}

TEST_F(SecondaryViewTest, AtomicWrapCrossesSourceOnlyAnchorsAndTrimsNowrapInlineSpaces) {
    init_vector_engine();
    stylesheet("@page { size: 100px 160px; margin: 10px } div { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 }");
    DomElement* body = block(nullptr); ASSERT_NE(body, nullptr);
    DomElement* first = image("width: 50px; height: 10px", body); ASSERT_NE(first, nullptr);
    ASSERT_NE(block(nullptr, "display: none; string-set: Title 'Hidden'", "span", body), nullptr);
    DomElement* second = image("width: 50px; height: 30px", body); ASSERT_NE(second, nullptr);
    DomElement* text = block("A ", "white-space: nowrap", "span", body); ASSERT_NE(text, nullptr);
    DomElement* third = image("width: 80px; height: 10px", body); ASSERT_NE(third, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* a = source_image_fragment(tree, first), *b = source_image_fragment(tree, second), *c = source_image_fragment(tree, third);
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(c, nullptr);
    EXPECT_GT(b->rect.y, a->rect.y + 10.0f); EXPECT_GT(c->rect.y, b->rect.y + 30.0f);
    EXPECT_FLOAT_EQ(c->rect.x, 10.0f);
    ViewNodeState* state = view_tree_node_state(tree, text->first_child, false); ASSERT_NE(state, nullptr);
    StrBuf* content = strbuf_new(); ASSERT_NE(content, nullptr);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) append_fragment_text(node, content);
    EXPECT_STREQ(content->str, "A"); strbuf_free(content);
}

TEST_F(SecondaryViewTest, InlineImagesUseTheBaselineAndClipCoverAndContainInTheirContentBoxes) {
    init_vector_engine();
    stylesheet("@page { size: 220px 150px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px }");
    DomElement* body = block("Text "); ASSERT_NE(body, nullptr);
    DomElement* cover = image("width: 20px; height: 20px; padding: 2px; border: 1px solid green; object-fit: cover; object-position: right top", body);
    DomElement* contain = image("display: block; width: 40px; height: 40px; object-fit: contain; background: yellow");
    ASSERT_NE(cover, nullptr); ASSERT_NE(contain, nullptr);
    ASSERT_TRUE(create_dir("temp/paged-media-images"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* image = source_image_fragment(tree, cover); ASSERT_NE(image, nullptr);
    LayoutViewNode* text = source_fragment(tree, body->first_child, VIEW_FRAGMENT_BODY, false); ASSERT_NE(text, nullptr);
    ASSERT_NE(text->glyph_run, nullptr);
    EXPECT_FLOAT_EQ(image->rect.width, 26.0f); EXPECT_FLOAT_EQ(image->rect.height, 26.0f);
    EXPECT_FLOAT_EQ(image->rect.y + image->rect.height, text->glyph_run->baseline_y);
    EXPECT_LT(image->image_box->image_rect.x, image->image_box->content_rect.x);
    EXPECT_FLOAT_EQ(image->image_box->image_rect.width, 40.0f);
    LayoutViewNode* contained = source_image_fragment(tree, contain); ASSERT_NE(contained, nullptr);
    EXPECT_FLOAT_EQ(contained->image_box->image_rect.width, 40.0f); EXPECT_FLOAT_EQ(contained->image_box->image_rect.height, 20.0f);
    ImageSurface* snapshot = render_secondary_page_snapshot(tree, 1, 2.0f); ASSERT_NE(snapshot, nullptr);
    Color blue = {snapshot_pixel(snapshot, (size_t)((image->image_box->content_rect.x + 5.0f) * 2.0f), (size_t)((image->image_box->content_rect.y + 5.0f) * 2.0f))};
    EXPECT_GT(blue.b, 200); EXPECT_LT(blue.r, 30);
    Color yellow = {snapshot_pixel(snapshot, (size_t)((contained->rect.x + 5.0f) * 2.0f), (size_t)((contained->rect.y + 5.0f) * 2.0f))};
    EXPECT_GT(yellow.r, 200); EXPECT_GT(yellow.g, 200); EXPECT_LT(yellow.b, 30);
    save_surface_to_png(snapshot, "temp/paged-media-images/image-fit-native.png"); image_surface_destroy(snapshot);
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-images/image-fit.pdf"));
}

TEST_F(SecondaryViewTest, ImageAssetsBelongToEachEditionAndSurviveTheRetainedPageLease) {
    init_vector_engine();
    stylesheet("@page { size: 160px 100px; margin: 10px } p { margin: 0 }");
    DomElement* image = this->image(); ASSERT_NE(image, nullptr);
    ViewTree* a = secondary(), *b = secondary(); ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr);
    PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(a, &options, nullptr), TYPESET_OK);
    ASSERT_EQ(layout_secondary_view(b, &options, nullptr), TYPESET_OK);
    LayoutViewNode* first = source_image_fragment(a, image), *second = source_image_fragment(b, image);
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    ImageSurface* old = first->image_box->image; auto handle = old->self;
    EXPECT_NE(old, second->image_box->image.get()); EXPECT_EQ(image->embed, nullptr);
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(a, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_model_reset(a)); EXPECT_EQ(image_surface_lookup(handle), old);
    ASSERT_EQ(layout_secondary_view(a, &options, nullptr), TYPESET_OK);
    EXPECT_NE(source_image_fragment(a, image)->image_box->image.get(), old);
    ASSERT_TRUE(view_tree_secondary_release(&doc, a)); EXPECT_EQ(image_surface_lookup(handle), old);
    ImageSurface* snapshot = render_secondary_page_snapshot(preview, 1, 2.0f); ASSERT_NE(snapshot, nullptr);
    image_surface_destroy(snapshot);
    ASSERT_TRUE(view_tree_secondary_release(&doc, preview)); EXPECT_EQ(image_surface_lookup(handle), nullptr);
    EXPECT_NE(source_image_fragment(b, image)->image_box->image, nullptr);
}

TEST_F(SecondaryViewTest, ImageSizingUsesHtmlHintsAndRatioConstraintsWithoutStretchingBothAutoAxes) {
    init_vector_engine();
    stylesheet("@page { size: 240px 200px; margin: 10px } p, div { margin: 0 }");
    const char* styles[] = {"display: block", "display: block; max-width: 10px", "display: block; min-width: 30px; max-height: 10px",
        "display: block; width: 30px; aspect-ratio: 1 / 1", "display: block; width: 30px; aspect-ratio: auto 1 / 1",
        "display: block; width: 32px; height: 22px; box-sizing: border-box; padding: 1px; border: 2px solid blue"};
    const float widths[] = {40.0f, 10.0f, 30.0f, 30.0f, 30.0f, 26.0f};
    const float heights[] = {20.0f, 5.0f, 10.0f, 30.0f, 15.0f, 16.0f};
    DomElement* images[6] = {};
    for (size_t i = 0; i < 6; i++) {
        images[i] = image(styles[i]); ASSERT_NE(images[i], nullptr);
    }
    ASSERT_TRUE(images[0]->set_attribute("width", "40"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    for (size_t i = 0; i < 6; i++) {
        SCOPED_TRACE(i); LayoutViewNode* fragment = source_image_fragment(tree, images[i]); ASSERT_NE(fragment, nullptr);
        EXPECT_FLOAT_EQ(fragment->image_box->content_rect.width, widths[i]);
        EXPECT_FLOAT_EQ(fragment->image_box->content_rect.height, heights[i]);
    }
}

TEST_F(SecondaryViewTest, ImagesMoveWithTheBodyWhenLateNotesAndTopFloatsReplayAKeepChain) {
    init_vector_engine();
    stylesheet("@page { size: 180px 120px; margin: 10px } p, div, span { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 }");
    ASSERT_NE(block("Prelude", "height: 36px"), nullptr);
    DomElement* body = block("Body ", "break-after: avoid"); ASSERT_NE(body, nullptr);
    DomElement* image = this->image("width: 60px; height: auto", body); ASSERT_NE(image, nullptr);
    ASSERT_TRUE(create_dir("temp/paged-media-images"));
    DomElement* call = block("Call "); ASSERT_NE(call, nullptr);
    // the line policy reserves the complete note before accepting the sibling keep chain.
    ASSERT_NE(block("Note A\nNote B", "float: footnote; footnote-policy: line; white-space: pre-wrap", "span", call), nullptr);
    ASSERT_NE(block("Float", "float: top; float-reference: page; height: 18px; color: green"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* fragment = source_image_fragment(tree, image); ASSERT_NE(fragment, nullptr);
    EXPECT_EQ(occurrence_page(fragment), 2u);
    EXPECT_GE(fragment->image_box->content_rect.y, 28.0f);
    EXPECT_FLOAT_EQ(fragment->rect.y, fragment->image_box->content_rect.y);
    size_t images = 0;
    ViewNodeState* state = view_tree_node_state(tree, image, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) if (node->image_box) images++;
    EXPECT_EQ(images, 1u);
    ImageSurface* snapshot = render_secondary_page_snapshot(tree, 2, 2.0f); ASSERT_NE(snapshot, nullptr);
    Color blue = {snapshot_pixel(snapshot, (size_t)((fragment->rect.x + 40.0f) * 2.0f), (size_t)((fragment->rect.y + 15.0f) * 2.0f))};
    EXPECT_GT(blue.b, 200); EXPECT_LT(blue.r, 30);
    save_surface_to_png(snapshot, "temp/paged-media-images/image-rollback-native.png"); image_surface_destroy(snapshot);
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-images/image-rollback.pdf"));
}

TEST_F(SecondaryViewTest, OversizedAndDeniedImagesReturnSourceDiagnosticsWithoutFragmentation) {
    init_vector_engine();
    stylesheet("@page { size: 160px 80px; margin: 10px } p { margin: 0 }");
    DomElement* image = this->image("display: block; width: 40px; height: 90px"); ASSERT_NE(image, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(diagnostic.source.expected_id, dom_node_ref(image).expected_id);
    EXPECT_LT(tree->model->page_count, 3u); EXPECT_FALSE(tree->model->committed);
    ASSERT_TRUE(image->set_attribute("src", "https://example.test/forbidden.png"));
    doc.resource_policy = INPUT_RESOURCE_LOCAL_ONLY; diagnostic = {};
    ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(diagnostic.source.expected_id, dom_node_ref(image).expected_id);
    EXPECT_STREQ(diagnostic.reason, "image source is unavailable or requires a sampled media producer");
}

TEST_F(SecondaryViewTest, ImagesUseTheSameProducerInFootnotesAndRepeatedRunningFurniture) {
    init_vector_engine();
    stylesheet("@page { size: 180px 140px; margin: 20px; @top-left { content: element(logo) } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 }");
    DomElement* logo = block(nullptr, "position: running(logo)", "div"); ASSERT_NE(logo, nullptr);
    DomElement* running_image = image("width: 20px; height: auto", logo); ASSERT_NE(running_image, nullptr);
    ASSERT_TRUE(create_dir("temp/paged-media-images"));
    DomElement* body = block("Statement "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note ", "float: footnote", "span", body); ASSERT_NE(note, nullptr);
    DomElement* note_image = image("width: 20px; height: auto", note); ASSERT_NE(note_image, nullptr);
    ASSERT_NE(block("Next page", "break-before: page"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* footnote = source_image_fragment(tree, note_image); ASSERT_NE(footnote, nullptr);
    EXPECT_EQ(footnote->role, VIEW_FRAGMENT_NOTE); EXPECT_EQ(occurrence_page(footnote), 1u);
    ViewNodeState* state = view_tree_node_state(tree, running_image, false); ASSERT_NE(state, nullptr);
    size_t logos = 0;
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) if (node->image_box) {
        logos++; EXPECT_EQ(node->role, VIEW_FRAGMENT_RUNNING);
        EXPECT_LT(node->image_box->content_rect.y, 20.0f);
        EXPECT_EQ(node->image_box->image.get(), footnote->image_box->image.get());
    }
    EXPECT_EQ(logos, 2u);
    EXPECT_EQ(note_image->embed, nullptr); EXPECT_EQ(running_image->embed, nullptr);
    ImageSurface* snapshot = render_secondary_page_snapshot(tree, 1, 2.0f); ASSERT_NE(snapshot, nullptr);
    Color blue = {snapshot_pixel(snapshot, (size_t)((footnote->rect.x + 15.0f) * 2.0f), (size_t)((footnote->rect.y + 5.0f) * 2.0f))};
    EXPECT_GT(blue.b, 200); EXPECT_LT(blue.r, 30);
    save_surface_to_png(snapshot, "temp/paged-media-images/image-regions-native.png"); image_surface_destroy(snapshot);
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-images/image-regions.pdf"));
}

TEST_F(SecondaryViewTest, PagedImagesRemainAtomicWhenTheFollowingPageChangesWidth) {
    init_vector_engine();
    // a fresh raster export must register its SVG backend without a prior PDF or SVG export.
    paint_ir_register_svg_subscene_lowerers(nullptr, nullptr);
    stylesheet("@page { size: 180px 100px; margin: 10px } @page :left { size: 260px 100px } "
        "p, div { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 }");
    ASSERT_NE(block("Prelude", "height: 64px"), nullptr);
    DomElement* image = this->image("display: block; width: 50%; height: auto", nullptr,
        "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='80' height='20'%3E%3Crect width='80' height='20' fill='blue'/%3E%3C/svg%3E");
    ASSERT_NE(image, nullptr);
    image->x = 71.25f; image->width = 123.5f;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_FLOAT_EQ(image->x, 71.25f); EXPECT_FLOAT_EQ(image->width, 123.5f);
    EXPECT_EQ(image->embed, nullptr);
    ViewNodeState* state = view_tree_node_state(tree, image, false); ASSERT_NE(state, nullptr);
    size_t visible = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        if (!fragment->paint_box) continue;
        visible++; EXPECT_EQ(occurrence_page(fragment), 2u);
        EXPECT_FLOAT_EQ(fragment->rect.width, 120.0f); EXPECT_FLOAT_EQ(fragment->rect.height, 30.0f);
    }
    EXPECT_EQ(visible, 1u);
    ASSERT_TRUE(create_dir("temp/paged-media-images"));

    ImageSurface* snapshot = render_secondary_page_snapshot(tree, 2, 2.0f); ASSERT_NE(snapshot, nullptr);
    Color blue = {snapshot_pixel(snapshot, 40, 40)};
    EXPECT_GT(blue.b, 200); EXPECT_LT(blue.r, 30); EXPECT_LT(blue.g, 30);
    save_surface_to_png(snapshot, "temp/paged-media-images/changed-width-image-native.png");
    image_surface_destroy(snapshot);
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-images/changed-width-image.pdf"));
}

TEST_F(SecondaryViewTest, OutsideListMarkersFollowFirstContentAcrossPageRollback) {
    stylesheet("@page { size: 160px 56px; margin: 8px } "
        "div, ol, li { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap } "
        "ol { padding-left: 24px } li { orphans: 2; widows: 1 } li::marker { color: blue }");
    ASSERT_NE(block("Prelude", "height: 24px"), nullptr);
    DomElement* list = block(nullptr, nullptr, "ol"); ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->set_attribute("start", "4"));
    DomElement* first = block("A\nB\nC\nD", nullptr, "li", list); ASSERT_NE(first, nullptr);
    DomElement* second = block("E", nullptr, "li", list); ASSERT_NE(second, nullptr);
    first->x = 71.0f; first->width = 83.0f;
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
    PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr);
    size_t markers = 0;
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) {
        if (!node->computed_style || node->computed_style->pseudo_element != PSEUDO_ELEMENT_MARKER || !node->glyph_run) continue;
        markers++;
        EXPECT_STREQ(node->glyph_run->text, "4. "); EXPECT_EQ(node->computed_style->color.b, 255);
        EXPECT_EQ(occurrence_page(node), 2u);
        LayoutViewNode* content = source_fragment(tree, first->first_child, VIEW_FRAGMENT_BODY, false);
        ASSERT_NE(content, nullptr); EXPECT_FLOAT_EQ(node->rect.y, content->rect.y);
        EXPECT_LE(node->rect.x + node->rect.width, content->rect.x);
    }
    EXPECT_EQ(markers, 1u);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    append_fragment_text(tree->model->root, text);
    EXPECT_NE(strstr(text->str, "5. "), nullptr); strbuf_free(text);
    EXPECT_FLOAT_EQ(first->x, 71.0f); EXPECT_FLOAT_EQ(first->width, 83.0f);
    ASSERT_TRUE(create_dir("temp/paged-media-lists"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-lists/outside.pdf"));
}

TEST_F(SecondaryViewTest, NestedListCountersAndValueHintsRemainInLogicalOrder) {
    stylesheet("@page { size: 200px 100px; margin: 8px } "
        "ol, li { margin: 0; font-size: 10px; line-height: 12px } ol { padding-left: 24px } "
        "li::marker { content: '(' counters(list-item, '.') ') ' }");
    DomElement* list = block(nullptr, nullptr, "ol"); ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->set_attribute("start", "3"));
    DomElement* first = block("First", nullptr, "li", list); ASSERT_NE(first, nullptr);
    DomElement* nested = block(nullptr, nullptr, "ol", first); ASSERT_NE(nested, nullptr);
    ASSERT_NE(block("Nested", nullptr, "li", nested), nullptr);
    DomElement* second = block("Second", nullptr, "li", list); ASSERT_NE(second, nullptr);
    ASSERT_TRUE(second->set_attribute("value", "8"));
    ASSERT_NE(block("Third", nullptr, "li", list), nullptr);
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
    PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    append_fragment_text(tree->model->root, text);
    EXPECT_STREQ(text->str, "(3) First(3.1) Nested(8) Second(9) Third"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, ReversedListMarkersExcludeHiddenItemsAndNestedScopes) {
    stylesheet("@page { size: 200px 68px; margin: 8px } ol, li { margin: 0; font-size: 10px; line-height: 12px }");
    DomElement* list = block(nullptr, nullptr, "ol"); ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->set_attribute("reversed", ""));
    DomElement* first = block("First", nullptr, "li", list); ASSERT_NE(first, nullptr);
    ASSERT_NE(block("Hidden", "display: none", "li", list), nullptr);
    DomElement* nested = block(nullptr, nullptr, "ol", first); ASSERT_NE(nested, nullptr);
    ASSERT_NE(block("Nested", nullptr, "li", nested), nullptr);
    ASSERT_NE(block("Second", nullptr, "li", list), nullptr);
    ASSERT_NE(block("Third", nullptr, "li", list), nullptr);
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {}; PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(tree->model->root, text);
    EXPECT_STREQ(text->str, "3. First1. Nested2. Second1. Third"); strbuf_free(text);
    ASSERT_TRUE(list->set_attribute("start", "7"));
    ViewTree* changed = secondary();
    ASSERT_EQ(layout_secondary_view(changed, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(changed->model->root, text);
    EXPECT_STREQ(text->str, "7. First1. Nested6. Second5. Third"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, InsideListMarkerShorthandAndEmptyItemsUseInlineFlow) {
    stylesheet("@page { size: 160px 80px; margin: 8px } ol, li { margin: 0; font-size: 10px; line-height: 12px } "
        "ol { padding-left: 0; list-style: upper-roman inside } .custom::marker { content: 'Note: '; color: red }");
    DomElement* list = block(nullptr, nullptr, "ol"); ASSERT_NE(list, nullptr);
    DomElement* first = block("Alpha", nullptr, "li", list); ASSERT_NE(first, nullptr);
    DomElement* empty = block(nullptr, nullptr, "li", list); ASSERT_NE(empty, nullptr);
    DomElement* third = block("Beta", nullptr, "li", list); ASSERT_NE(third, nullptr);
    ASSERT_TRUE(third->set_attribute("class", "custom"));
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {}; PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(tree->model->root, text);
    EXPECT_STREQ(text->str, "I. AlphaII. Note: Beta"); strbuf_free(text);
    LayoutViewNode* content = source_fragment(tree, first->first_child, VIEW_FRAGMENT_BODY, false); ASSERT_NE(content, nullptr);
    EXPECT_GT(content->rect.x, tree->model->pages.get()[0]->content_rect.x);
    LayoutViewNode* empty_box = source_fragment(tree, empty, VIEW_FRAGMENT_BODY, true); ASSERT_NE(empty_box, nullptr);
    EXPECT_GT(empty_box->rect.height, 0.0f);
}

TEST_F(SecondaryViewTest, ListCounterCssOverridesHintsAndRunsResetIncrementSetBeforeMarker) {
    stylesheet("@page { size: 200px 100px; margin: 8px } ol, li { margin: 0; font-size: 10px; line-height: 12px } "
        "li::before { content: '[' counter(list-item) ']' }");
    DomElement* list = block(nullptr, "counter-reset: list-item 9", "ol"); ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->set_attribute("start", "3"));
    DomElement* first = block("First", "counter-increment: list-item 2", "li", list); ASSERT_NE(first, nullptr);
    DomElement* second = block("Second", "counter-set: list-item 20", "li", list); ASSERT_NE(second, nullptr);
    ASSERT_TRUE(second->set_attribute("value", "8"));
    ASSERT_NE(block("Third", "counter-increment: list-item 0; list-style-type: none", "li", list), nullptr);
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {}; PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(tree->model->root, text);
    EXPECT_STREQ(text->str, "11. [11]First20. [20]Second[20]Third"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, OutsideListMarkersPreserveLeadingSpaceCollapseAndNestedBlockAlignment) {
    stylesheet("@page { size: 200px 100px; margin: 8px } ol, li, div { margin: 0; font-size: 10px; line-height: 12px }");
    DomElement* list = block(nullptr, nullptr, "ol"); ASSERT_NE(list, nullptr);
    DomElement* item = block(nullptr, nullptr, "li", list); ASSERT_NE(item, nullptr);
    DomElement* child = block(" \n\tAlpha", "margin-left: 12px", "div", item); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {}; PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* state = view_tree_node_state(tree, child->first_child, false); ASSERT_NE(state, nullptr);
    LayoutViewNode* ink = nullptr;
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) if (node->glyph_run) { ink = node; break; }
    ASSERT_NE(ink, nullptr);
    EXPECT_FLOAT_EQ(ink->rect.x, 8.0f + 40.0f + 12.0f);
    state = view_tree_node_state(tree, item, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) if (node->glyph_run) {
        EXPECT_STREQ(node->glyph_run->text, "1. "); EXPECT_FLOAT_EQ(node->rect.y, ink->rect.y);
        EXPECT_FLOAT_EQ(node->rect.x + node->rect.width, 8.0f + 40.0f);
    }
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(tree->model->root, text);
    EXPECT_STREQ(text->str, "1. Alpha"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, SharedPropertyAllocationBelongsToTheExplicitlySelectedView) {
    ViewTree* tree = secondary(); ViewTree* sibling = secondary();
    LayoutContext context = {}; context.doc = lam::up(&doc); context.pool = lam::up(doc.document_pool.get());
    context.selected_view_tree = lam::up(tree);
    void* property = alloc_prop(&context, 8); ASSERT_NE(property, nullptr);
    EXPECT_TRUE(pool_owns(tree->prop_pool, property)); EXPECT_FALSE(pool_owns(doc.view_tree->prop_pool, property));
    EXPECT_FALSE(pool_owns(sibling->prop_pool, property));
    ViewTree* browsing = doc.view_tree; doc.view_tree = nullptr;
    void* headless_property = alloc_prop(&context, 8);
    doc.view_tree = lam::own(browsing);
    ASSERT_NE(headless_property, nullptr); EXPECT_TRUE(pool_owns(tree->prop_pool, headless_property));
    context.selected_view_tree = nullptr;
    void* default_property = alloc_prop(&context, 8); ASSERT_NE(default_property, nullptr);
    EXPECT_TRUE(pool_owns(doc.view_tree->prop_pool, default_property));
}

TEST_F(SecondaryViewTest, ModelRollbackRestoresNestedLinksAndNeverReusesDiscardedHandles) {
    DomElement* other = block("other"); ASSERT_NE(other, nullptr);
    ViewTree* tree = secondary(); ViewTree* sibling = secondary();
    ViewPageBox* page = view_tree_page_append(tree, 100.0f, 200.0f, {0, 0, 100, 200}, VIEW_PAGE_RIGHT); ASSERT_NE(page, nullptr);
    LayoutViewNode* first = view_tree_fragment_append(tree, &page->node, source, {0, 0, 10, 12}); ASSERT_NE(first, nullptr);
    PaintGlyphRun* run = (PaintGlyphRun*)arena_alloc(tree->model->arena, sizeof(PaintGlyphRun)); ASSERT_NE(run, nullptr);
    *run = {}; run->baseline_y = 9.0f; first->glyph_run = lam::up(run);
    LayoutViewRef kept = first->ref;
    ViewNodeState* state = view_tree_node_state(tree, source, false); ASSERT_NE(state, nullptr);
    size_t nodes = tree->model->node_count;
    ViewModelCheckpoint* outer = view_tree_model_checkpoint(tree); ASSERT_NE(outer, nullptr);
    ASSERT_TRUE(view_tree_model_touch_node(tree, first)); first->rect.y = 30; run->baseline_y = 39;
    LayoutViewNode* discarded = view_tree_fragment_append(tree, &page->node, source, {0, 30, 10, 12}); ASSERT_NE(discarded, nullptr);
    LayoutViewRef removed = discarded->ref;
    ViewModelCheckpoint* inner = view_tree_model_checkpoint(tree); ASSERT_NE(inner, nullptr);
    EXPECT_FALSE(view_tree_model_accept(tree, outer)); EXPECT_FALSE(view_tree_model_commit(tree));
    ViewPageBox* extra = view_tree_page_append(tree, 100, 200, {0, 0, 100, 200}, VIEW_PAGE_LEFT); ASSERT_NE(extra, nullptr);
    LayoutViewRef removed_page = extra->node.ref;
    ASSERT_NE(view_tree_fragment_append(tree, &extra->node, other, {0, 0, 10, 12}), nullptr);
    ASSERT_TRUE(view_tree_model_accept(tree, inner));
    ASSERT_TRUE(view_tree_model_restore(tree, outer));
    EXPECT_EQ(tree->model->node_count, nodes); EXPECT_EQ(tree->model->page_count, 1u);
    EXPECT_EQ(view_tree_node_resolve(tree, kept), first); EXPECT_EQ(view_tree_node_resolve(tree, removed), nullptr);
    EXPECT_EQ(view_tree_node_resolve(tree, removed_page), nullptr);
    EXPECT_FLOAT_EQ(first->rect.y, 0); EXPECT_FLOAT_EQ(run->baseline_y, 9);
    EXPECT_EQ(page->node.first_child.get(), first); EXPECT_EQ(page->node.last_child.get(), first);
    EXPECT_EQ(first->next_sibling, nullptr); EXPECT_EQ(first->next_occurrence, nullptr);
    EXPECT_EQ(state->first_occurrence.get(), first); EXPECT_EQ(state->last_occurrence.get(), first); EXPECT_EQ(state->occurrence_count, 1u);
    EXPECT_EQ(view_tree_node_state(tree, other, false), nullptr); EXPECT_EQ(page->node.next_sibling, nullptr);
    EXPECT_EQ(sibling->model->node_count, 1u);
    LayoutViewNode* replacement = view_tree_fragment_append(tree, &page->node, source, {0, 12, 10, 12}); ASSERT_NE(replacement, nullptr);
    EXPECT_GT(replacement->ref.node_id, removed_page.node_id); EXPECT_EQ(view_tree_node_resolve(tree, removed), nullptr);
    ASSERT_TRUE(view_tree_model_commit(tree)); EXPECT_EQ(view_tree_model_checkpoint(tree), nullptr);
}

TEST_F(SecondaryViewTest, UnimplementedContextsCannotPublishFlattenedPages) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0 }");
    DomElement* child = block("context content"); ASSERT_NE(child, nullptr);
    const char* declarations[] = {"display: flex", "display: grid", "display: inline-block",
        "display: inline flow-root list-item", "column-count: 2", "column-width: 60px", "position: absolute", "float: left"};
    PagedLayoutOptions options = paged_layout_options_default();
    for (const char* declaration : declarations) {
        ASSERT_TRUE(child->set_attribute("style", declaration));
        ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID) << declaration;
        EXPECT_EQ(diagnostic.source.address, child) << declaration;
        EXPECT_NE(diagnostic.reason, nullptr) << declaration;
        EXPECT_FALSE(tree->model->committed) << declaration;
    }
    ASSERT_TRUE(child->set_attribute("style", "display: none; float: left"));
    ASSERT_TRUE(source->set_attribute("style", "display: grid"));
    ViewTree* root = secondary(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(root, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, source);
}

TEST_F(SecondaryViewTest, SourceFontShorthandAndBlockBordersBelongToTheSelectedView) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 16px Arial } "
        "div { border: 2px solid black; padding: 3px; height: 80px; box-sizing: border-box }");
    DomElement* child = block("border content"); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewCssStyle* style = view_css_resolve(tree, source); ASSERT_NE(style, nullptr);
    EXPECT_STREQ(style->font.family.get(), "Arial");
    ViewNodeState* state = view_tree_node_state(tree, child, false); ASSERT_NE(state, nullptr);
    LayoutViewNode* box = state->first_occurrence; ASSERT_NE(box, nullptr);
    ASSERT_NE(box->computed_boundary, nullptr);
    ASSERT_NE(box->computed_boundary->border, nullptr);
    EXPECT_FLOAT_EQ(box->computed_boundary->border->width.left, 2.0f);
    EXPECT_FLOAT_EQ(box->rect.height, 80.0f);
    EXPECT_EQ(child->x, 0.0f); EXPECT_EQ(child->width, 0.0f);
    DomNode* text = child->first_child; ASSERT_NE(text, nullptr);
    ViewNodeState* text_state = view_tree_node_state(tree, text, false); ASSERT_NE(text_state, nullptr);
    EXPECT_FLOAT_EQ(text_state->first_occurrence->rect.x, box->rect.x + 5.0f);
}

TEST_F(SecondaryViewTest, SlicedAndClonedBordersReserveOnlyTheirOccurrenceEdges) {
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font-size: 8px; line-height: 18px } "
        "div { white-space: pre; border: 3px solid blue; padding: 5px; orphans: 1; widows: 1 } "
        "@media (max-width: 500px) { div { box-decoration-break: clone } }");
    DomElement* child = block("a\nb\nc\nd\ne"); ASSERT_NE(child, nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* slice = secondary();
    ASSERT_EQ(layout_secondary_view(slice, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(slice->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(slice, child, false); ASSERT_NE(state, nullptr);
    LayoutViewNode* first = source_fragment(slice, child, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* last = source_fragment(slice, child, VIEW_FRAGMENT_BODY, true, true);
    ASSERT_NE(first, nullptr); ASSERT_NE(last, nullptr); ASSERT_NE(first, last);
    ASSERT_NE(first->computed_boundary, nullptr); ASSERT_NE(last->computed_boundary, nullptr);
    EXPECT_FLOAT_EQ(first->computed_boundary->border->width.top, 3.0f);
    EXPECT_FLOAT_EQ(first->computed_boundary->border->width.bottom, 0.0f);
    EXPECT_FLOAT_EQ(last->computed_boundary->border->width.top, 0.0f);
    EXPECT_FLOAT_EQ(last->computed_boundary->border->width.bottom, 3.0f);
    EXPECT_FLOAT_EQ(first->rect.height, 80.0f); EXPECT_FLOAT_EQ(last->rect.height, 26.0f);
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED);
    environment.viewport_width = 400.0f;
    ViewTree* clone = view_tree_secondary_create(&doc, &environment); ASSERT_NE(clone, nullptr);
    ASSERT_EQ(layout_secondary_view(clone, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(clone->model->page_count, 2u);
    state = view_tree_node_state(clone, child, false); ASSERT_NE(state, nullptr);
    first = source_fragment(clone, child, VIEW_FRAGMENT_BODY, true);
    last = source_fragment(clone, child, VIEW_FRAGMENT_BODY, true, true);
    ASSERT_NE(first, nullptr); ASSERT_NE(last, nullptr); ASSERT_NE(first, last);
    EXPECT_FLOAT_EQ(first->rect.height, 70.0f); EXPECT_FLOAT_EQ(last->rect.height, 52.0f);
    for (LayoutViewNode* box : {first, last}) {
        ASSERT_NE(box->computed_boundary, nullptr);
        EXPECT_FLOAT_EQ(box->computed_boundary->border->width.top, 3.0f);
        EXPECT_FLOAT_EQ(box->computed_boundary->border->width.bottom, 3.0f);
        EXPECT_LE(box->rect.y + box->rect.height, 90.0f);
    }
    // The retained slice's edge metrics do not change when another edition uses clone.
    EXPECT_FLOAT_EQ(view_tree_node_state(slice, child, false)->first_occurrence->computed_boundary->border->width.bottom, 0.0f);
    ASSERT_TRUE(render_secondary_view_to_pdf(slice, "temp/paged-media-impl/border-slice.pdf"));
    ASSERT_TRUE(render_secondary_view_to_pdf(clone, "temp/paged-media-impl/border-clone.pdf"));
}

TEST_F(SecondaryViewTest, MultiwordFontFamiliesRetainFallbackGroupsAndViewOwnership) {
    stylesheet("p, span { font: italic 18px/20px Times New Roman, monospace }");
    ViewTree* first = secondary(); ViewCssStyle* style = view_css_resolve(first, source); ASSERT_NE(style, nullptr);
    EXPECT_STREQ(style->font.family.get(), "Times New Roman, monospace");
    EXPECT_FLOAT_EQ(style->font.font_size, 18.0f); EXPECT_FLOAT_EQ(style->line_height, 20.0f);
    EXPECT_EQ(style->font.font_style, CSS_VALUE_ITALIC);
    ASSERT_TRUE(source->set_attribute("style", "font-family: Courier New, serif"));
    ViewTree* second = secondary(); style = view_css_resolve(second, source); ASSERT_NE(style, nullptr);
    EXPECT_STREQ(style->font.family.get(), "Courier New, serif");
    ASSERT_TRUE(view_tree_secondary_release(&doc, first));
    EXPECT_STREQ(style->font.family.get(), "Courier New, serif");
    EXPECT_EQ(source->font.get(), nullptr);
}

TEST_F(SecondaryViewTest, FileLoadedGroupedSelectorsRetainTheirFontShorthand) {
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    const char* path = "temp/paged-media-impl/grouped-font.html";
    write_text_file(path, "<!doctype html><html><head><style>html,body { margin:0; font:16px Arial }"
        "@page { size:240px 180px; margin:10px }</style></head><body><p>Font sample</p></body></html>");
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin(&session, path, 0, 0, 800, 1200, 1.0f, true, true));
    ViewTree* tree = session.paged_view;
    ASSERT_NE(tree, nullptr);
    ViewCssStyle* root = view_css_resolve(tree, session.document->root); ASSERT_NE(root, nullptr);
    CssDeclaration declaration = {};
    EXPECT_TRUE(css_select_element_declaration(tree->model->css->engine, tree->model->css->matcher,
        session.document->root, session.document->stylesheets, session.document->stylesheet_count,
        nullptr, 0, "font-family", &declaration)) << session.document->root->tag_name;
    EXPECT_STREQ(declaration.property_name, "font");
    if (declaration.value) {
        EXPECT_EQ(declaration.value->type, CSS_VALUE_TYPE_LIST);
        EXPECT_NE(css_font_shorthand_longhand(declaration.value, "font-family", tree->model->css->pool), nullptr);
    }
    const CssValue* family = view_css_property(tree, root, "font-family");
    ASSERT_NE(family, nullptr);
    EXPECT_STREQ(css_font_family_name_from_value(family->type == CSS_VALUE_TYPE_LIST ? family->data.list.values[0] : family), "Arial");
    EXPECT_STREQ(root->font.family.get(), "Arial");
    for (ViewCssStyle* style = tree->model->css->styles; style; style = style->next) {
        if (!style->pseudo_element && strcmp(style->source->tag_name, "p") == 0)
            EXPECT_STREQ(style->font.family.get(), "Arial");
    }
    render_export_session_end(&session);
}

TEST_F(SecondaryViewTest, TableCaptionsUseATransparentWrapperOutsideGridDecorations) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 0; vertical-align: top } caption, .caption { padding: 2px; border: 1px solid red }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 50%; margin: 8px auto; "
        "border: 2px solid blue; padding: 3px; border-spacing: 0; background: yellow; caption-side: bottom", "table");
    ASSERT_NE(table, nullptr);
    DomElement* bottom = block("Bottom", "margin-bottom: 7px", "caption", table);
    ASSERT_NE(bottom, nullptr);
    ASSERT_TRUE(table_rows(table, 1));
    DomElement* top = block("Top", "display: table-caption; caption-side: initial; padding: 2px; border: 1px solid red", "div", table);
    ASSERT_NE(top, nullptr);
    DomElement* after = block("After"); ASSERT_NE(after, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* wrapper = source_fragment(tree, table, VIEW_FRAGMENT_BODY, false);
    LayoutViewNode* grid = source_fragment(tree, table, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* above = source_fragment(tree, top, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* below = source_fragment(tree, bottom, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(wrapper, nullptr); ASSERT_NE(grid, nullptr); ASSERT_NE(above, nullptr); ASSERT_NE(below, nullptr);
    EXPECT_EQ(above->parent, wrapper); EXPECT_EQ(grid->parent, wrapper); EXPECT_EQ(below->parent, wrapper);
    EXPECT_EQ(wrapper->first_child, above); EXPECT_EQ(above->next_sibling, grid); EXPECT_EQ(grid->next_sibling, below);
    EXPECT_FLOAT_EQ(grid->rect.width, 120.0f); EXPECT_FLOAT_EQ(grid->rect.x, 60.0f);
    EXPECT_FLOAT_EQ(above->rect.width, grid->rect.width); EXPECT_FLOAT_EQ(below->rect.width, grid->rect.width);
    EXPECT_FLOAT_EQ(above->rect.x, grid->rect.x); EXPECT_FLOAT_EQ(above->rect.y, 18.0f);
    EXPECT_FLOAT_EQ(grid->rect.y, above->rect.y + above->rect.height);
    EXPECT_FLOAT_EQ(below->rect.y, grid->rect.y + grid->rect.height);
    EXPECT_FLOAT_EQ(wrapper->rect.height, above->rect.height + grid->rect.height + below->rect.height + 7.0f);
    LayoutViewNode* next = source_fragment(tree, after, VIEW_FRAGMENT_BODY, true); ASSERT_NE(next, nullptr);
    EXPECT_FLOAT_EQ(next->rect.y, wrapper->rect.y + wrapper->rect.height + 8.0f);
    EXPECT_EQ(table->first_child, bottom); EXPECT_EQ(top->parent, table);
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, CaptionMinimumSizesBothAutomaticAndFixedTableGrids) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { padding: 0 }");
    for (bool fixed : {false, true}) {
        DomElement* table = block(nullptr, fixed ? "table-layout: fixed; width: 60px; border-spacing: 0" :
            "width: 60px; border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
        ASSERT_NE(block("Caption", "width: 120px; margin: 0 4px; padding: 2px; border: 1px solid red", "caption", table), nullptr);
        ASSERT_TRUE(table_rows(table, 1, "A", "B"));
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    DomElement* automatic = source->first_child->as_element(); DomElement* fixed = automatic->next_sibling->as_element();
    LayoutViewNode* auto_grid = source_fragment(tree, automatic, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* fixed_grid = source_fragment(tree, fixed, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(auto_grid, nullptr); ASSERT_NE(fixed_grid, nullptr);
    EXPECT_FLOAT_EQ(auto_grid->rect.width, 134.0f); EXPECT_FLOAT_EQ(fixed_grid->rect.width, 134.0f);
    EXPECT_FLOAT_EQ(auto_grid->first_child->rect.width, 134.0f);
}

TEST_F(SecondaryViewTest, TableCaptionsOccurOnceAroundRepeatingGroupsAtChangingPageWidths) {
    stylesheet("@page { size: 240px 140px; margin: 10px } @page :left { size: 200px 140px } "
        "p { margin: 0; font: 10px/12px Arial } td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr; DomElement* table = fixed_table(9, &header, &footer);
    ASSERT_NE(table, nullptr);
    DomElement* top = block("Above", nullptr, "caption", table); ASSERT_NE(top, nullptr);
    DomElement* bottom = block("Below", "caption-side: bottom", "caption", table); ASSERT_NE(bottom, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    LayoutViewNode* above = source_fragment(tree, top, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* below = source_fragment(tree, bottom, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(above, nullptr); ASSERT_NE(below, nullptr);
    EXPECT_EQ(occurrence_page(above), 1u); EXPECT_EQ(occurrence_page(below), 3u);
    EXPECT_TRUE(above->first_fragment && above->last_fragment); EXPECT_TRUE(below->first_fragment && below->last_fragment);
    ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
    size_t index = 0;
    for (LayoutViewNode* grid = state->first_occurrence; grid; grid = grid->next_occurrence) {
        if (!grid->paint_box) continue;
        EXPECT_FLOAT_EQ(grid->rect.width, index == 1 ? 180.0f : 220.0f);
        EXPECT_EQ(grid->first_fragment, index == 0); EXPECT_EQ(grid->last_fragment, index == 2); index++;
    }
    EXPECT_EQ(index, 3u);
    for (DomElement* group : {header, footer}) {
        ViewNodeState* rows = view_tree_node_state(tree, group->first_child, false); ASSERT_NE(rows, nullptr);
        EXPECT_EQ(rows->occurrence_count, 3u);
    }
}

TEST_F(SecondaryViewTest, TableBreaksApplyAroundBothCaptionsAndGrid) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { padding: 0 }");
    ASSERT_NE(block("Prelude"), nullptr);
    DomElement* table = block(nullptr, "width: 50%; border-spacing: 0; break-before: right; break-after: page", "table");
    ASSERT_NE(table, nullptr);
    DomElement* top = block("Above", nullptr, "caption", table); ASSERT_NE(top, nullptr);
    DomElement* bottom = block("Below", "caption-side: bottom", "caption", table); ASSERT_NE(bottom, nullptr);
    ASSERT_TRUE(table_rows(table, 1));
    DomElement* after = block("After"); ASSERT_NE(after, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 4u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, top, VIEW_FRAGMENT_BODY, true)), 3u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, table, VIEW_FRAGMENT_BODY, true)), 3u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, bottom, VIEW_FRAGMENT_BODY, true)), 3u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, after, VIEW_FRAGMENT_BODY, true)), 4u);
}

TEST_F(SecondaryViewTest, CaptionInheritanceAndGeneratedCountersPreserveSourceOrderWhenReordered) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "table { caption-side: bottom; counter-reset: chapter } caption { counter-increment: chapter } "
        "caption::before { content: counter(chapter) ':' }");
    DomElement* table = block(nullptr, "width: 160px; border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* bottom = block("Bottom", "caption-side: inherit", "caption", table); ASSERT_NE(bottom, nullptr);
    ASSERT_TRUE(table_rows(table, 1, "A", "B"));
    DomElement* top = block("Top", "caption-side: initial; text-align: left", "caption", table); ASSERT_NE(top, nullptr);
    DomElement* last = block("Last", "caption-side: unset", "caption", table); ASSERT_NE(last, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* above = source_fragment(tree, top, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* below = source_fragment(tree, bottom, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* final = source_fragment(tree, last, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(above, nullptr); ASSERT_NE(below, nullptr); ASSERT_NE(final, nullptr);
    EXPECT_LT(above->rect.y, below->rect.y); EXPECT_LT(below->rect.y, final->rect.y);
    EXPECT_EQ(above->computed_style->caption_side, CSS_VALUE_TOP); EXPECT_EQ(below->computed_style->caption_side, CSS_VALUE_BOTTOM);
    EXPECT_EQ(above->computed_style->text_align, CSS_VALUE_LEFT); EXPECT_EQ(below->computed_style->text_align, CSS_VALUE_CENTER);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    append_fragment_text(above, text); EXPECT_STREQ(text->str, "2:Top");
    strbuf_reset(text); append_fragment_text(below, text); EXPECT_STREQ(text->str, "1:Bottom");
    strbuf_reset(text); append_fragment_text(final, text); EXPECT_STREQ(text->str, "3:Last");
    strbuf_free(text);
}

TEST_F(SecondaryViewTest, CaptionContinuationsRollbackAndRetainedPagesKeepGridAndCaptionPaint) {
    init_vector_engine();
    stylesheet("@page { size: 140px 80px; margin: 10px } @page :left { size: 120px 80px } "
        "p { margin: 0; font: 10px/12px Arial } caption { white-space: pre; orphans: 1; widows: 1; "
        "padding: 1px; border: 1px solid blue; background: yellow } td { padding: 0 }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0; background: magenta", "table");
    ASSERT_NE(table, nullptr);
    DomElement* group = block(nullptr, nullptr, "colgroup", table); ASSERT_NE(group, nullptr);
    DomElement* column = block(nullptr, "background: red", "col", group); ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->set_attribute("span", "2"));
    const char* content = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL";
    DomElement* top = block(content, nullptr, "caption", table); ASSERT_NE(top, nullptr);
    ASSERT_TRUE(table_rows(table, 1, "A", "B"));
    DomElement* bottom = block(content, "caption-side: bottom; box-decoration-break: clone", "caption", table);
    ASSERT_NE(bottom, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    options.max_pages = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
    tree->reset_retained(); options.max_pages = 20;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_GE(tree->model->page_count, 5u);
    for (DomElement* caption : {top, bottom}) {
        ViewNodeState* state = view_tree_node_state(tree, caption->first_child, false); ASSERT_NE(state, nullptr);
        size_t bytes = 0;
        for (LayoutViewNode* text = state->first_occurrence; text; text = text->next_occurrence) {
            EXPECT_EQ(text->text_start, bytes); bytes += text->text_length;
        }
        EXPECT_EQ(bytes, strlen(content));
        LayoutViewNode* first = source_fragment(tree, caption, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* last = source_fragment(tree, caption, VIEW_FRAGMENT_BODY, true, true);
        ASSERT_NE(first, nullptr); ASSERT_NE(last, nullptr);
        EXPECT_TRUE(first->first_fragment); EXPECT_FALSE(first->last_fragment);
        EXPECT_FALSE(last->first_fragment); EXPECT_TRUE(last->last_fragment);
        EXPECT_LT(occurrence_page(first), occurrence_page(last));
    }
    ViewPreviewOptions preview = view_preview_options_default(); preview.columns = 3;
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ImageSurface* before = render_secondary_view_snapshot(retained); ASSERT_NE(before, nullptr);
    tree->reset_retained(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* after = render_secondary_view_snapshot(retained); ASSERT_NE(after, nullptr);
    EXPECT_EQ(before->width, after->width); EXPECT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
}

TEST_F(SecondaryViewTest, AutomaticCaptionPercentageWidthsResolveAfterTheGridMinimumSettles) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial }");
    DomElement* table = block(nullptr, "width: 120px; border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* caption = block("Caption", "width: 50%; margin: 0 auto", "caption", table); ASSERT_NE(caption, nullptr);
    ASSERT_TRUE(table_rows(table, 1, "A", "B"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* grid = source_fragment(tree, table, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* cap = source_fragment(tree, caption, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(grid, nullptr); ASSERT_NE(cap, nullptr);
    EXPECT_FLOAT_EQ(grid->rect.width, 120.0f); EXPECT_FLOAT_EQ(cap->rect.width, 60.0f);
    EXPECT_FLOAT_EQ(cap->rect.x, grid->rect.x + 30.0f);
}

TEST_F(SecondaryViewTest, AutomaticCaptionCyclicPercentagesRejectTheEditionWithASizingDiagnostic) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial }");
    DomElement* table = block(nullptr, "border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* caption = block("Caption", "padding-left: 50%", "caption", table); ASSERT_NE(caption, nullptr);
    ASSERT_TRUE(table_rows(table, 1, "A", "B"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_NE(strstr(diagnostic.reason, "cyclic descendant percentages"), nullptr);
    EXPECT_EQ(diagnostic.source.expected_id, dom_node_ref(caption).expected_id);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, FixedCaptionPercentagesAndKeepsUseTheGridBorderWidth) {
    stylesheet("@page { size: 160px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { padding: 0 }");
    ASSERT_NE(block("Prelude", "height: 54px"), nullptr);
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; box-sizing: border-box; "
        "padding: 3px; border: 2px solid blue; border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* caption = block("A\nB\nC", "white-space: pre; width: 50%; box-sizing: border-box; "
        "padding: 2px; border: 1px solid red; margin: 0 auto; break-inside: avoid", "caption", table);
    ASSERT_NE(caption, nullptr); ASSERT_TRUE(table_rows(table, 1, "A", "B"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* cap = source_fragment(tree, caption, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* grid = source_fragment(tree, table, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(cap, nullptr); ASSERT_NE(grid, nullptr);
    EXPECT_EQ(occurrence_page(cap), 2u); EXPECT_EQ(occurrence_page(grid), 2u);
    EXPECT_TRUE(cap->first_fragment && cap->last_fragment);
    EXPECT_FLOAT_EQ(grid->rect.width, 140.0f); EXPECT_FLOAT_EQ(cap->rect.width, 70.0f);
    EXPECT_FLOAT_EQ(cap->rect.x, 45.0f); EXPECT_FLOAT_EQ(cap->rect.height, 42.0f);
}

TEST_F(SecondaryViewTest, NestedTablesInsideCaptionsRejectTheEditionBeforeIntrinsicMeasurement) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial }");
    DomElement* table = block(nullptr, "border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* caption = block(nullptr, nullptr, "caption", table); ASSERT_NE(caption, nullptr);
    DomElement* nested = block(nullptr, "border-spacing: 0", "table", caption); ASSERT_NE(nested, nullptr);
    ASSERT_TRUE(table_rows(nested, 1, "A", "B")); ASSERT_TRUE(table_rows(table, 1, "C", "D"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_NE(strstr(diagnostic.reason, "nested and auxiliary tables"), nullptr);
    EXPECT_EQ(diagnostic.source.expected_id, dom_node_ref(nested).expected_id);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, FixedTablesBreakBetweenRowsAndRepeatSourceGroupsWithoutChangingTheDom) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(9, &header, &footer); ASSERT_NE(table, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_EQ(header->parent, table); EXPECT_EQ(footer->parent, table);
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
    for (DomElement* group : {header, footer}) {
        ViewNodeState* state = view_tree_node_state(tree, group->first_child, false); ASSERT_NE(state, nullptr);
        ASSERT_EQ(state->occurrence_count, 3u); size_t page = 0;
        for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
            EXPECT_EQ(occurrence_page(fragment), ++page);
            EXPECT_EQ(fragment->role, page == 1 ? VIEW_FRAGMENT_BODY : VIEW_FRAGMENT_REPEATED_TABLE);
            EXPECT_FLOAT_EQ(fragment->rect.height, 18.0f);
        }
    }
    DomElement* body = footer->next_sibling->as_element();
    size_t row_number = 0;
    for (DomNode* row = body->first_child; row; row = row->next_sibling) {
        ViewNodeState* state = view_tree_node_state(tree, row, false); ASSERT_NE(state, nullptr);
        ASSERT_EQ(state->occurrence_count, 1u);
        EXPECT_EQ(occurrence_page(state->first_occurrence), 1u + row_number++ / 4u);
        for (DomNode* cell = row->as_element()->first_child; cell; cell = cell->next_sibling) {
            DomText* content = cell->as_element()->first_child->as_text();
            ViewNodeState* text = view_tree_node_state(tree, content, false); ASSERT_NE(text, nullptr);
            size_t bytes = 0;
            for (LayoutViewNode* part = text->first_occurrence; part; part = part->next_occurrence) {
                EXPECT_EQ(part->text_start, bytes); bytes += part->text_length;
            }
            EXPECT_EQ(bytes, content->length);
        }
    }
}

TEST_F(SecondaryViewTest, FixedTableTracksUseFirstVisualRowAndRemeasureAtChangedPageWidths) {
    stylesheet("@page { size: 240px 140px; margin: 10px } @page :left { size: 200px 140px } "
        "p { margin: 0; font: 10px/12px Arial } td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(9, &header, &footer); ASSERT_NE(table, nullptr);
    DomElement* first = header->first_child->as_element()->first_child->as_element();
    ASSERT_TRUE(first->set_attribute("style", "width: 25%"));
    DomElement* later = footer->next_sibling->as_element()->first_child->as_element()->first_child->as_element();
    ASSERT_TRUE(later->set_attribute("style", "width: 1000px"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr);
    const float expected[] = {61.0f, 51.0f, 61.0f}; size_t page = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (fragment->paint_box) { ASSERT_LT(page, 3u); EXPECT_FLOAT_EQ(fragment->rect.width, expected[page++]); }
    EXPECT_EQ(page, 3u);
    ViewNodeState* later_state = view_tree_node_state(tree, later, false); ASSERT_NE(later_state, nullptr);
    EXPECT_FLOAT_EQ(later_state->first_occurrence->rect.width, 61.0f);
}

TEST_F(SecondaryViewTest, ImpossibleTableRowRejectsTheEditionWithoutPublishingHeaderOnlyPages) {
    stylesheet("@page { size: 240px 70px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    ASSERT_NE(fixed_table(1), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, AutoTableWidthsIncludeLaterRowsAndFootersAndCenterTheDecoratedTable) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(9, &header, &footer); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->set_attribute("style", "table-layout: auto; border-spacing: 0; margin: 0 auto; padding: 4px; border: 1px solid black"));
    DomElement* first = header->first_child->as_element()->first_child->as_element();
    DomElement* later = footer->next_sibling->as_element()->last_child->as_element()->first_child->as_element();
    DomElement* last = footer->first_child->as_element()->last_child->as_element();
    ASSERT_TRUE(first->set_attribute("style", "width: 20px"));
    ASSERT_TRUE(later->set_attribute("style", "width: 80px")); ASSERT_TRUE(last->set_attribute("style", "width: 100px"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        EXPECT_FLOAT_EQ(fragment->rect.width, 202.0f); EXPECT_FLOAT_EQ(fragment->rect.x, 19.0f);
    }
    state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (fragment->paint_box) EXPECT_FLOAT_EQ(fragment->rect.width, 86.0f);
    state = view_tree_node_state(tree, last, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (fragment->paint_box) EXPECT_FLOAT_EQ(fragment->rect.width, 106.0f);
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
    EXPECT_FLOAT_EQ(first->width, 0.0f); EXPECT_EQ(first->parent->parent, header);
}

TEST_F(SecondaryViewTest, AutoTablePercentTracksRemeasureOnAlternatingPages) {
    stylesheet("@page { size: 240px 140px; margin: 10px } @page :left { size: 200px 140px } "
        "p { margin: 0; font: 10px/12px Arial } td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* table = fixed_table(9, &header); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->set_attribute("style", "table-layout: auto; width: 100%; border-spacing: 0"));
    DomElement* first = header->first_child->as_element()->first_child->as_element();
    ASSERT_TRUE(first->set_attribute("style", "width: 25%"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    ViewNodeState* state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr);
    const float expected[] = {55.0f, 45.0f, 55.0f}; size_t page = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (fragment->paint_box) { ASSERT_LT(page, 3u); EXPECT_FLOAT_EQ(fragment->rect.width, expected[page++]); }
    EXPECT_EQ(page, 3u);
}

TEST_F(SecondaryViewTest, AutoTableColumnMinimaOverrideSmallerDeclaredTableAndMaximumWidths) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "width: 20px; max-width: 100px; box-sizing: border-box; padding: 5px; border: 1px solid black; border-spacing: 0", "table");
    ASSERT_NE(table, nullptr); DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* first = block("A", "width: 70px; min-width: 90px; max-width: 80px; box-sizing: border-box; padding: 2px; border: 1px solid black", "td", row);
    ASSERT_NE(first, nullptr); ASSERT_NE(block("B", "width: 50px", "td", row), nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, 152.0f);
    state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr); EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, 90.0f);
}

TEST_F(SecondaryViewTest, AutoWidthTableClampsPercentageTracksToTheAvailableWidth) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* first = block("A", "width: 80%", "td", row); ASSERT_NE(first, nullptr);
    DomElement* last = block("B", "width: 80%", "td", row); ASSERT_NE(last, nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, 220.0f);
    state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr); EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, 176.0f);
    state = view_tree_node_state(tree, last, false); ASSERT_NE(state, nullptr); EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, 44.0f);
}

TEST_F(SecondaryViewTest, AutoTableIntrinsicWidthsRespectNoWrapAndRejectAnImpossibleGrid) {
    stylesheet("@page { size: 80px 160px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* first = block("AA AA AA AA AA AA", nullptr, "td", row); ASSERT_NE(first, nullptr);
    ASSERT_NE(block("B", nullptr, "td", row), nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
    EXPECT_NEAR(state->first_occurrence->rect.width, 60.0f, 0.01f);
    ASSERT_TRUE(first->set_attribute("style", "white-space: nowrap"));
    tree = secondary(); EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "table width exceeds its page region");
}

TEST_F(SecondaryViewTest, FixedTableWithAutoWidthUsesIntrinsicSizingAndForcedLineBreaks) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "table-layout: fixed; border-spacing: 0; box-sizing: border-box; padding: 2px; border: 1px solid black", "table");
    ASSERT_NE(table, nullptr); DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* first = block("WW\nI", "white-space: pre", "td", row); ASSERT_NE(first, nullptr);
    DomElement* last = block("B", nullptr, "td", row); ASSERT_NE(last, nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewCssStyle* style = view_css_resolve(tree, first); ASSERT_NE(style, nullptr);
    float first_width = font_measure_text(style->font.font_handle, "WW", 2).width;
    float last_width = font_measure_text(style->font.font_handle, "B", 1).width;
    ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
    EXPECT_NEAR(state->first_occurrence->rect.width, first_width + last_width + 6.0f, 0.01f);
    state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr);
    EXPECT_NEAR(state->first_occurrence->rect.width, first_width, 0.01f);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.height, 24.0f);
}

TEST_F(SecondaryViewTest, AutoTableMeasuresNestedFlowAndNaturalInlineImages) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* first = block(nullptr, nullptr, "td", row); ASSERT_NE(first, nullptr);
    DomElement* img = image(nullptr, first); ASSERT_NE(img, nullptr);
    DomElement* last = block(nullptr, nullptr, "td", row); ASSERT_NE(last, nullptr);
    ASSERT_NE(block("B", "width: 60px; margin: 0 5px; padding: 2px; border: 1px solid black", "div", last), nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    const struct { const char* css; float width; } cases[] = {
        {"", 20.0f}, {"width: 40px; height: 12px", 40.0f}, {"height: 20px", 40.0f}};
    for (const auto& test : cases) {
        SCOPED_TRACE(test.css); ASSERT_TRUE(img->set_attribute("style", test.css));
        ViewTree* tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
        EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, test.width + 76.0f);
        state = view_tree_node_state(tree, first, false); ASSERT_NE(state, nullptr); EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, test.width);
        state = view_tree_node_state(tree, last, false); ASSERT_NE(state, nullptr); EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, 76.0f);
    }
}

TEST_F(SecondaryViewTest, AutoTableDiagnosesUnimplementedCyclicIntrinsicPercentages) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr);
    DomElement* child = block("B", "width: 50%", "div", cell); ASSERT_NE(child, nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* tree = secondary(); EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, child); EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(cell->DomNode::remove_child(child));
    DomElement* inline_image = image("width: 50%", cell); ASSERT_NE(inline_image, nullptr);
    tree = secondary(); EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, inline_image); EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, TableSpacingContributesToIntrinsicWidthsAndFixedPercentageTracks) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: top; padding: 2px; border: 1px solid black }");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* first = nullptr; DomElement* last = nullptr;
    for (size_t i = 0; i < 2; i++) {
        DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
        DomElement* a = block("A", nullptr, "td", row); ASSERT_NE(a, nullptr);
        DomElement* b = block("B", "width: 50px; box-sizing: border-box", "td", row); ASSERT_NE(b, nullptr);
        if (!i) { first = a; last = b; }
    }
    for (bool fixed : {true, false}) {
        SCOPED_TRACE(fixed);
        ASSERT_TRUE(table->set_attribute("style", fixed ?
            "table-layout: fixed; width: 200px; box-sizing: border-box; padding: 2px; border: 1px solid black; border-spacing: 5px 3px" :
            "table-layout: auto; margin: 0 auto; box-sizing: border-box; padding: 2px; border: 1px solid black; border-spacing: 5px 3px"));
        ASSERT_TRUE(first->set_attribute("style", fixed ? "width: 25%; box-sizing: border-box" : "width: 70px; box-sizing: border-box"));
        ASSERT_TRUE(last->set_attribute("style", fixed ? "box-sizing: border-box" : "width: 50px; box-sizing: border-box"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 1u);
        LayoutViewNode* grid = view_tree_node_state(tree, table, false)->first_occurrence;
        LayoutViewNode* a = view_tree_node_state(tree, first, false)->first_occurrence;
        LayoutViewNode* b = view_tree_node_state(tree, last, false)->first_occurrence;
        EXPECT_FLOAT_EQ(grid->rect.width, fixed ? 200.0f : 141.0f);
        EXPECT_FLOAT_EQ(grid->rect.x, fixed ? 10.0f : 49.5f);
        EXPECT_FLOAT_EQ(grid->rect.height, 51.0f);
        EXPECT_FLOAT_EQ(a->rect.x, grid->rect.x + 8.0f); EXPECT_FLOAT_EQ(a->rect.y, 16.0f);
        EXPECT_FLOAT_EQ(a->rect.width, fixed ? 44.75f : 70.0f);
        EXPECT_FLOAT_EQ(b->rect.x, a->rect.x + a->rect.width + 5.0f);
        EXPECT_FLOAT_EQ(b->rect.x + b->rect.width, grid->rect.x + grid->rect.width - 8.0f);
        EXPECT_FLOAT_EQ(a->parent->rect.width, grid->rect.width - 16.0f);
        EXPECT_FLOAT_EQ(a->rect.height, 18.0f);
    }
}

TEST_F(SecondaryViewTest, FixedColumnSpansDivideFirstVisualRowWidthsAndIncludeInternalSpacing) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: top }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 4px 2px", "table");
    ASSERT_NE(table, nullptr);
    DomElement* header = block(nullptr, nullptr, "thead", table); ASSERT_NE(header, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", header); ASSERT_NE(row, nullptr);
    DomElement* spanning = block("A", nullptr, "td", row); ASSERT_NE(spanning, nullptr);
    ASSERT_TRUE(spanning->set_attribute("colspan", "2"));
    ASSERT_NE(block("B", nullptr, "td", row), nullptr);
    DomElement* body = block(nullptr, nullptr, "tbody", table); ASSERT_NE(body, nullptr);
    row = block(nullptr, nullptr, "tr", body); ASSERT_NE(row, nullptr);
    DomElement* cells[3] = {};
    for (DomElement*& cell : cells) { cell = block("X", "width: 300px", "td", row); ASSERT_NE(cell, nullptr); }
    for (bool border_box : {false, true}) {
        ASSERT_TRUE(spanning->set_attribute("style", border_box ?
            "width: 50%; box-sizing: border-box; padding: 2px; border: 1px solid black" :
            "width: 50%; padding: 2px; border: 1px solid black"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* span = source_fragment(tree, spanning, VIEW_FRAGMENT_BODY, true); ASSERT_NE(span, nullptr);
        float width = border_box ? 102.0f : 108.0f;
        EXPECT_FLOAT_EQ(span->rect.width, width); EXPECT_FLOAT_EQ(span->rect.x, 14.0f);
        float x = 14.0f;
        for (size_t i = 0; i < 3; i++) {
            LayoutViewNode* cell = source_fragment(tree, cells[i], VIEW_FRAGMENT_BODY, true); ASSERT_NE(cell, nullptr);
            float track = i < 2 ? (width - 4.0f) * 0.5f : 208.0f - width;
            EXPECT_FLOAT_EQ(cell->rect.width, track); EXPECT_FLOAT_EQ(cell->rect.x, x);
            x += track + 4.0f;
        }
        EXPECT_FLOAT_EQ(x, 230.0f);
    }
}

TEST_F(SecondaryViewTest, AutomaticColumnSpansKeepSingleColumnConstraintsAndAccountForOverlappingSpans) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: top }");
    DomElement* table = block(nullptr, "table-layout: auto; margin: 0 auto; border-spacing: 4px 2px", "table");
    ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cells[3] = {block(nullptr, "width: 40px", "td", row), block(nullptr, nullptr, "td", row),
        block(nullptr, "width: 30px", "td", row)};
    for (DomElement* cell : cells) ASSERT_NE(cell, nullptr);
    row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* first = block(nullptr, "width: 120px", "td", row); ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->set_attribute("colspan", "2")); ASSERT_NE(block(nullptr, nullptr, "td", row), nullptr);
    row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    ASSERT_NE(block(nullptr, nullptr, "td", row), nullptr);
    DomElement* second = block(nullptr, "width: 140px", "td", row); ASSERT_NE(second, nullptr);
    ASSERT_TRUE(second->set_attribute("colspan", "2"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* grid = source_fragment(tree, table, VIEW_FRAGMENT_BODY, true); ASSERT_NE(grid, nullptr);
    EXPECT_FLOAT_EQ(grid->rect.width, 192.0f); EXPECT_FLOAT_EQ(grid->rect.x, 24.0f);
    const float widths[] = {40.0f, 106.0f, 30.0f};
    for (size_t i = 0; i < 3; i++) {
        LayoutViewNode* cell = source_fragment(tree, cells[i], VIEW_FRAGMENT_BODY, true); ASSERT_NE(cell, nullptr);
        EXPECT_FLOAT_EQ(cell->rect.width, widths[i]);
    }
    EXPECT_FLOAT_EQ(source_fragment(tree, first, VIEW_FRAGMENT_BODY, true)->rect.width, 150.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, second, VIEW_FRAGMENT_BODY, true)->rect.width, 140.0f);
    EXPECT_EQ(first->parent->as_element()->first_child, first); EXPECT_FLOAT_EQ(first->width, 0.0f);
}

TEST_F(SecondaryViewTest, SpanningCellsUseActualTracksOnAlternatingPagesAndRetainTheirEdition) {
    stylesheet("@page { size: 240px 100px; margin: 10px } @page :left { size: 200px 100px } "
        "p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, "height: 60px", "tr", table); ASSERT_NE(row, nullptr);
    ASSERT_NE(block("A", "width: 25%", "td", row), nullptr);
    ASSERT_NE(block("B", "width: 25%", "td", row), nullptr); ASSERT_NE(block("C", nullptr, "td", row), nullptr);
    DomElement* spanning[2] = {};
    for (DomElement*& cell : spanning) {
        row = block(nullptr, "height: 60px", "tr", table); ASSERT_NE(row, nullptr);
        cell = block("A", nullptr, "td", row); ASSERT_NE(cell, nullptr);
        ASSERT_TRUE(cell->set_attribute("colspan", "2")); ASSERT_NE(block("C", nullptr, "td", row), nullptr);
    }
    ViewTree* retained = nullptr;
    for (const char* layout : {"table-layout: fixed; width: 100%; border-spacing: 4px 2px",
            "table-layout: auto; width: 100%; border-spacing: 4px 2px"}) {
        SCOPED_TRACE(layout); ASSERT_TRUE(table->set_attribute("style", layout));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 3u);
        const float widths[] = {86.0f, 106.0f};
        for (size_t i = 0; i < 2; i++) {
            LayoutViewNode* cell = source_fragment(tree, spanning[i], VIEW_FRAGMENT_BODY, true); ASSERT_NE(cell, nullptr);
            EXPECT_EQ(occurrence_page(cell), i + 2); EXPECT_FLOAT_EQ(cell->rect.width, widths[i]);
            EXPECT_FLOAT_EQ(cell->rect.x, 14.0f);
        }
        if (!retained) retained = tree;
    }
    EXPECT_FLOAT_EQ(source_fragment(retained, spanning[0], VIEW_FRAGMENT_BODY, true)->rect.width, 86.0f);
}

TEST_F(SecondaryViewTest, AutomaticSpansApplyShorterConstraintsBeforeEarlierLongerSpans) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "border-spacing: 4px 2px", "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* longest = block(nullptr, "width: 180px", "td", row); ASSERT_NE(longest, nullptr);
    ASSERT_TRUE(longest->set_attribute("colspan", "3"));
    row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* shorter = block(nullptr, "width: 160px", "td", row); ASSERT_NE(shorter, nullptr);
    ASSERT_TRUE(shorter->set_attribute("colspan", "2")); ASSERT_NE(block(nullptr, nullptr, "td", row), nullptr);
    row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cells[] = {block(nullptr, "width: 20px", "td", row), block(nullptr, "width: 40px", "td", row),
        block(nullptr, "width: 30px", "td", row)};
    for (DomElement* cell : cells) ASSERT_NE(cell, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(source_fragment(tree, table, VIEW_FRAGMENT_BODY, true)->rect.width, 202.0f);
    const float widths[] = {52.0f, 104.0f, 30.0f};
    for (size_t i = 0; i < 3; i++)
        EXPECT_FLOAT_EQ(source_fragment(tree, cells[i], VIEW_FRAGMENT_BODY, true)->rect.width, widths[i]);
    EXPECT_FLOAT_EQ(source_fragment(tree, longest, VIEW_FRAGMENT_BODY, true)->rect.width, 194.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, shorter, VIEW_FRAGMENT_BODY, true)->rect.width, 160.0f);
}

TEST_F(SecondaryViewTest, ColumnSpanNormalizationKeepsOneCellCursorEvenAtTheHtmlLimit) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: top; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cell = block("A\nB\nC\nD\nE\nF\nG\nH", nullptr, "td", row); ASSERT_NE(cell, nullptr);
    for (const char* span : {"0", "-3", "-0", "invalid", "1001", "999999999999999999999999999999"}) {
        SCOPED_TRACE(span); ASSERT_TRUE(cell->set_attribute("colspan", span));
        for (const char* css : {"table-layout: fixed; width: 100%; border-spacing: 0", "table-layout: auto; width: 100%; border-spacing: 0"}) {
            SCOPED_TRACE(css); ASSERT_TRUE(table->set_attribute("style", css));
            ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
            ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
            EXPECT_EQ(tree->model->page_count, 2u);
            for (LayoutViewNode* box = view_tree_node_state(tree, cell, false)->first_occurrence; box; box = box->next_occurrence)
                if (box->paint_box) EXPECT_NEAR(box->rect.width, 220.0f, .002f);
            size_t bytes = 0;
            for (LayoutViewNode* text = view_tree_node_state(tree, cell->first_child, false)->first_occurrence;
                    text; text = text->next_occurrence) { EXPECT_EQ(text->text_start, bytes); bytes += text->text_length; }
            EXPECT_EQ(bytes, 15u);
        }
    }
}

TEST_F(SecondaryViewTest, RowSpansOccupyGridSlotsAlongsideColumnSpansInBothAlgorithms) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* span = nullptr; DomElement* lower = nullptr;
    DomElement* table = rowspan_table(&span, &lower); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(span->set_attribute("colspan", "2"));
    DomElement* upper = span->next_sibling->as_element();
    ASSERT_NE(block("Right", nullptr, "td", upper->parent->as_element()), nullptr);
    ASSERT_NE(block("Right", nullptr, "td", lower->parent->as_element()), nullptr);
    for (const char* css : {"table-layout: fixed; width: 100%; border-spacing: 4px 2px",
            "table-layout: auto; width: 100%; border-spacing: 4px 2px"}) {
        SCOPED_TRACE(css); ASSERT_TRUE(table->set_attribute("style", css));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* a = source_fragment(tree, span, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* b = source_fragment(tree, upper, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* c = source_fragment(tree, lower, VIEW_FRAGMENT_BODY, true);
        ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(c, nullptr);
        EXPECT_FLOAT_EQ(a->rect.height, 26.0f); EXPECT_FLOAT_EQ(b->rect.height, 12.0f);
        EXPECT_FLOAT_EQ(c->rect.x, b->rect.x); EXPECT_FLOAT_EQ(c->rect.width, b->rect.width);
        EXPECT_FLOAT_EQ(c->rect.y, b->rect.y + 14.0f);
        EXPECT_FLOAT_EQ(b->rect.x, a->rect.x + a->rect.width + 4.0f);
        size_t boxes = 0;
        for (LayoutViewNode* occurrence = a; occurrence; occurrence = occurrence->next_occurrence)
            if (occurrence->paint_box) boxes++;
        EXPECT_EQ(boxes, 1u); EXPECT_EQ(tree->model->page_count, 1u);
        EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
    }
}

TEST_F(SecondaryViewTest, RowSpansNormalizeToTheirActualGroupAndPreserveCoveredEmptyRows) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* span = nullptr; DomElement* lower = nullptr; DomElement* table = rowspan_table(&span, &lower);
    ASSERT_NE(table, nullptr);
    DomElement* second = lower->parent->as_element(); ASSERT_TRUE(second->remove_child(lower));
    ASSERT_TRUE(span->set_attribute("colspan", "2"));
    ASSERT_TRUE(span->parent->remove_child(span->next_sibling));
    DomElement* other = block(nullptr, nullptr, "tbody", table); ASSERT_NE(other, nullptr);
    ASSERT_TRUE(table_rows(other, 1, "Next", "Group"));
    for (const char* value : {"0", "2", "  +2tail", "65535", "999999999999999999999999999999"}) {
        SCOPED_TRACE(value); ASSERT_TRUE(span->set_attribute("rowspan", value));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* a = source_fragment(tree, span, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* empty = source_fragment(tree, second, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* next = source_fragment(tree, other->first_child, VIEW_FRAGMENT_BODY, true);
        ASSERT_NE(a, nullptr); ASSERT_NE(empty, nullptr); ASSERT_NE(next, nullptr);
        EXPECT_FLOAT_EQ(a->rect.height, 12.0f); EXPECT_FLOAT_EQ(empty->rect.height, 12.0f);
        EXPECT_FLOAT_EQ(next->rect.y, a->rect.y + a->rect.height);
    }
    for (const char* value : {"", "invalid", "-3", "-0"}) {
        ASSERT_TRUE(span->set_attribute("rowspan", value)); EXPECT_EQ(layout_table_cell_rowspan(span), 1u);
    }
    ASSERT_TRUE(span->set_attribute("rowspan", "65535")); EXPECT_EQ(layout_table_cell_rowspan(span), 65534u);
}

TEST_F(SecondaryViewTest, RowSpanHeightDistributionIncludesSpacingAndAlignsContent) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* span = nullptr; DomElement* lower = nullptr; DomElement* table = rowspan_table(&span, &lower); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(span->parent->as_element()->set_attribute("style", "height: 12px"));
    ASSERT_TRUE(lower->parent->as_element()->set_attribute("style", "height: 24px"));
    const struct { const char* css; float offset; } cases[] = {{"height: 60px; vertical-align: top", 0.0f},
        {"height: 60px; vertical-align: middle", 24.0f}, {"height: 60px; vertical-align: bottom", 48.0f}};
    for (const char* css : {"width: 100%; table-layout: fixed; border-spacing: 6px 4px",
            "width: 100%; table-layout: auto; border-spacing: 6px 4px"}) for (const auto& test : cases) {
        SCOPED_TRACE(css); ASSERT_TRUE(table->set_attribute("style", css));
        SCOPED_TRACE(test.css); ASSERT_TRUE(span->set_attribute("style", test.css));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* a = source_fragment(tree, span, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* b = source_fragment(tree, span->parent, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* c = source_fragment(tree, lower->parent, VIEW_FRAGMENT_BODY, true);
        LayoutViewNode* text = view_tree_node_state(tree, span->first_child, false)->first_occurrence;
        ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(c, nullptr); ASSERT_NE(text, nullptr);
        EXPECT_NEAR(b->rect.height, 56.0f / 3.0f, .001f); EXPECT_NEAR(c->rect.height, 112.0f / 3.0f, .001f);
        EXPECT_FLOAT_EQ(a->rect.height, 60.0f); EXPECT_NEAR(c->rect.y + c->rect.height, a->rect.y + 60.0f, .001f);
        EXPECT_NEAR(text->parent->rect.y - a->rect.y, test.offset, .001f);
    }
}

TEST_F(SecondaryViewTest, ConnectedRowSpansMoveTogetherAndPreserveExteriorForcedBreaks) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: top } tr { height: 24px }");
    ASSERT_NE(block("Prefix", "height: 30px"), nullptr);
    DomElement* span = nullptr; DomElement* lower = nullptr; DomElement* table = rowspan_table(&span, &lower); ASSERT_NE(table, nullptr);
    // the second span extends the connected cluster from two rows to three.
    ASSERT_TRUE(lower->set_attribute("rowspan", "2"));
    ASSERT_NE(block("Third", nullptr, "td", block(nullptr, "break-after: page", "tr", lower->parent->parent->as_element())), nullptr);
    DomElement* after = block("After"); ASSERT_NE(after, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 3u);
    for (DomNode* row = span->parent; row; row = row->next_sibling) {
        LayoutViewNode* box = source_fragment(tree, row, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
        EXPECT_EQ(occurrence_page(box), 2u); EXPECT_FLOAT_EQ(box->rect.height, 24.0f);
    }
    EXPECT_EQ(occurrence_page(source_fragment(tree, after, VIEW_FRAGMENT_BODY, true)), 3u);
    EXPECT_FLOAT_EQ(source_fragment(tree, span, VIEW_FRAGMENT_BODY, true)->rect.height, 48.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, lower, VIEW_FRAGMENT_BODY, true)->rect.height, 48.0f);
}

TEST_F(SecondaryViewTest, OversizedAndInternallyForcedRowSpansRejectWithoutPublishing) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* span = nullptr; DomElement* lower = nullptr; DomElement* table = rowspan_table(&span, &lower); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(span->set_attribute("style", "height: 100px"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_STREQ(diagnostic.reason, "row spans exceeding a page require spanning-cell continuations");
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
    ASSERT_TRUE(span->remove_attribute("style")); ASSERT_TRUE(lower->parent->as_element()->set_attribute("style", "break-before: page"));
    tree = secondary(); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "forced breaks within row spans require spanning-cell continuations");
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, OverlappingRowSpansAndSpanCoverageBudgetsRejectAtTheirSource) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* span = nullptr; DomElement* lower = nullptr; DomElement* table = rowspan_table(&span, &lower); ASSERT_NE(table, nullptr);
    // a second-row colspan starting in the free slot overlaps a span in the next slot.
    DomElement* first = span->parent->as_element(); ASSERT_TRUE(first->remove_child(span));
    ASSERT_TRUE(first->append_child(span)); ASSERT_TRUE(lower->set_attribute("colspan", "2"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "overlapping table spans require a valid rectangular grid");
    EXPECT_EQ(diagnostic.source.address, lower); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(lower->remove_attribute("colspan"));
    ASSERT_TRUE(span->set_attribute("rowspan", "0"));
    for (size_t row = 0; row < 3; row++) ASSERT_NE(block("Next", nullptr, "td", block(nullptr, nullptr, "tr", first->parent->as_element())), nullptr);
    options.max_items = 8; tree = secondary(); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "table span coverage exceeds its grid budget");
    EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(first->remove_child(first->first_child));
    for (DomNode* row = first->next_sibling; row; row = row->next_sibling) {
        DomElement* element = row->as_element();
        while (element->first_child) ASSERT_TRUE(element->remove_child(element->first_child));
    }
    for (size_t row = 0; row < 4; row++) ASSERT_NE(block(nullptr, nullptr, "tr", first->parent->as_element()), nullptr);
    tree = secondary(); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "table grid exceeds its row budget");
    EXPECT_EQ(tree->model->page_count, 0u);

}

TEST_F(SecondaryViewTest, SpanningHeaderAndFooterCellsRepeatAndRemeasureOnAlternatingPages) {
    stylesheet("@page { size: 240px 120px; margin: 10px } @page :left { size: 200px 120px } "
        "p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(8, &header, &footer); ASSERT_NE(table, nullptr);
    for (DomElement* group : {header, footer}) {
        ASSERT_TRUE(group->first_child->as_element()->first_child->as_element()->set_attribute("rowspan", "2"));
        ASSERT_TRUE(group->first_child->as_element()->first_child->as_element()->set_attribute("style", "width: 50%"));
        ASSERT_NE(block("Second", nullptr, "td", block(nullptr, nullptr, "tr", group)), nullptr);
    }
    for (const char* css : {"width: 100%; table-layout: fixed; border-spacing: 0",
            "width: 100%; table-layout: auto; border-spacing: 0"}) {
        SCOPED_TRACE(css); ASSERT_TRUE(table->set_attribute("style", css));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 2u);
        for (DomElement* group : {header, footer}) {
            DomElement* cell = group->first_child->as_element()->first_child->as_element();
            ViewNodeState* state = view_tree_node_state(tree, cell, false); ASSERT_NE(state, nullptr);
            size_t page = 0;
            for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) if (box->paint_box) {
                EXPECT_EQ(occurrence_page(box), ++page); EXPECT_FLOAT_EQ(box->rect.height, 24.0f);
                EXPECT_FLOAT_EQ(box->rect.width, page == 1 ? 110.0f : 90.0f);
                EXPECT_EQ(box->role, page == 1 ? VIEW_FRAGMENT_BODY : VIEW_FRAGMENT_REPEATED_TABLE);
                LayoutViewNode* lower = source_fragment(tree, group->last_child->as_element()->first_child,
                    page == 1 ? VIEW_FRAGMENT_BODY : VIEW_FRAGMENT_REPEATED_TABLE, true);
                ASSERT_NE(lower, nullptr); EXPECT_FLOAT_EQ(lower->rect.x, box->rect.x + box->rect.width);
            }
            EXPECT_EQ(page, 2u);
        }
    }
}

TEST_F(SecondaryViewTest, SpanningCellsPreserveTheFirstRowBaseline) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: baseline }");
    DomElement* span = nullptr; DomElement* lower = nullptr; DomElement* table = rowspan_table(&span, &lower); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(span->set_attribute("style", "font: 20px/24px Arial; padding-top: 5px"));
    ASSERT_TRUE(lower->set_attribute("style", "font: 10px/12px Arial"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* a = view_tree_node_state(tree, span->first_child, false)->first_occurrence;
    LayoutViewNode* b = view_tree_node_state(tree, span->next_sibling->as_element()->first_child, false)->first_occurrence;
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(a->glyph_run->baseline_y, b->glyph_run->baseline_y);
    LayoutViewNode* first_row = source_fragment(tree, span->parent, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* last_row = source_fragment(tree, lower->parent, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(first_row, nullptr); ASSERT_NE(last_row, nullptr);
    EXPECT_GE(first_row->rect.height, a->glyph_run->baseline_y - first_row->rect.y);
    EXPECT_FLOAT_EQ(source_fragment(tree, span, VIEW_FRAGMENT_BODY, true)->rect.height,
        first_row->rect.height + last_row->rect.height);
}

TEST_F(SecondaryViewTest, RetainedRowSpanPreviewsSurviveRollbackResetAndRecomposition) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top; background: cyan }");
    init_vector_engine();
    ASSERT_NE(block("Prefix", "height: 70px"), nullptr);
    DomElement* span = nullptr; DomElement* lower = nullptr; DomElement* table = rowspan_table(&span, &lower); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(span->set_attribute("style", "background: transparent"));
    ASSERT_TRUE(span->parent->as_element()->set_attribute("style", "background: blue"));
    ASSERT_TRUE(lower->parent->as_element()->set_attribute("style", "background: red"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* box = source_fragment(tree, span, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    EXPECT_EQ(occurrence_page(box), 2u);
    for (DomElement* cell : {span, lower}) {
        ViewNodeState* state = view_tree_node_state(tree, cell->first_child, false); ASSERT_NE(state, nullptr);
        size_t bytes = 0;
        for (LayoutViewNode* text = state->first_occurrence; text; text = text->next_occurrence) {
            EXPECT_EQ(text->text_start, bytes); bytes += text->text_length;
            EXPECT_EQ(occurrence_page(text), 2u);
        }
        EXPECT_EQ(bytes, cell->first_child->as_text()->length);
    }
    ImageSurface* physical = render_secondary_page_snapshot(tree, 2, 1.0f); ASSERT_NE(physical, nullptr);
    Color origin = {snapshot_pixel(physical, (size_t)(box->rect.x + 50.0f), (size_t)(box->rect.y + 18.0f))};
    EXPECT_EQ(origin.r, 0); EXPECT_EQ(origin.g, 0); EXPECT_EQ(origin.b, 255);
    image_surface_destroy(physical);
    ViewPreviewOptions preview = view_preview_options_default(); preview.columns = 1;
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ImageSurface* before = render_secondary_view_snapshot(retained); ASSERT_NE(before, nullptr);
    tree->reset_retained(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* after = render_secondary_view_snapshot(retained); ASSERT_NE(after, nullptr);
    ASSERT_EQ(before->width, after->width); ASSERT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, UnequalTableRowsGainAnonymousCellsWithoutChangingDom) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: top; padding: 3px; background: red } tr { color: blue; text-align: right; white-space: pre }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    DomElement* header = table->first_child->as_element()->first_child->as_element();
    ASSERT_TRUE(header->first_child->as_element()->set_attribute("colspan", "2"));
    DomElement* body = table->last_child->as_element()->first_child->as_element();
    DomNode* last = body->last_child;
    for (const char* algorithm : {"table-layout: fixed; width: 100%; border-spacing: 4px 2px",
            "table-layout: auto; width: 100%; border-spacing: 4px 2px"}) {
        ASSERT_TRUE(table->set_attribute("style", algorithm));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* row = source_fragment(tree, body, VIEW_FRAGMENT_BODY, true); ASSERT_NE(row, nullptr);
        LayoutViewNode* anonymous = row->last_child; ASSERT_NE(anonymous, nullptr);
        EXPECT_EQ(anonymous->source.address, nullptr); EXPECT_EQ(anonymous->state, nullptr);
        EXPECT_EQ(anonymous->computed_style->source, nullptr);
        EXPECT_EQ(anonymous->computed_style->display.inner, CSS_VALUE_TABLE_CELL);
        EXPECT_EQ(anonymous->computed_style->parent->source, body);
        EXPECT_FLOAT_EQ(anonymous->rect.height, row->rect.height);
        EXPECT_FLOAT_EQ(anonymous->rect.x, anonymous->parent->rect.x + anonymous->parent->rect.width - anonymous->rect.width);
        EXPECT_EQ(anonymous->computed_style->background.a, 0);
        EXPECT_EQ(anonymous->computed_style->padding[0], nullptr);
        EXPECT_EQ(anonymous->computed_style->text_align, CSS_VALUE_RIGHT);
        EXPECT_EQ(anonymous->computed_style->white_space, CSS_VALUE_PRE);
        EXPECT_EQ(anonymous->computed_style->color.b, 255);
        EXPECT_EQ(view_css_property(tree, anonymous->computed_style, "background"), nullptr);
        EXPECT_EQ(body->last_child, last); EXPECT_EQ(last->next_sibling, nullptr);
        EXPECT_EQ(view_tree_node_state(tree, body, false)->occurrence_count, 1u);
        EXPECT_FLOAT_EQ(source->width, 640.0f);
    }
}

TEST_F(SecondaryViewTest, MissingTableCellsFillHolesAroundSpansAndDeclaredColumns) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 0; vertical-align: top } col { width: 40px }");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* column = block(nullptr, nullptr, "col", table); ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->set_attribute("span", "4"));
    DomElement* group = block(nullptr, nullptr, "tbody", table); ASSERT_NE(group, nullptr);
    DomElement* rows[3] = {}; DomElement* span = nullptr; DomElement* lower = nullptr;
    for (size_t i = 0; i < 3; i++) {
        rows[i] = block(nullptr, "height: 20px", "tr", group); ASSERT_NE(rows[i], nullptr);
        if (!i) {
            ASSERT_NE(block("A", nullptr, "td", rows[i]), nullptr);
            span = block("Span", nullptr, "td", rows[i]); ASSERT_NE(span, nullptr);
            ASSERT_TRUE(span->set_attribute("rowspan", "3"));
            ASSERT_NE(block("B", nullptr, "td", rows[i]), nullptr);
        } else if (i == 1) {
            lower = block("C", nullptr, "td", rows[i]); ASSERT_NE(lower, nullptr);
            ASSERT_TRUE(lower->set_attribute("rowspan", "2"));
        }
    }
    for (const char* algorithm : {"table-layout: fixed; width: 100%; border-spacing: 4px 2px",
            "table-layout: auto; width: 100%; border-spacing: 4px 2px"}) {
        SCOPED_TRACE(algorithm); ASSERT_TRUE(table->set_attribute("style", algorithm));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        const size_t missing[] = {1, 2, 2};
        for (size_t r = 0; r < 3; r++) {
            LayoutViewNode* row = source_fragment(tree, rows[r], VIEW_FRAGMENT_BODY, true); ASSERT_NE(row, nullptr);
            size_t count = 0;
            for (LayoutViewNode* cell = row->first_child; cell; cell = cell->next_sibling) if (!cell->source.address) {
                ASSERT_NE(cell->table_range, nullptr); EXPECT_EQ(cell->table_range->span, 1u);
                EXPECT_TRUE(cell->table_range->missing);
                size_t c = cell->table_range->column;
                EXPECT_GE(c, r ? 2u : 3u); EXPECT_LT(c, 4u);
                EXPECT_FLOAT_EQ(cell->rect.x, 14.0f + c * 54.0f);
                EXPECT_FLOAT_EQ(cell->rect.width, 50.0f); EXPECT_FLOAT_EQ(cell->rect.height, 20.0f); count++;
            }
            EXPECT_EQ(count, missing[r]);
        }
        EXPECT_FLOAT_EQ(source_fragment(tree, span, VIEW_FRAGMENT_BODY, true)->rect.height, 64.0f);
        EXPECT_FLOAT_EQ(source_fragment(tree, lower, VIEW_FRAGMENT_BODY, true)->rect.height, 42.0f);
        EXPECT_EQ(rows[2]->first_child, nullptr);
    }
}

TEST_F(SecondaryViewTest, AnonymousCellsRepeatAndRemeasureWithTheirRows) {
    stylesheet("@page { size: 240px 100px; margin: 10px } @page :left { size: 200px 100px } "
        "p { margin: 0; font: 10px/12px Arial } td, th { padding: 0; vertical-align: top } col { width: 30px }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(8, &header, &footer); ASSERT_NE(table, nullptr);
    for (DomNode* group = table->first_child; group; group = group->next_sibling)
        for (DomNode* row = group->as_element()->first_child; row; row = row->next_sibling)
            for (DomNode* cell = row->as_element()->first_child; cell; cell = cell->next_sibling)
                ASSERT_NE(table_cell_text(cell->as_element(), "A"), nullptr);
    DomElement* column = block(nullptr, nullptr, "col", table); ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->set_attribute("span", "3"));
    for (const char* algorithm : {"table-layout: fixed; width: 100%; border-spacing: 4px 2px",
            "table-layout: auto; width: 100%; border-spacing: 4px 2px"}) {
        ASSERT_TRUE(table->set_attribute("style", algorithm));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_GE(tree->model->page_count, 2u);
        for (DomElement* group : {header, footer}) {
            size_t count = 0;
            for (LayoutViewNode* row = view_tree_node_state(tree, group->first_child, false)->first_occurrence; row; row = row->next_occurrence) {
                LayoutViewNode* cell = row->last_child; ASSERT_NE(cell, nullptr);
                EXPECT_EQ(cell->source.address, nullptr); EXPECT_EQ(cell->role, row->role);
                EXPECT_EQ(cell->role, count ? VIEW_FRAGMENT_REPEATED_TABLE : VIEW_FRAGMENT_BODY);
                EXPECT_FLOAT_EQ(cell->rect.width, (occurrence_page(row) % 2 ? 204.0f : 164.0f) / 3.0f);
                EXPECT_EQ(cell->first_child, nullptr); count++;
            }
            EXPECT_EQ(count, tree->model->page_count);
        }
    }
}

TEST_F(SecondaryViewTest, AnonymousCellsContinueWithLongInlineRows) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 0; vertical-align: top; white-space: pre; orphans: 1; widows: 1 } col { width: 40px }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    DomElement* column = block(nullptr, nullptr, "col", table); ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->set_attribute("span", "3"));
    DomElement* body = table->last_child->prev_sibling->as_element()->first_child->as_element();
    DomText* text = table_cell_text(body->first_child->as_element(), "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ"); ASSERT_NE(text, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_GE(tree->model->page_count, 2u);
    size_t count = 0, bytes = 0;
    for (LayoutViewNode* row = view_tree_node_state(tree, body, false)->first_occurrence; row; row = row->next_occurrence) {
        LayoutViewNode* cell = row->last_child; ASSERT_NE(cell, nullptr); EXPECT_EQ(cell->source.address, nullptr);
        EXPECT_FLOAT_EQ(cell->rect.height, row->rect.height); EXPECT_EQ(cell->first_child, nullptr); count++;
    }
    EXPECT_EQ(count, tree->model->page_count);
    for (LayoutViewNode* piece = view_tree_node_state(tree, text, false)->first_occurrence; piece; piece = piece->next_occurrence) {
        EXPECT_EQ(piece->text_start, bytes); bytes += piece->text_length;
    }
    EXPECT_EQ(bytes, 19u);
}

TEST_F(SecondaryViewTest, CompletedCellCoverageAndNodeBudgetsRejectBeforePublishing) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table"); ASSERT_NE(table, nullptr);
    DomElement* column = block(nullptr, nullptr, "col", table); ASSERT_NE(column, nullptr);
    ASSERT_TRUE(column->set_attribute("span", "3"));
    for (size_t i = 0; i < 2; i++) {
        DomElement* row = block(nullptr, "height: 12px", "tr", table); ASSERT_NE(row, nullptr);
        if (!i) ASSERT_NE(block(nullptr, nullptr, "td", row), nullptr);
    }
    PagedLayoutOptions options = paged_layout_options_default(); options.max_items = 5;
    PagedLayoutDiagnostic diagnostic = {}; ViewTree* tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "completed table coverage exceeds its grid budget");
    EXPECT_EQ(diagnostic.source.address, table); EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
    options = paged_layout_options_default(); options.max_nodes = 8;
    diagnostic = {}; tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "anonymous table cells exceed node budget");
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
    options = paged_layout_options_default(); diagnostic = {}; tree = secondary();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(source_fragment(tree, table, VIEW_FRAGMENT_BODY, true)->rect.height, 24.0f);
}

TEST_F(SecondaryViewTest, RetainedAnonymousCellsPreserveLayerGeometryAfterRecomposition) {
    init_vector_engine();
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 0; background: red; vertical-align: top }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 4px 2px; background: magenta", "table");
    ASSERT_NE(table, nullptr);
    DomElement* columns = block(nullptr, "background: yellow", "colgroup", table); ASSERT_NE(columns, nullptr);
    ASSERT_NE(block(nullptr, "background: cyan", "col", columns), nullptr);
    ASSERT_NE(block(nullptr, nullptr, "col", columns), nullptr);
    DomElement* last_column = block(nullptr, "background: blue", "col", columns); ASSERT_NE(last_column, nullptr);
    DomElement* first = block(nullptr, "height: 20px; background: lime", "tr", table); ASSERT_NE(first, nullptr);
    ASSERT_NE(block("A", nullptr, "td", first), nullptr);
    DomElement* empty = block(nullptr, "height: 20px", "tr", table); ASSERT_NE(empty, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* row = source_fragment(tree, empty, VIEW_FRAGMENT_BODY, true); ASSERT_NE(row, nullptr);
    ASSERT_NE(row->first_child, nullptr); EXPECT_TRUE(row->first_child->generated);
    EXPECT_EQ(row->first_child->source.address, nullptr); ASSERT_NE(row->first_child->table_range, nullptr);
    EXPECT_EQ(row->first_child->table_range->column, 0u);
    ViewPreviewOptions preview = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ImageSurface* before = render_secondary_view_snapshot(retained); ASSERT_NE(before, nullptr);
    const struct { size_t x, y; uint32_t rgb; } pixels[] = {
        {40, 20, 0xff0000}, {110, 20, 0xff00ff}, {20, 40, 0xff00ff}, {110, 40, 0xff00ff},
        {200, 40, 0xff00ff}, {84, 40, 0xff00ff}};
    for (const auto& pixel : pixels) {
        Color color = {snapshot_pixel(before, pixel.x, pixel.y)};
        EXPECT_EQ((uint32_t)color.r * 65536 + (uint32_t)color.g * 256 + color.b, pixel.rgb);
    }
    ASSERT_TRUE(first->set_attribute("style", "height: 20px; background: red"));
    ASSERT_TRUE(last_column->set_attribute("style", "background: black"));
    tree->reset_retained(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* after = render_secondary_view_snapshot(retained); ASSERT_NE(after, nullptr);
    ASSERT_EQ(before->width, after->width); ASSERT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
    EXPECT_EQ(empty->first_child, nullptr);
}

TEST_F(SecondaryViewTest, ColumnSpanTrackBudgetsFailBeforePublishing) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    DomElement* cell = table->first_child->as_element()->first_child->as_element()->first_child->as_element();
    ASSERT_TRUE(cell->set_attribute("colspan", "2"));
    ASSERT_NE(table_cell_text(cell, ""), nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); options.max_items = 2;
    PagedLayoutDiagnostic diagnostic = {}; ViewTree* tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "table grid exceeds its track budget");
    EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, PercentageColumnSpansUseEachAlgorithmsTrackBudget) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cell = block("A", "width: 50%", "td", row); ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cell->set_attribute("colspan", "2")); ASSERT_NE(block("B", nullptr, "td", row), nullptr);
    const struct { const char* css; float width; } cases[] = {
        {"table-layout: fixed; width: 100%; border-spacing: 4px 2px", 102.0f},
        {"table-layout: auto; width: 100%; border-spacing: 4px 2px", 106.0f}};
    for (const auto& test : cases) {
        SCOPED_TRACE(test.css); ASSERT_TRUE(table->set_attribute("style", test.css));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* box = source_fragment(tree, cell, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
        EXPECT_FLOAT_EQ(box->rect.width, test.width);
        LayoutViewNode* next = source_fragment(tree, cell->next_sibling, VIEW_FRAGMENT_BODY, true); ASSERT_NE(next, nullptr);
        EXPECT_FLOAT_EQ(next->rect.x, box->rect.x + box->rect.width + 4.0f);
        EXPECT_FLOAT_EQ(next->rect.x + next->rect.width, 226.0f);
    }
}

TEST_F(SecondaryViewTest, TableSpacingUsesSelectedInheritanceHtmlDefaultsAndCalculatedLengths) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    ASSERT_TRUE(source->set_attribute("style", "border-spacing: .5em 1em; font-size: 8px"));
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cell = block("A", "width: 40px; font-size: 10px; line-height: 12px", "td", row); ASSERT_NE(cell, nullptr);
    const struct { const char* css; const char* hint; float horizontal, vertical; } cases[] = {
        {"font-size: 20px", nullptr, 2.0f, 2.0f},
        {"font-size: 20px", "7", 7.0f, 7.0f},
        {"border-spacing: inherit; font-size: 20px", nullptr, 4.0f, 8.0f},
        {"border-spacing: unset; font-size: 20px", nullptr, 4.0f, 8.0f},
        {"border-spacing: initial", "7", 0.0f, 0.0f},
        {"border-spacing: calc(2px + 1.5px)", nullptr, 3.5f, 3.5f},
        {"border-spacing: 1.5px 2.5px", nullptr, 1.5f, 2.5f},
        {"display: table; font-size: 20px", nullptr, 2.0f, 2.0f}};
    for (const auto& test : cases) {
        SCOPED_TRACE(test.css); ASSERT_TRUE(table->set_attribute("style", test.css));
        if (test.hint) ASSERT_TRUE(table->set_attribute("cellspacing", test.hint));
        else table->remove_attribute("cellspacing");
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* grid = view_tree_node_state(tree, table, false)->first_occurrence;
        LayoutViewNode* box = view_tree_node_state(tree, cell, false)->first_occurrence;
        EXPECT_FLOAT_EQ(grid->rect.width, 40.0f + 2.0f * test.horizontal);
        EXPECT_FLOAT_EQ(grid->rect.height, 12.0f + 2.0f * test.vertical);
        EXPECT_FLOAT_EQ(box->rect.x, grid->rect.x + test.horizontal);
        EXPECT_FLOAT_EQ(box->rect.y, grid->rect.y + test.vertical);
    }
}

TEST_F(SecondaryViewTest, SpacedTablesReserveRepeatedGroupsAndBothFragmentEdges) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(9, &header, &footer); ASSERT_NE(table, nullptr);
    for (const char* layout : {"table-layout: fixed; width: 100%; border-spacing: 6px 4px",
            "table-layout: auto; width: 100%; border-spacing: 6px 4px"}) {
        SCOPED_TRACE(layout); ASSERT_TRUE(table->set_attribute("style", layout));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 3u);
        ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
        ASSERT_EQ(state->occurrence_count, 3u);
        for (LayoutViewNode* grid = state->first_occurrence; grid; grid = grid->next_occurrence) {
            EXPECT_FLOAT_EQ(grid->rect.y, 10.0f); EXPECT_FLOAT_EQ(grid->rect.height, 114.0f);
            EXPECT_FLOAT_EQ(grid->first_child->rect.x, 16.0f); EXPECT_FLOAT_EQ(grid->first_child->rect.y, 14.0f);
            EXPECT_FLOAT_EQ(grid->last_child->rect.y, 102.0f);
        }
        EXPECT_EQ(view_tree_node_state(tree, header->first_child, false)->occurrence_count, 3u);
        EXPECT_EQ(view_tree_node_state(tree, footer->first_child, false)->occurrence_count, 3u);
    }
}

TEST_F(SecondaryViewTest, SpacedPercentageTableTracksRemeasureForAlternatingPageWidths) {
    stylesheet("@page { size: 240px 100px; margin: 10px } @page :left { size: 200px 100px } "
        "p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    for (size_t i = 0; i < 3; i++) {
        DomElement* row = block(nullptr, "height: 60px", "tr", table); ASSERT_NE(row, nullptr);
        DomElement* cell = block("A", "width: 25%", "td", row); ASSERT_NE(cell, nullptr);
        ASSERT_NE(block("B", nullptr, "td", row), nullptr);
    }
    for (const char* layout : {"table-layout: fixed; width: 100%; border-spacing: 4px 2px",
            "table-layout: auto; width: 100%; border-spacing: 4px 2px"}) {
        SCOPED_TRACE(layout); ASSERT_TRUE(table->set_attribute("style", layout));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 3u);
        const float widths[] = {52.0f, 42.0f, 52.0f}; size_t page = 0;
        for (DomNode* row = table->first_child; row; row = row->next_sibling, page++) {
            LayoutViewNode* cell = view_tree_node_state(tree, row->as_element()->first_child, false)->first_occurrence;
            EXPECT_FLOAT_EQ(cell->rect.width, widths[page]); EXPECT_FLOAT_EQ(cell->rect.x, 14.0f);
        }
    }
}

TEST_F(SecondaryViewTest, SpacingThatConsumesTheTableWidthFailsBeforePublishing) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->set_attribute("style", "table-layout: fixed; width: 100%; border-spacing: 80px 0"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(diagnostic.source.address, table); EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "table spacing leaves no room for its tracks");
}

TEST_F(SecondaryViewTest, SpacedTablesWithoutRepeatedGroupsKeepForcedBreaksAndTheirFinalGap) {
    stylesheet("@page { size: 120px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = block(nullptr, "width: 100%; box-sizing: border-box; padding: 2px; border: 1px solid black; "
        "box-decoration-break: clone; border-spacing: 5px 7px", "table"); ASSERT_NE(table, nullptr);
    for (size_t i = 0; i < 2; i++) {
        DomElement* row = block(nullptr, i ? "height: 20px" : "height: 20px; break-after: page", "tr", table);
        ASSERT_NE(row, nullptr); ASSERT_NE(block("A", nullptr, "td", row), nullptr);
    }
    DomElement* after = block(nullptr, "height: 5px"); ASSERT_NE(after, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->occurrence_count, 2u);
    for (LayoutViewNode* grid = state->first_occurrence; grid; grid = grid->next_occurrence) {
        EXPECT_FLOAT_EQ(grid->rect.height, 40.0f);
        EXPECT_FLOAT_EQ(grid->first_child->rect.y, 20.0f);
    }
    LayoutViewNode* following = view_tree_node_state(tree, after, false)->first_occurrence;
    EXPECT_EQ(occurrence_page(following), 2u); EXPECT_FLOAT_EQ(following->rect.y, 50.0f);
}

TEST_F(SecondaryViewTest, AnonymousTableSourceTextIsDiagnosedAtItsSource) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0 }");
    DomElement* table = block("context content", "display: table"); ASSERT_NE(table, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, table->first_child);
    EXPECT_STREQ(diagnostic.reason, "anonymous table boxes require a table fixup producer");
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, OversizedTableCellsContinueIndependentlyWithoutRepeatingCompletedText) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(1, &header, &footer, "2"); ASSERT_NE(table, nullptr);
    DomElement* row = footer->next_sibling->as_element()->first_child->as_element();
    DomElement* tall = row->first_child->as_element();
    DomText* text = table_cell_text(tall, "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ"); ASSERT_NE(text, nullptr);
    for (const char* layout : {"table-layout: fixed; width: 100%; border-spacing: 0", "table-layout: auto; width: 100%; border-spacing: 0",
            "table-layout: fixed; width: 100%; border-spacing: 6px 4px", "table-layout: auto; width: 100%; border-spacing: 6px 4px"}) {
        SCOPED_TRACE(layout); ASSERT_TRUE(table->set_attribute("style", layout));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 2u);
        ViewNodeState* state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr);
        size_t bytes = 0;
        for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
            EXPECT_EQ(fragment->text_start, bytes); bytes += fragment->text_length;
        }
        EXPECT_EQ(bytes, 19u);
        DomText* short_text = row->first_child->next_sibling->as_element()->first_child->as_text();
        ViewNodeState* shorter = view_tree_node_state(tree, short_text, false); ASSERT_NE(shorter, nullptr);
        bytes = 0;
        for (LayoutViewNode* fragment = shorter->first_occurrence; fragment; fragment = fragment->next_occurrence) {
            EXPECT_EQ(occurrence_page(fragment), 1u); bytes += fragment->text_length;
        }
        EXPECT_EQ(bytes, short_text->length);
        ViewNodeState* rows = view_tree_node_state(tree, row, false); ASSERT_NE(rows, nullptr);
        ASSERT_EQ(rows->occurrence_count, 2u);
        EXPECT_TRUE(rows->first_occurrence->first_fragment); EXPECT_FALSE(rows->first_occurrence->last_fragment);
        EXPECT_FALSE(rows->last_occurrence->first_fragment); EXPECT_TRUE(rows->last_occurrence->last_fragment);
        EXPECT_EQ(view_tree_node_state(tree, header->first_child, false)->occurrence_count, 2u);
        EXPECT_EQ(view_tree_node_state(tree, footer->first_child, false)->occurrence_count, 2u);
    }
}

TEST_F(SecondaryViewTest, TableCellWidowsChooseAnEarlierLegalBoundaryBeforeRelaxation) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top; white-space: pre-wrap; orphans: 2; widows: 2 }");
    DomElement* footer = nullptr; DomElement* table = fixed_table(1, nullptr, &footer, "2"); ASSERT_NE(table, nullptr);
    DomElement* cell = footer->next_sibling->as_element()->first_child->as_element()->first_child->as_element();
    DomText* text = table_cell_text(cell, "A\nB\nC\nD\nE\nF\nG"); ASSERT_NE(text, nullptr);
    for (const char* layout : {"table-layout: fixed; width: 100%; border-spacing: 0", "table-layout: auto; width: 100%; border-spacing: 0"}) {
        SCOPED_TRACE(layout); ASSERT_TRUE(table->set_attribute("style", layout));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
        ViewNodeState* state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr);
        size_t bytes[2] = {};
        for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
            bytes[occurrence_page(fragment) - 1] += fragment->text_length;
        EXPECT_EQ(bytes[0], 10u); EXPECT_EQ(bytes[1], 3u);
    }
}

TEST_F(SecondaryViewTest, IndependentDecorationConditionalityReservesAndPaintsBodyAndCellContinuations) {
    stylesheet("@page{size:140px 80px;margin:10px}@page :left{size:120px 80px}"
        "table{table-layout:fixed;width:100%;border-spacing:0}td{padding:0;vertical-align:top}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    const char* names[] = {"r:border-before-width.conditionality", "r:border-after-width.conditionality",
        "r:padding-before.conditionality", "r:padding-after.conditionality"};
    for (size_t context = 0; context < 3; context++) {
        SCOPED_TRACE(context);
        DomElement* container = block(nullptr); ASSERT_NE(container, nullptr);
        DomElement* owner = nullptr;
        if (!context) owner = block(nullptr, nullptr, "div", container);
        else {
            DomElement* table = block(nullptr, nullptr, "table", container); ASSERT_NE(table, nullptr);
            DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
            DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr);
            owner = context == 1 ? cell : block(nullptr, nullptr, "div", cell);
        }
        ASSERT_NE(owner, nullptr);
        DomElement* content = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", nullptr, "p", owner); ASSERT_NE(content, nullptr);
        for (size_t mask = 0; mask < 16; mask++) {
            SCOPED_TRACE(mask);
            ASSERT_TRUE(owner->set_attribute("style", mask % 2 ? "border:1px solid red;padding:2px" :
                "border:1px solid red;padding:2px;box-decoration-break:clone"));
            for (size_t i = 0; i < 4; i++) ASSERT_TRUE(owner->set_attribute(names[i], mask & (1u << i) ? "retain" : "discard"));
            ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
            ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
            ASSERT_EQ(tree->model->page_count, 3u);
            ViewNodeState* state = view_tree_node_state(tree, owner, false); ASSERT_NE(state, nullptr);
            size_t boxes = 0;
            for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
                uint32_t page = occurrence_page(part); boxes++;
                EXPECT_EQ(part->first_fragment, page == 1); EXPECT_EQ(part->last_fragment, page == 3);
                ASSERT_NE(part->computed_boundary, nullptr); ASSERT_NE(part->computed_boundary->border, nullptr);
                const BoundaryProp* boundary = part->computed_boundary;
                float top = 0.0f, bottom = 0.0f;
                for (size_t side = 0; side < 2; side++) {
                    bool terminal = side ? part->last_fragment : part->first_fragment;
                    float border = terminal || (mask & (1u << side)) ? 1.0f : 0.0f;
                    float padding = terminal || (mask & (1u << (side + 2))) ? 2.0f : 0.0f;
                    EXPECT_FLOAT_EQ(boundary->border->width.values[side * 2], border);
                    EXPECT_FLOAT_EQ(boundary->padding.values[side * 2], padding);
                    (side ? bottom : top) = border + padding;
                }
                ASSERT_NE(part->first_child, nullptr);
                EXPECT_FLOAT_EQ(part->first_child->rect.y, part->rect.y + top);
                EXPECT_FLOAT_EQ(part->first_child->rect.height, part->rect.height - top - bottom);
                EXPECT_LE(part->rect.y + part->rect.height, 70.0f);
                EXPECT_FLOAT_EQ(part->rect.width, page == 2 ? 100.0f : 120.0f);
            }
            EXPECT_EQ(boxes, 3u); EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
            DomText* text = content->first_child->as_text(); ASSERT_NE(text, nullptr);
            state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr); size_t bytes = 0;
            for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) {
                EXPECT_EQ(part->text_start, bytes); bytes += part->text_length;
            }
            EXPECT_EQ(bytes, text->length);
            ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
        }
        ASSERT_TRUE(container->set_attribute("style", "display:none"));
    }
}

TEST_F(SecondaryViewTest, IndependentDecorationPaintSurvivesFailedRecompositionAndSourceRelease) {
    init_vector_engine();
    stylesheet("@page{size:140px 80px;margin:10px}p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* owner = block(nullptr, "border:1px solid red;padding:2px"); ASSERT_NE(owner, nullptr);
    ASSERT_TRUE(owner->set_attribute("r:border-before-width.conditionality", "retain"));
    ASSERT_TRUE(owner->set_attribute("r:border-after-width.conditionality", "discard"));
    ASSERT_TRUE(owner->set_attribute("r:padding-before.conditionality", "discard"));
    ASSERT_TRUE(owner->set_attribute("r:padding-after.conditionality", "retain"));
    ASSERT_NE(block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", nullptr, "p", owner), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 3u);
    ImageSurface* before = render_secondary_page_snapshot(tree, 2, 1.0f); ASSERT_NE(before, nullptr);
    EXPECT_EQ(snapshot_pixel(before, 50, 10), 0xff0000ffu);
    ViewPreviewOptions preview = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* after = render_secondary_page_snapshot(retained, 2, 1.0f); ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->width, before->width); ASSERT_EQ(after->height, before->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
}

TEST_F(SecondaryViewTest, NativeDecorationPoliciesComputeExplicitInheritanceAndRejectInvalidValues) {
    stylesheet("@page{size:140px 80px;margin:10px}div{margin:0;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, "box-decoration-break:clone"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:padding-before.conditionality", "retain"));
    ASSERT_TRUE(parent->set_attribute("r:border-after-width.conditionality", "discard"));
    DomElement* child = block("Text", "box-decoration-break:slice", "div", parent); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:padding-before.conditionality", "inherit"));
    ASSERT_TRUE(child->set_attribute("r:border-after-width.conditionality", "inherit"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_TRUE(radiant_decoration_retain(style, true, false)); EXPECT_FALSE(radiant_decoration_retain(style, false, true));
    EXPECT_FALSE(radiant_decoration_retain(style, true, true)); EXPECT_FALSE(radiant_decoration_retain(style, false, false));
    DomElement* ordinary = block("Other", "box-decoration-break:clone", "div", parent); ASSERT_NE(ordinary, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    style = view_css_resolve(tree, ordinary); ASSERT_NE(style, nullptr);
    EXPECT_TRUE(radiant_decoration_retain(style, false, true)); EXPECT_TRUE(radiant_decoration_retain(style, true, false));
    ASSERT_TRUE(child->set_attribute("r:padding-before.conditionality", "sometimes")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "decoration conditionality requires css, discard or retain");
}

TEST_F(SecondaryViewTest, FoCompoundDecorationLengthsAndConditionalityLowerWithOriginalDiagnostics) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body overflow='visible'/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block border='3pt solid red' border-before-width='6pt' border-before-width.length='0.75pt' border-before-width.conditionality='retain' "
        "padding='9pt' padding-before='6pt' padding-before.length='1.5pt' padding-after.length='2.25pt' padding-after.conditionality='retain'>"
        "<f:block border-before-style='solid' border-before-width='inherit' padding-after='inherit'>Child</f:block></f:block>"
        "</f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options);
    ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* parent = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(parent, nullptr);
    DomElement* child = parent->first_child_element(); ASSERT_NE(child, nullptr);
    EXPECT_STREQ(parent->get_attribute("r:border-before-width.conditionality"), "retain");
    EXPECT_STREQ(parent->get_attribute("r:padding-before.conditionality"), "discard");
    EXPECT_STREQ(child->get_attribute("r:border-before-width.conditionality"), "inherit");
    EXPECT_STREQ(child->get_attribute("r:padding-after.conditionality"), "inherit");
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* box = source_fragment(tree, parent, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr); ASSERT_NE(box->computed_boundary, nullptr);
    EXPECT_FLOAT_EQ(box->computed_boundary->border->width.top, 1.0f); EXPECT_FLOAT_EQ(box->computed_boundary->padding.top, 2.0f);
    EXPECT_FLOAT_EQ(box->computed_boundary->padding.bottom, 3.0f);
    ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_TRUE(radiant_decoration_retain(style, false, false)); EXPECT_TRUE(radiant_decoration_retain(style, true, true));
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    ASSERT_TRUE(original->set_attribute("padding-before.length", "-1pt"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_STREQ(translated->diagnostic.property, "padding-before.length");
    ASSERT_TRUE(original->set_attribute("padding-before.length", "1.5pt"));
    ASSERT_TRUE(original->set_attribute("border-top-width", "2pt"));
    for (const char* invalid : {"medium", "1", "initial", "-1pt", "10%"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original->set_attribute("border-before-width.length", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_STREQ(translated->diagnostic.property, "border-before-width.length");
    }
    ASSERT_TRUE(original->set_attribute("border-before-width.length", "0.75pt"));
    ASSERT_TRUE(original->set_attribute("border-before-width.conditionality", "css"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_STREQ(translated->diagnostic.property, "border-before-width.conditionality");
}

TEST_F(SecondaryViewTest, FoSparseDecorationComponentsUseInitialValuesAndAbsolutePropertiesReplaceTheCompound) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body overflow='visible'/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block border='0.75pt solid red' padding='1.5pt' border-before-width.conditionality='retain' padding-after.conditionality='retain'>"
        "<f:block border-before-style='solid' border-before-width='inherit'>Whole</f:block>"
        "<f:block border-before-style='solid' border-before-width.length='inherit'>Length</f:block></f:block>"
        "<f:block border='0.75pt solid red' padding='1.5pt' border-before-width.conditionality='retain' border-top-width='0.75pt' "
        "padding-after.conditionality='retain' padding-bottom='3pt'>Absolute</f:block>"
        "</f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options);
    ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* parent = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(parent, nullptr);
    DomElement* absolute = parent->next_sibling_element(); ASSERT_NE(absolute, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* box = source_fragment(tree, parent, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr); ASSERT_NE(box->computed_boundary, nullptr);
    EXPECT_FLOAT_EQ(box->computed_boundary->border->width.top, 3.0f); EXPECT_FLOAT_EQ(box->computed_boundary->padding.bottom, 0.0f);
    DomElement* whole = parent->first_child_element(); ASSERT_NE(whole, nullptr);
    DomElement* length = whole->next_sibling_element(); ASSERT_NE(length, nullptr);
    EXPECT_TRUE(radiant_decoration_retain(view_css_resolve(tree, whole), false, false));
    EXPECT_FALSE(radiant_decoration_retain(view_css_resolve(tree, length), false, false));
    box = source_fragment(tree, length, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr); ASSERT_NE(box->computed_boundary, nullptr);
    EXPECT_FLOAT_EQ(box->computed_boundary->border->width.top, 3.0f);
    box = source_fragment(tree, absolute, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr); ASSERT_NE(box->computed_boundary, nullptr);
    EXPECT_FLOAT_EQ(box->computed_boundary->border->width.top, 1.0f); EXPECT_FLOAT_EQ(box->computed_boundary->padding.bottom, 4.0f);
    EXPECT_FALSE(radiant_decoration_retain(view_css_resolve(tree, absolute), false, false));
    EXPECT_FALSE(radiant_decoration_retain(view_css_resolve(tree, absolute), true, true));
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
}

TEST_F(SecondaryViewTest, ProportionalColumnsRepeatWeightsAndRemeasureAcrossPageWidths) {
    init_vector_engine();
    stylesheet("@page{size:240px 80px;margin:10px}@page :left{size:200px 80px}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, "table-layout:fixed;width:100%;border-spacing:4px 2px", "table"); ASSERT_NE(table, nullptr);
    DomElement* fixed = block(nullptr, "width:20px;background:red", "col", table); ASSERT_NE(fixed, nullptr);
    DomElement* repeated = block(nullptr, "background:blue", "col", table); ASSERT_NE(repeated, nullptr);
    ASSERT_TRUE(repeated->set_attribute("span", "2")); ASSERT_TRUE(repeated->set_attribute("r:column-proportion", "2"));
    ASSERT_NE(block(nullptr, "background:green", "col", table), nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    const char* texts[] = {"F", "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", "C", "D"}; DomElement* cells[4] = {};
    for (size_t i = 0; i < 4; i++) {
        cells[i] = block(nullptr, i == 1 ? "width:999px" : nullptr, "td", row); ASSERT_NE(cells[i], nullptr);
        ASSERT_NE(block(texts[i], nullptr, "p", cells[i]), nullptr);
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 3u);
    for (size_t i = 0; i < 4; i++) {
        ViewNodeState* state = view_tree_node_state(tree, cells[i], false); ASSERT_NE(state, nullptr); size_t boxes = 0;
        for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) if (box->paint_box) {
            uint32_t page = occurrence_page(box); boxes++; float unit = page == 2 ? 28.0f : 36.0f;
            float width = !i ? 20.0f : i == 3 ? unit : unit * 2.0f;
            float x = !i ? 14.0f : 38.0f + (i - 1) * (unit * 2.0f + 4.0f);
            EXPECT_FLOAT_EQ(box->rect.width, width); EXPECT_FLOAT_EQ(box->rect.x, x);
            EXPECT_LE(box->rect.y + box->rect.height, 70.0f);
        }
        EXPECT_EQ(boxes, 3u);
    }
    DomText* text = cells[1]->first_child_element()->first_child->as_text(); ASSERT_NE(text, nullptr);
    ViewNodeState* state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr); size_t bytes = 0;
    for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) { EXPECT_EQ(box->text_start, bytes); bytes += box->text_length; }
    EXPECT_EQ(bytes, text->length); EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
    ViewPreviewOptions preview = view_preview_options_default(); ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ImageSurface* before = render_secondary_page_snapshot(retained, 2, 1.0f); ASSERT_NE(before, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* after = render_secondary_page_snapshot(retained, 2, 1.0f); ASSERT_NE(after, nullptr);
    ASSERT_EQ(before->width, after->width); ASSERT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
}

TEST_F(SecondaryViewTest, ProportionalColumnsRetainCssBasesAndExplicitGroupInheritanceWithSourceDiagnostics) {
    stylesheet("@page{size:240px 100px;margin:10px}p,div{margin:0;font:10px/12px Arial}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, "table-layout:fixed;width:100%;border-spacing:0", "table"); ASSERT_NE(table, nullptr);
    DomElement* group = block(nullptr, nullptr, "colgroup", table); ASSERT_NE(group, nullptr);
    ASSERT_TRUE(group->set_attribute("r:column-proportion", "2"));
    DomElement* inherited = block(nullptr, "width:10px", "col", group); ASSERT_NE(inherited, nullptr);
    ASSERT_TRUE(inherited->set_attribute("r:column-proportion", "inherit"));
    DomElement* ordinary = block(nullptr, "width:30px", "col", group); ASSERT_NE(ordinary, nullptr);
    DomElement* weighted = block(nullptr, "width:0", "col", table); ASSERT_NE(weighted, nullptr);
    ASSERT_TRUE(weighted->set_attribute("r:column-proportion", "1"));
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr); DomElement* cells[3] = {};
    for (size_t i = 0; i < 3; i++) { cells[i] = block("X", nullptr, "td", row); ASSERT_NE(cells[i], nullptr); }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(source_fragment(tree, cells[0], VIEW_FRAGMENT_BODY, true)->rect.width, 130.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, cells[1], VIEW_FRAGMENT_BODY, true)->rect.width, 30.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, cells[2], VIEW_FRAGMENT_BODY, true)->rect.width, 60.0f);
    ASSERT_TRUE(view_css_resolve(tree, ordinary)->flow_traits == nullptr || view_css_resolve(tree, ordinary)->flow_traits->column_proportion == 0.0f);
    for (const char* invalid : {"0", "-1", "1px", "10%", "1e309", "1e-100", "auto", "calc(2)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(weighted->set_attribute("r:column-proportion", invalid)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        EXPECT_EQ(diagnostic.source.address, weighted); EXPECT_STREQ(diagnostic.reason, "column proportion requires none or a finite positive number");
    }
    ASSERT_TRUE(weighted->set_attribute("r:column-proportion", "1")); ASSERT_TRUE(table->set_attribute("style", "table-layout:auto;width:100%;border-spacing:0"));
    ASSERT_TRUE(view_tree_model_reset(tree)); EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_STREQ(diagnostic.reason, "proportional columns require a fixed table with a definite inline extent");
}

TEST_F(SecondaryViewTest, FoDirectCellsGroupIntoTransparentRowsWithSharedSpansAndComputedInheritance) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt' border-collapse='separate'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:table table-layout='fixed' width='100%'><f:table-column column-width='75pt'/><f:table-column column-width='75pt'/>"
        "<f:table-header><f:table-cell number-columns-spanned='2' starts-row='true' ends-row='true'><f:block>Header</f:block></f:table-cell></f:table-header>"
        "<f:table-footer><f:table-cell number-columns-spanned='2'><f:block>Footer</f:block></f:table-cell></f:table-footer>"
        "<f:table-body padding-before.length='3pt' color='red' font-size='10pt'>"
        "<f:table-cell number-rows-spanned='2' starts-row='from-parent()'><f:block>A</f:block></f:table-cell>"
        "<f:table-cell padding-top='from-parent(padding-top)' color='from-parent(color)'><f:block>B</f:block></f:table-cell>"
        "<f:table-cell starts-row='true' column-number='2' background-color='from-parent(color)'><f:block>C</f:block></f:table-cell>"
        "<f:table-cell starts-row='true' ends-row='true' column-number='2'><f:block>D</f:block></f:table-cell>"
        "<f:table-cell starts-row='true' ends-row='true'><f:block>E</f:block></f:table-cell>"
        "</f:table-body></f:table></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    size_t origins = 0; for (RadiantFoOrigin* origin = translated->origins; origin; origin = origin->next) origins++;
    EXPECT_EQ(translated->node_count, origins + 6); // four body rows and two furniture rows have no original FO object.
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins));
    DomElement* table = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(table, nullptr);
    DomElement* body = table->last_child_element(); ASSERT_NE(body, nullptr);
    size_t rows = 0; for (DomElement* row = body->first_child_element(); row; row = row->next_sibling_element()) {
        EXPECT_STREQ(row->tag_name, "tr"); EXPECT_STREQ(row->get_attribute("r:style-transparent"), "true"); rows++;
    }
    EXPECT_EQ(rows, 4u);
    DomElement* a = body->first_child_element()->first_child_element(); ASSERT_NE(a, nullptr);
    DomElement* b = a->next_sibling_element(); ASSERT_NE(b, nullptr); EXPECT_EQ(b->next_sibling_element(), nullptr);
    DomElement* c = body->first_child_element()->next_sibling_element()->first_child_element(); ASSERT_NE(c, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 1u);
    const ViewCssStyle* style = view_css_resolve(tree, b); ASSERT_NE(style, nullptr); EXPECT_EQ(style->parent->source, body);
    EXPECT_NEAR(style->font.font_size, 40.0f / 3.0f, 0.0001f); ASSERT_NE(style->padding[0], nullptr);
    EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 4); EXPECT_EQ(style->color.r, 255); EXPECT_EQ(style->color.g, 0);
    EXPECT_EQ(view_css_resolve(tree, c)->background.r, 255);
    EXPECT_FLOAT_EQ(source_fragment(tree, b, VIEW_FRAGMENT_BODY, true)->rect.x, 100);
    EXPECT_FLOAT_EQ(source_fragment(tree, c, VIEW_FRAGMENT_BODY, true)->rect.x, 100);
    EXPECT_GT(source_fragment(tree, a, VIEW_FRAGMENT_BODY, true)->rect.height, source_fragment(tree, b, VIEW_FRAGMENT_BODY, true)->rect.height);
    fo_options.max_nodes = translated->node_count - 1;
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(translated->root, nullptr);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    fo_options = radiant_fo_options_default();
    for (const char* invalid : {"TRUE", "1", "from-parent(color)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original->set_attribute("starts-row", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
        EXPECT_EQ(translated->diagnostic.source.address, original); EXPECT_STREQ(translated->diagnostic.property, "starts-row");
    }
}

TEST_F(SecondaryViewTest, FoDirectCellContentModelsRejectMixedRowsAndBoundariesInsideExplicitRows) {
    const char* cases[] = {
        "<f:table-body><f:table-cell><f:block>A</f:block></f:table-cell><f:table-row><f:table-cell><f:block>B</f:block></f:table-cell></f:table-row></f:table-body>",
        "<f:table-body><f:table-row><f:table-cell><f:block>A</f:block></f:table-cell></f:table-row><f:table-cell><f:block>B</f:block></f:table-cell></f:table-body>",
        "<f:table-body><f:table-row><f:table-cell starts-row='false'><f:block>A</f:block></f:table-cell></f:table-row></f:table-body>",
        "<f:table-body><f:table-row><f:table-cell ends-row='true'><f:block>A</f:block></f:table-cell></f:table-row></f:table-body>"};
    for (const char* content : cases) {
        SCOPED_TRACE(content); StrBuf* xml = strbuf_new(); ASSERT_NE(xml, nullptr);
        strbuf_append_str(xml, "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' border-collapse='separate'><f:layout-master-set>"
            "<f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/></f:simple-page-master>"
            "</f:layout-master-set><f:page-sequence master-reference='sheet'><f:flow flow-name='xsl-region-body'><f:table>");
        strbuf_append_str(xml, content); strbuf_append_str(xml, "</f:table></f:flow></f:page-sequence></f:root>");
        DomElement* fo = formatting_root(xml->str); strbuf_free(xml); ASSERT_NE(fo, nullptr);
        RadiantFoOptions options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &options);
        ASSERT_NE(translated, nullptr); EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
        EXPECT_NE(translated->diagnostic.source.address, nullptr);
    }
}

TEST_F(SecondaryViewTest, FoProportionalColumnsLowerToCommonWeightedTracksAndRejectUnsupportedFunctions) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt' border-collapse='separate'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:table table-layout='fixed' width='100%'><f:table-column column-width='30pt'/><f:table-column column-width='proportional-column-width(4 div 2 - 1)'/>"
        "<f:table-column column-width='proportional-column-width(max(1, 6 div 2))'/><f:table-body><f:table-row>"
        "<f:table-cell><f:block>F</f:block></f:table-cell><f:table-cell><f:block>A</f:block></f:table-cell>"
        "<f:table-cell><f:block>B</f:block></f:table-cell></f:table-row></f:table-body></f:table></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options);
    ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* table = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(table, nullptr);
    DomElement* col = table->first_child_element()->next_sibling_element(); ASSERT_NE(col, nullptr); EXPECT_STREQ(col->get_attribute("r:column-proportion"), "1");
    DomElement* row = table->last_child_element()->first_child_element(); ASSERT_NE(row, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const float widths[] = {40.0f, 40.0f, 120.0f}; size_t i = 0;
    for (DomElement* cell = row->first_child_element(); cell; cell = cell->next_sibling_element(), i++) {
        ASSERT_LT(i, 3u); EXPECT_FLOAT_EQ(source_fragment(tree, cell, VIEW_FRAGMENT_BODY, true)->rect.width, widths[i]);
    }
    EXPECT_EQ(i, 3u);
    DomElement* original_table = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original_table, nullptr);
    DomElement* original_col = original_table->first_child_element()->next_sibling_element(); ASSERT_NE(original_col, nullptr);
    struct Mixed { const char* expression; float width, weight; };
    const Mixed mixed[] = {{"10pt + proportional-column-width(2)", 72.0f, 2.0f},
        {"(10pt + proportional-column-width(2)) * 3", 120.0f, 6.0f},
        {"2 * 3 * proportional-column-width(1)", 320.0f / 3.0f, 6.0f},
        {"1em + proportional-column-width(1)", 47.5f, 1.0f},
        {"5% + proportional-column-width(1)", 47.5f, 1.0f},
        {"min(6pt + proportional-column-width(1), 15pt + proportional-column-width(1))", 46.0f, 1.0f},
        {"proportional-column-width(3) - proportional-column-width(1) + 6pt", 68.8f, 2.0f}};
    for (const Mixed& item : mixed) {
        SCOPED_TRACE(item.expression); ASSERT_TRUE(original_col->set_attribute("column-width", item.expression));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
        generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
        table = generated->last_child_element()->last_child_element()->first_child_element();
        col = table->first_child_element()->next_sibling_element(); row = table->last_child_element()->first_child_element();
        tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        EXPECT_FLOAT_EQ(view_css_resolve(tree, col)->flow_traits->column_proportion, item.weight);
        DomElement* cell = row->first_child_element()->next_sibling_element();
        EXPECT_NEAR(source_fragment(tree, cell, VIEW_FRAGMENT_BODY, true)->rect.width, item.width, 0.0001f);
        ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    }
    for (const char* invalid : {"proportional-column-width(0)", "proportional-column-width(-1)", "proportional-column-width(1,2)",
        "proportional-column-width(1px)", "proportional-column-width(1e309)", "proportional-column-width(1) * proportional-column-width(2)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original_col->set_attribute("column-width", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, original_col);
        EXPECT_STREQ(translated->diagnostic.property, "column-width");
    }
    ASSERT_TRUE(original_col->set_attribute("column-width", "proportional-column-width(1)"));
    ASSERT_TRUE(original_table->set_attribute("table-layout", "auto")); translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_STREQ(translated->diagnostic.property, "column-width");
}

TEST_F(SecondaryViewTest, NumberedColumnsAndCellsPreserveSourceOrderAroundSpansAndMissingSlots) {
    stylesheet("@page{size:240px 100px;margin:10px}p,div{margin:0;font:10px/12px Arial}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, "table-layout:fixed;width:100%;border-spacing:0", "table"); ASSERT_NE(table, nullptr);
    const char* numbers[] = {"3", "1", "2", "4"}; const char* widths[] = {"width:60px", "width:20px", "width:40px", "width:100px"};
    DomElement* columns[4] = {};
    DomElement* group = block(nullptr, nullptr, "colgroup", table); ASSERT_NE(group, nullptr);
    for (size_t i = 0; i < 4; i++) {
        columns[i] = block(nullptr, widths[i], "col", i < 3 ? group : table); ASSERT_NE(columns[i], nullptr);
        ASSERT_TRUE(columns[i]->set_attribute("r:column-number", numbers[i]));
    }
    DomElement* first = block(nullptr, nullptr, "tr", table); ASSERT_NE(first, nullptr);
    DomElement* wide = block("Wide", nullptr, "td", first); ASSERT_NE(wide, nullptr);
    ASSERT_TRUE(wide->set_attribute("r:column-number", "3")); ASSERT_TRUE(wide->set_attribute("colspan", "2"));
    DomElement* tall = block("Tall", nullptr, "td", first); ASSERT_NE(tall, nullptr);
    ASSERT_TRUE(tall->set_attribute("r:column-number", "1")); ASSERT_TRUE(tall->set_attribute("rowspan", "2"));
    DomElement* implicit = block("Next", nullptr, "td", first); ASSERT_NE(implicit, nullptr);
    DomElement* second = block(nullptr, nullptr, "tr", table); ASSERT_NE(second, nullptr);
    DomElement* right = block("Right", nullptr, "td", second); ASSERT_NE(right, nullptr); ASSERT_TRUE(right->set_attribute("r:column-number", "4"));
    DomElement* middle = block("Middle", nullptr, "td", second); ASSERT_NE(middle, nullptr);
    ASSERT_TRUE(middle->set_attribute("r:column-number", "2")); ASSERT_TRUE(middle->set_attribute("colspan", "2"));
    DomElement* third = block(nullptr, nullptr, "tr", table); ASSERT_NE(third, nullptr);
    DomElement* single = block("Single", nullptr, "td", third); ASSERT_NE(single, nullptr); ASSERT_TRUE(single->set_attribute("r:column-number", "3"));
    for (const char* layout : {"table-layout:fixed;width:100%;border-spacing:0", "table-layout:auto;width:100%;border-spacing:0"}) {
        SCOPED_TRACE(layout); ASSERT_TRUE(table->set_attribute("style", layout));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        const struct { DomElement* source; float x, width; size_t column, span; } expected[] = {
            {wide, 70.0f, 160.0f, 2, 2}, {tall, 10.0f, 20.0f, 0, 1}, {implicit, 30.0f, 40.0f, 1, 1},
            {right, 130.0f, 100.0f, 3, 1}, {middle, 30.0f, 100.0f, 1, 2}, {single, 70.0f, 60.0f, 2, 1}, {group, 10.0f, 120.0f, 0, 3}};
        for (const auto& e : expected) {
            LayoutViewNode* box = source_fragment(tree, e.source, VIEW_FRAGMENT_BODY, e.source != group); ASSERT_NE(box, nullptr);
            EXPECT_FLOAT_EQ(box->rect.x, e.x); EXPECT_FLOAT_EQ(box->rect.width, e.width);
            ASSERT_NE(box->table_range, nullptr); EXPECT_EQ(box->table_range->column, e.column); EXPECT_EQ(box->table_range->span, e.span);
        }
        LayoutViewNode* row = source_fragment(tree, third, VIEW_FRAGMENT_BODY, true); ASSERT_NE(row, nullptr); size_t missing = 0;
        for (LayoutViewNode* box = row->first_child; box; box = box->next_sibling) if (box->table_range && box->table_range->missing) missing++;
        EXPECT_EQ(missing, 3u); EXPECT_EQ(first->first_child_element(), wide); EXPECT_EQ(wide->next_sibling_element(), tall);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(tree->model->root, text);
        EXPECT_STREQ(text->str, "WideTallNextRightMiddleSingle"); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, NumberedRepeatedColumnsKeepWeightsAndRetainedContinuationGeometry) {
    init_vector_engine();
    stylesheet("@page{size:240px 80px;margin:10px}@page :left{size:200px 80px}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, "table-layout:fixed;width:100%;border-spacing:0", "table"); ASSERT_NE(table, nullptr);
    DomElement* repeated = block(nullptr, "background:blue", "col", table); ASSERT_NE(repeated, nullptr);
    ASSERT_TRUE(repeated->set_attribute("r:column-number", "3")); ASSERT_TRUE(repeated->set_attribute("r:column-proportion", "1")); ASSERT_TRUE(repeated->set_attribute("span", "2"));
    DomElement* fixed = block(nullptr, "width:20px;background:red", "col", table); ASSERT_NE(fixed, nullptr); ASSERT_TRUE(fixed->set_attribute("r:column-number", "1"));
    DomElement* implicit = block(nullptr, "width:40px;background:green", "col", table); ASSERT_NE(implicit, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* long_cell = block(nullptr, nullptr, "td", row); ASSERT_NE(long_cell, nullptr); ASSERT_TRUE(long_cell->set_attribute("r:column-number", "3"));
    DomElement* content = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK", nullptr, "p", long_cell); ASSERT_NE(content, nullptr);
    DomElement* left = block("Left", nullptr, "td", row); ASSERT_NE(left, nullptr); ASSERT_TRUE(left->set_attribute("r:column-number", "1"));
    DomElement* next = block("Next", nullptr, "td", row); ASSERT_NE(next, nullptr);
    DomElement* right = block("Right", nullptr, "td", row); ASSERT_NE(right, nullptr); ASSERT_TRUE(right->set_attribute("r:column-number", "4"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 3u);
    for (DomElement* cell : {long_cell, left, next, right}) {
        ViewNodeState* state = view_tree_node_state(tree, cell, false); ASSERT_NE(state, nullptr); size_t count = 0;
        for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) if (box->paint_box) {
            float unit = occurrence_page(box) == 2 ? 60.0f : 80.0f; count++;
            EXPECT_FLOAT_EQ(box->rect.width, cell == left ? 20.0f : cell == next ? 40.0f : unit);
            EXPECT_FLOAT_EQ(box->rect.x, cell == left ? 10.0f : cell == next ? 30.0f : cell == long_cell ? 70.0f : 70.0f + unit);
        }
        EXPECT_EQ(count, 3u);
    }
    DomText* text = content->first_child->as_text(); ASSERT_NE(text, nullptr); size_t bytes = 0;
    for (LayoutViewNode* box = view_tree_node_state(tree, text, false)->first_occurrence; box; box = box->next_occurrence) {
        EXPECT_EQ(box->text_start, bytes); bytes += box->text_length;
    }
    EXPECT_EQ(bytes, text->length); EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
    ViewPreviewOptions preview = view_preview_options_default(); ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ImageSurface* before = render_secondary_page_snapshot(retained, 2, 1.0f); ASSERT_NE(before, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 3;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); ImageSurface* after = render_secondary_page_snapshot(retained, 2, 1.0f); ASSERT_NE(after, nullptr);
    ASSERT_EQ(before->width, after->width); ASSERT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
}

TEST_F(SecondaryViewTest, NumberedTablePositionsRejectInvalidValuesOverlapsAndExhaustedTrackBudgets) {
    stylesheet("@page{size:240px 100px;margin:10px}p,div{margin:0;font:10px/12px Arial}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, "table-layout:fixed;width:100%;border-spacing:0", "table"); ASSERT_NE(table, nullptr);
    DomElement* col = block(nullptr, "width:100px", "col", table); ASSERT_NE(col, nullptr);
    DomElement* second = block(nullptr, "width:100px", "col", table); ASSERT_NE(second, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr); ASSERT_TRUE(row->set_attribute("r:column-number", "2"));
    DomElement* cell = block("X", nullptr, "td", row); ASSERT_NE(cell, nullptr);
    DomElement* next = block("Y", nullptr, "td", row); ASSERT_NE(next, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_TRUE(!view_css_resolve(tree, cell)->flow_traits || view_css_resolve(tree, cell)->flow_traits->column_number == 0);
    for (DomElement* target : {col, cell}) for (const char* invalid : {"0", "-1", "1.5", "1px", "2%", "2147483648", "1e2", "none", "calc(2)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(target->set_attribute("r:column-number", invalid)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        EXPECT_EQ(diagnostic.source.address, target); EXPECT_STREQ(diagnostic.reason, "column number requires auto or a positive integer in the counter domain");
        ASSERT_TRUE(target->set_attribute("r:column-number", "auto"));
    }
    ASSERT_TRUE(cell->set_attribute("r:column-number", "inherit")); ASSERT_TRUE(next->set_attribute("r:column-number", "1")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(source_fragment(tree, cell, VIEW_FRAGMENT_BODY, true)->rect.x, 120.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, next, VIEW_FRAGMENT_BODY, true)->rect.x, 10.0f);
    ASSERT_TRUE(next->set_attribute("r:column-number", "2")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, next);
    EXPECT_STREQ(diagnostic.reason, "overlapping table spans require a valid rectangular grid"); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(next->set_attribute("r:column-number", "1")); ASSERT_TRUE(second->set_attribute("r:column-number", "1")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, second);
    EXPECT_STREQ(diagnostic.reason, "overlapping table column declarations require a column property conflict policy");
    ASSERT_TRUE(second->set_attribute("r:column-number", "auto")); ASSERT_TRUE(cell->set_attribute("rowspan", "2"));
    DomElement* second_row = block(nullptr, nullptr, "tr", table); ASSERT_NE(second_row, nullptr);
    DomElement* collision = block("Collision", nullptr, "td", second_row); ASSERT_NE(collision, nullptr);
    ASSERT_TRUE(collision->set_attribute("r:column-number", "2")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, collision);
    EXPECT_STREQ(diagnostic.reason, "overlapping table spans require a valid rectangular grid"); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(col->set_attribute("r:column-number", "1000")); ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 50;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(diagnostic.source.address, col); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, NumberedSparseColumnsLeaveAutomaticTracksAndImplicitDeclarationsAtTheirSourceEndpoint) {
    stylesheet("@page{size:240px 100px;margin:10px}p,div{margin:0;font:10px/12px Arial}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, "table-layout:fixed;width:100%;border-spacing:0", "table"); ASSERT_NE(table, nullptr);
    DomElement* fifth = block(nullptr, "width:20px;background:red", "col", table); ASSERT_NE(fifth, nullptr);
    ASSERT_TRUE(fifth->set_attribute("r:column-number", "5"));
    DomElement* sixth = block(nullptr, "width:20px;background:blue", "col", table); ASSERT_NE(sixth, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* span = block("Span", nullptr, "td", row); ASSERT_NE(span, nullptr);
    ASSERT_TRUE(span->set_attribute("r:column-number", "3")); ASSERT_TRUE(span->set_attribute("colspan", "2"));
    DomElement* next = block("V", nullptr, "td", row); ASSERT_NE(next, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(source_fragment(tree, span, VIEW_FRAGMENT_BODY, true)->rect.x, 100.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, span, VIEW_FRAGMENT_BODY, true)->rect.width, 90.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, next, VIEW_FRAGMENT_BODY, true)->rect.x, 190.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, next, VIEW_FRAGMENT_BODY, true)->rect.width, 20.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, sixth, VIEW_FRAGMENT_BODY, false)->rect.x, 210.0f);
    LayoutViewNode* fragment = source_fragment(tree, row, VIEW_FRAGMENT_BODY, true); ASSERT_NE(fragment, nullptr); size_t missing = 0;
    for (LayoutViewNode* box = fragment->first_child; box; box = box->next_sibling) if (box->table_range && box->table_range->missing) missing++;
    EXPECT_EQ(missing, 3u);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 5;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(diagnostic.source.address, sixth);
    EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, FoNumberedColumnsAndCellsLowerRoundedPositionsWithoutReorderingSources) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt' border-collapse='separate'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:table table-layout='fixed' width='100%'><f:table-column column-number='1.3 * 2' column-width='30pt'/><f:table-column column-number='-7 mod 3' column-width='30pt'/>"
        "<f:table-column column-width='30pt'/><f:table-body><f:table-row><f:table-cell column-number='floor(6.8 div 2)'><f:block>Third</f:block></f:table-cell>"
        "<f:table-cell column-number='-1'><f:block>First</f:block></f:table-cell><f:table-cell><f:block>Second</f:block></f:table-cell>"
        "</f:table-row></f:table-body></f:table></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options);
    ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* table = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(table, nullptr);
    EXPECT_STREQ(table->first_child_element()->get_attribute("r:column-number"), "3");
    EXPECT_STREQ(table->first_child_element()->next_sibling_element()->get_attribute("r:column-number"), "1");
    DomElement* row = table->last_child_element()->first_child_element(); ASSERT_NE(row, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const float positions[] = {400.0f / 3.0f, 0.0f, 200.0f / 3.0f}; size_t i = 0;
    for (DomElement* cell = row->first_child_element(); cell; cell = cell->next_sibling_element(), i++) {
        ASSERT_LT(i, 3u); EXPECT_NEAR(source_fragment(tree, cell, VIEW_FRAGMENT_BODY, true)->rect.x, positions[i], 0.001f);
    }
    EXPECT_EQ(i, 3u);
    DomElement* original_table = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original_table, nullptr);
    DomElement* original_cell = original_table->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original_cell, nullptr);
    for (const char* invalid : {"1001", "1px", "1e309", "inherit", "auto", "1,2"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original_cell->set_attribute("column-number", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, original_cell);
        EXPECT_STREQ(translated->diagnostic.property, "column-number");
    }
}

TEST_F(SecondaryViewTest, TableFurnitureOmissionReservesOnlyTheActualFirstAndLastGroups) {
    stylesheet("@page{size:240px 80px;margin:10px}@page :left{size:200px 80px}"
        "p,div{margin:0;font:10px/12px Arial}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* header = nullptr; DomElement* footer = nullptr; DomElement* table = fixed_table(9, &header, &footer); ASSERT_NE(table, nullptr);
    for (bool omit_header : {false, true}) for (bool omit_footer : {false, true}) {
        SCOPED_TRACE(omit_header);
        SCOPED_TRACE(omit_footer);
        ASSERT_TRUE(table->set_attribute("r:table-omit-header-at-break", omit_header ? "true" : "false"));
        ASSERT_TRUE(table->set_attribute("r:table-omit-footer-at-break", omit_footer ? "true" : "false"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 3u);
        for (DomElement* group : {header, footer}) {
            ViewNodeState* state = view_tree_node_state(tree, group->first_child_element(), false); ASSERT_NE(state, nullptr); size_t count = 0;
            for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) if (box->paint_box) {
                count++; EXPECT_LE(box->rect.y + box->rect.height, 70.0f);
                if (group == header && omit_header) EXPECT_EQ(occurrence_page(box), 1u);
                if (group == footer && omit_footer) EXPECT_EQ(occurrence_page(box), 3u);
                EXPECT_FLOAT_EQ(box->rect.width, occurrence_page(box) == 2 ? 180.0f : 220.0f);
            }
            EXPECT_EQ(count, (group == header ? omit_header : omit_footer) ? 1u : 3u);
        }
        size_t counts[3] = {};
        for (DomElement* row = table->last_child_element()->first_child_element(); row; row = row->next_sibling_element()) {
            LayoutViewNode* box = source_fragment(tree, row, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
            counts[occurrence_page(box) - 1]++; EXPECT_LE(box->rect.y + box->rect.height, 70.0f);
        }
        const size_t expected[4][3] = {{3,3,3},{4,4,1},{3,4,2},{4,4,1}};
        for (size_t i = 0; i < 3; i++) EXPECT_EQ(counts[i], expected[(omit_header ? 2 : 0) + (omit_footer ? 1 : 0)][i]);
    }
}

TEST_F(SecondaryViewTest, TerminalTableFooterRetainsARealCellTailAndSurvivesFailedRecomposition) {
    init_vector_engine();
    stylesheet("@page{size:240px 80px;margin:10px}p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* header = nullptr; DomElement* footer = nullptr; DomElement* table = fixed_table(1, &header, &footer); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->DomNode::remove_child(header)); ASSERT_TRUE(table->set_attribute("r:table-omit-footer-at-break", "true"));
    DomElement* cell = table->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(cell, nullptr);
    DomText* text = table_cell_text(cell, "A\nB\nC\nD\nE"); ASSERT_NE(text, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 2u);
    size_t bytes = 0, lines[2] = {};
    for (LayoutViewNode* box = view_tree_node_state(tree, text, false)->first_occurrence; box; box = box->next_occurrence) {
        EXPECT_EQ(box->text_start, bytes); bytes += box->text_length;
        // newline records retain source coverage without painting another line.
        if (box->glyph_run) {
            uint32_t page = occurrence_page(box); ASSERT_GE(page, 1u); ASSERT_LE(page, 2u);
            EXPECT_FLOAT_EQ(box->rect.y, 10.0f + lines[page - 1] * 12.0f); lines[page - 1]++;
        }
    }
    EXPECT_EQ(bytes, text->length); EXPECT_EQ(lines[0], 4u); EXPECT_EQ(lines[1], 1u); EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
    ViewNodeState* state = view_tree_node_state(tree, footer->first_child_element(), false); ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->occurrence_count, 1u); EXPECT_EQ(occurrence_page(state->first_occurrence), 2u);
    EXPECT_FLOAT_EQ(source_fragment(tree, table, VIEW_FRAGMENT_BODY, true)->rect.height, 48.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, table, VIEW_FRAGMENT_BODY, true, true)->rect.height, 24.0f);
    ViewPreviewOptions preview = view_preview_options_default(); ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ImageSurface* before = render_secondary_page_snapshot(retained, 2, 1.0f); ASSERT_NE(before, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); ImageSurface* after = render_secondary_page_snapshot(retained, 2, 1.0f); ASSERT_NE(after, nullptr);
    ASSERT_EQ(before->width, after->width); ASSERT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
}

TEST_F(SecondaryViewTest, TableFurnitureFlagsInheritOnlyExplicitlyAndFoRejectsInvalidOriginalValues) {
    stylesheet("@page{size:240px 80px;margin:10px}p,div{margin:0;font:10px/12px Arial}td{padding:0}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    ASSERT_TRUE(source->set_attribute("r:table-omit-header-at-break", "true"));
    ASSERT_TRUE(source->set_attribute("r:table-omit-footer-at-break", "true"));
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr); ViewTree* tree = secondary();
    ViewCssStyle* style = view_css_resolve(tree, table); ASSERT_NE(style, nullptr);
    EXPECT_TRUE(!style->flow_traits || (!style->flow_traits->table_omit[0] && !style->flow_traits->table_omit[1]));
    ASSERT_TRUE(table->set_attribute("r:table-omit-header-at-break", "inherit")); ASSERT_TRUE(table->set_attribute("r:table-omit-footer-at-break", "inherit"));
    ASSERT_TRUE(view_tree_model_reset(tree)); style = view_css_resolve(tree, table); ASSERT_NE(style->flow_traits, nullptr);
    EXPECT_TRUE(style->flow_traits->table_omit[0]); EXPECT_TRUE(style->flow_traits->table_omit[1]);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_TRUE(table->set_attribute("r:table-omit-header-at-break", "sometimes")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, table);
    EXPECT_STREQ(diagnostic.reason, "table furniture omission requires true, false or inherit"); EXPECT_EQ(tree->model->page_count, 0u);
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' border-collapse='separate'>"
        "<f:layout-master-set><f:simple-page-master master-name='s' page-width='150pt' page-height='120pt'><f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='s' force-page-count='no-force'><f:flow flow-name='xsl-region-body'><f:table table-omit-header-at-break='true' table-omit-footer-at-break='true'>"
        "<f:table-body><f:table-row><f:table-cell><f:block>X</f:block></f:table-cell></f:table-row></f:table-body></f:table></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr);
    DomElement* lowered = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(lowered, nullptr);
    EXPECT_STREQ(lowered->get_attribute("r:table-omit-header-at-break"), "true"); EXPECT_STREQ(lowered->get_attribute("r:table-omit-footer-at-break"), "true");
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    for (const char* property : {"table-omit-header-at-break", "table-omit-footer-at-break"}) {
        ASSERT_TRUE(original->set_attribute(property, "sometimes")); translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, original); EXPECT_STREQ(translated->diagnostic.property, property);
        ASSERT_TRUE(original->set_attribute(property, "true"));
    }
}

TEST_F(SecondaryViewTest, SplitTableCellDecorationsHonorSliceAndClone) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* footer = nullptr; DomElement* table = fixed_table(1, nullptr, &footer, "2"); ASSERT_NE(table, nullptr);
    DomElement* cell = footer->next_sibling->as_element()->first_child->as_element()->first_child->as_element();
    ASSERT_NE(table_cell_text(cell, "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ"), nullptr);
    for (bool clone : {false, true}) {
        ASSERT_TRUE(cell->set_attribute("style", clone ? "box-decoration-break: clone" : "box-decoration-break: slice"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 2u);
        ViewNodeState* state = view_tree_node_state(tree, cell, false); ASSERT_NE(state, nullptr);
        size_t page = 0;
        for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
            if (!fragment->paint_box) continue;
            ASSERT_NE(fragment->computed_boundary, nullptr); ASSERT_NE(fragment->computed_boundary->border, nullptr);
            EXPECT_FLOAT_EQ(fragment->computed_boundary->border->width.top, clone || !page ? 1.0f : 0.0f);
            EXPECT_FLOAT_EQ(fragment->computed_boundary->border->width.bottom, clone || page ? 1.0f : 0.0f);
            page++;
        }
        EXPECT_EQ(page, 2u);
    }
}

TEST_F(SecondaryViewTest, RetainedTablePagesKeepTheirProducerAndPaintAfterSourceViewReset) {
    init_vector_engine();
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* table = fixed_table(9); ASSERT_NE(table, nullptr);
    for (const char* layout : {"table-layout: fixed; width: 100%; border-spacing: 0", "table-layout: auto; width: 100%; border-spacing: 0"}) {
        SCOPED_TRACE(layout); ASSERT_TRUE(table->set_attribute("style", layout));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
        ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
        ViewPreviewOptions preview = view_preview_options_default(); preview.rows = 1; preview.columns = 3; preview.scale = .5f;
        ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
        ImageSurface* before = render_secondary_view_snapshot(retained); ASSERT_NE(before, nullptr);
        tree->reset_retained();
        ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
        ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
        ImageSurface* after = render_secondary_view_snapshot(retained); ASSERT_NE(after, nullptr);
        EXPECT_EQ(before->width, after->width); EXPECT_EQ(before->height, after->height);
        for (int y = 0; y < before->height; y++)
            EXPECT_EQ(memcmp((uint8_t*)before->pixels + y * before->pitch,
                (uint8_t*)after->pixels + y * after->pitch, (size_t)before->width * 4), 0);
        image_surface_destroy(before); image_surface_destroy(after);
    }
}

TEST_F(SecondaryViewTest, TableGroupForcedBreaksRepeatFurnitureAndDoNotCreateTrailingPages) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(2, &header, &footer); ASSERT_NE(table, nullptr);
    DomElement* first = table->last_child->as_element();
    ASSERT_TRUE(first->set_attribute("style", "break-before: page; break-after: page"));
    DomElement* second = block(nullptr, "break-before: page; break-after: page", "tbody", table);
    DomElement* third = block(nullptr, "break-before: page; break-after: page", "tbody", table);
    ASSERT_TRUE(table_rows(second, 2)); ASSERT_TRUE(table_rows(third, 1));
    DomText* trailing = DomText::create_copy("\n  \t", 4, source); ASSERT_NE(trailing, nullptr);
    ASSERT_TRUE(source->DomNode::append_child(trailing));
    for (const char* algorithm : {"fixed", "auto"}) {
        SCOPED_TRACE(algorithm);
        StrBuf* css = strbuf_new(); strbuf_append_format(css, "table-layout: %s; width: 100%%; border-spacing: 4px 2px", algorithm);
        ASSERT_TRUE(table->set_attribute("style", css->str)); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 3u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
        ViewNodeState* whitespace = view_tree_node_state(tree, trailing, false); ASSERT_NE(whitespace, nullptr);
        ASSERT_NE(whitespace->first_occurrence, nullptr); EXPECT_EQ(occurrence_page(whitespace->first_occurrence), 3u);
        EXPECT_EQ(whitespace->first_occurrence->text_length, 4u); EXPECT_FLOAT_EQ(whitespace->first_occurrence->rect.height, 0.0f);
        size_t page = 0;
        for (DomElement* group : {first, second, third}) {
            page++;
            for (DomNode* row = group->first_child; row; row = row->next_sibling) {
                ViewNodeState* state = view_tree_node_state(tree, row, false); ASSERT_NE(state, nullptr);
                EXPECT_EQ(state->occurrence_count, 1u); EXPECT_EQ(occurrence_page(state->first_occurrence), page);
            }
        }
        for (DomElement* group : {header, footer}) {
            ViewNodeState* state = view_tree_node_state(tree, group->first_child, false); ASSERT_NE(state, nullptr);
            EXPECT_EQ(state->occurrence_count, 3u);
        }
    }
    EXPECT_EQ(first->parent, table); EXPECT_EQ(second->parent, table); EXPECT_EQ(third->parent, table);
}

TEST_F(SecondaryViewTest, AdjoiningTableGroupBreaksResolveSidednessBeforeInsertingBlankPages) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* header = nullptr; DomElement* table = fixed_table(1, &header); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->last_child->as_element()->set_attribute("style", "break-after: left"));
    DomElement* second = block(nullptr, "break-before: right", "tbody", table); ASSERT_TRUE(table_rows(second, 1));
    for (bool row_override : {false, true}) {
        ASSERT_TRUE(second->first_child->as_element()->set_attribute("style", row_override ? "break-before: left" : ""));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        EXPECT_EQ(tree->model->page_count, row_override ? 2u : 3u);
        ViewNodeState* state = view_tree_node_state(tree, second->first_child, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(occurrence_page(state->first_occurrence), row_override ? 2u : 3u);
        state = view_tree_node_state(tree, header->first_child, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(state->occurrence_count, 2u);
        if (!row_override && tree->model->page_count == 3u) EXPECT_TRUE(tree->model->pages.get()[1]->blank);
    }
}

TEST_F(SecondaryViewTest, AvoidedTableGroupsMoveTogetherWithSpacingAndColumnSpans) {
    stylesheet("@page { size: 240px 120px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* table = fixed_table(2, nullptr, nullptr, "2"); ASSERT_NE(table, nullptr);
    DomElement* second = block(nullptr, "break-inside: avoid", "tbody", table); ASSERT_TRUE(table_rows(second, 2, "Kept A", "Kept B", "2"));
    for (const char* algorithm : {"fixed", "auto"}) {
        StrBuf* css = strbuf_new(); strbuf_append_format(css, "table-layout: %s; width: 100%%; border-spacing: 4px 2px", algorithm);
        ASSERT_TRUE(table->set_attribute("style", css->str)); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
        for (DomNode* row = second->first_child; row; row = row->next_sibling) {
            ViewNodeState* state = view_tree_node_state(tree, row, false); ASSERT_NE(state, nullptr);
            EXPECT_EQ(state->occurrence_count, 1u); EXPECT_EQ(occurrence_page(state->first_occurrence), 2u);
        }
    }
}

TEST_F(SecondaryViewTest, OversizedAvoidedTableGroupsRelaxWithoutLosingOrRepeatingRows) {
    stylesheet("@page { size: 240px 120px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top } thead, tfoot { break-inside: avoid }");
    DomElement* table = fixed_table(6); ASSERT_NE(table, nullptr);
    DomElement* body = table->last_child->as_element(); ASSERT_TRUE(body->set_attribute("style", "break-inside: avoid"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 1u);
    size_t index = 0;
    for (DomNode* row = body->first_child; row; row = row->next_sibling) {
        ViewNodeState* state = view_tree_node_state(tree, row, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(state->occurrence_count, 1u); EXPECT_EQ(occurrence_page(state->first_occurrence), 1u + index++ / 3u);
    }
}

TEST_F(SecondaryViewTest, ForcedRowBreaksOverrideTableGroupAvoidanceWithoutRelaxation) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* table = fixed_table(4); ASSERT_NE(table, nullptr);
    DomElement* body = table->last_child->as_element(); ASSERT_TRUE(body->set_attribute("style", "break-inside: avoid"));
    ASSERT_TRUE(body->first_child->next_sibling->as_element()->set_attribute("style", "break-before: page"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
    size_t index = 0;
    for (DomNode* row = body->first_child; row; row = row->next_sibling) {
        ViewNodeState* state = view_tree_node_state(tree, row, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(occurrence_page(state->first_occurrence), index++ ? 2u : 1u);
    }
}

TEST_F(SecondaryViewTest, TableGroupBoundaryAvoidanceCanChooseAnEarlierRow) {
    stylesheet("@page { size: 240px 120px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* table = fixed_table(3); ASSERT_NE(table, nullptr);
    DomElement* first = table->last_child->as_element();
    DomElement* second = block(nullptr, nullptr, "tbody", table); ASSERT_TRUE(table_rows(second, 1));
    for (bool before : {false, true}) {
        ASSERT_TRUE(first->set_attribute("style", before ? "" : "break-after: avoid"));
        ASSERT_TRUE(second->set_attribute("style", before ? "break-before: avoid" : ""));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
        ViewNodeState* last = view_tree_node_state(tree, first->last_child, false); ASSERT_NE(last, nullptr);
        ViewNodeState* next = view_tree_node_state(tree, second->first_child, false); ASSERT_NE(next, nullptr);
        EXPECT_EQ(occurrence_page(last->first_occurrence), 2u); EXPECT_EQ(occurrence_page(next->first_occurrence), 2u);
    }
}

TEST_F(SecondaryViewTest, AvoidedTableGroupsRetainIndependentOversizedCellContinuations) {
    stylesheet("@page { size: 240px 120px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    DomElement* body = table->last_child->as_element(); ASSERT_TRUE(body->set_attribute("style", "break-inside: avoid"));
    DomElement* cell = body->first_child->as_element()->first_child->as_element();
    DomText* text = table_cell_text(cell, "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ"); ASSERT_NE(text, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 1u);
    ViewNodeState* state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr);
    size_t bytes = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        EXPECT_EQ(fragment->text_start, bytes); bytes += fragment->text_length;
    }
    EXPECT_EQ(bytes, text->length);
}

TEST_F(SecondaryViewTest, TableGroupEdgeKeepsReachAdjoiningOuterBlocks) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    for (bool before : {false, true}) {
        DomElement* leading = block("Leading", "height: 40px"); ASSERT_NE(leading, nullptr);
        DomElement* kept = before ? block("Kept", "height: 20px") : nullptr;
        DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
        ASSERT_TRUE(table->last_child->as_element()->set_attribute("style", before ? "break-before: avoid" : "break-after: avoid"));
        DomElement* tail = before ? nullptr : block("Tail");
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
        ViewNodeState* state = view_tree_node_state(tree, table->last_child->as_element()->first_child, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(occurrence_page(state->first_occurrence), 2u);
        state = view_tree_node_state(tree, before ? kept : tail, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(occurrence_page(state->first_occurrence), 2u);
        ASSERT_TRUE(source->DomNode::remove_child(leading)); ASSERT_TRUE(source->DomNode::remove_child(table));
        ASSERT_TRUE(source->DomNode::remove_child(before ? kept : tail));
    }
}

TEST_F(SecondaryViewTest, GroupBreakAfterAndFollowingBlockBeforeShareOneSidedBoundary) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* header = nullptr; DomElement* table = fixed_table(1, &header); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->last_child->as_element()->set_attribute("style", "break-after: left"));
    DomElement* tail = block("Tail", "break-before: right"); ASSERT_NE(tail, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_TRUE(tree->model->pages.get()[1]->blank);
    ViewNodeState* table_state = view_tree_node_state(tree, table, false); ASSERT_NE(table_state, nullptr);
    EXPECT_EQ(table_state->occurrence_count, 1u);
    ViewNodeState* state = view_tree_node_state(tree, header->first_child, false); ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->occurrence_count, 1u);
    state = view_tree_node_state(tree, tail, false); ASSERT_NE(state, nullptr);
    EXPECT_EQ(occurrence_page(state->first_occurrence), 3u);
}

TEST_F(SecondaryViewTest, AdjoiningTablesResolveFirstGroupSidednessAndCloseBeforeAdvancing) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* first = fixed_table(1); DomElement* second = fixed_table(1);
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    ASSERT_TRUE(first->last_child->as_element()->set_attribute("style", "break-after: right"));
    ASSERT_TRUE(second->last_child->as_element()->set_attribute("style", "break-before: left"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    for (DomElement* table : {first, second}) {
        ViewNodeState* state = view_tree_node_state(tree, table, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(state->occurrence_count, 1u);
        EXPECT_EQ(occurrence_page(state->first_occurrence), table == first ? 1u : 2u);
    }
}

TEST_F(SecondaryViewTest, TerminalTableGroupBreakDistinguishesCollapsedAndPreservedWhitespace) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->last_child->as_element()->set_attribute("style", "break-after: page"));
    DomText* trailing = DomText::create_copy("\n", 1, source); ASSERT_NE(trailing, nullptr);
    ASSERT_TRUE(source->DomNode::append_child(trailing));
    for (bool preserve : {false, true}) {
        ASSERT_TRUE(source->set_attribute("style", preserve ? "white-space: pre-wrap" : "white-space: normal"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        EXPECT_EQ(tree->model->page_count, preserve ? 2u : 1u);
        ViewNodeState* state = view_tree_node_state(tree, trailing, false); ASSERT_NE(state, nullptr);
        ASSERT_NE(state->first_occurrence, nullptr); EXPECT_EQ(occurrence_page(state->first_occurrence), preserve ? 2u : 1u);
        EXPECT_FLOAT_EQ(state->first_occurrence->rect.height, preserve ? 12.0f : 0.0f);
        EXPECT_EQ(state->first_occurrence->text_length, 1u);
    }
}

TEST_F(SecondaryViewTest, TerminalTableGroupBreakRetainsFollowingZeroHeightInsertionAnchors) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p, div { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    ASSERT_TRUE(table->last_child->as_element()->set_attribute("style", "break-after: page"));
    DomElement* floating = block("Float", "float: top; float-reference: page; height: 20px"); ASSERT_NE(floating, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, floating, false); ASSERT_NE(state, nullptr);
    size_t painted = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (fragment->paint_box) { EXPECT_EQ(occurrence_page(fragment), 2u); painted++; }
    EXPECT_EQ(painted, 1u);
}

TEST_F(SecondaryViewTest, AdditionalHeaderGroupsUseBodyGroupBreaksAndAreNotRepeated) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* header = nullptr; DomElement* table = fixed_table(1, &header); ASSERT_NE(table, nullptr);
    DomElement* extra = block(nullptr, "break-before: page; break-inside: avoid", "thead", table);
    ASSERT_TRUE(table_rows(extra, 2));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
    ViewNodeState* state = view_tree_node_state(tree, header->first_child, false); ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->occurrence_count, 2u);
    for (DomNode* row = extra->first_child; row; row = row->next_sibling) {
        state = view_tree_node_state(tree, row, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(state->occurrence_count, 1u); EXPECT_EQ(occurrence_page(state->first_occurrence), 2u);
        EXPECT_EQ(state->first_occurrence->role, VIEW_FRAGMENT_BODY);
    }
}

TEST_F(SecondaryViewTest, TableCellMiddleAndBottomAlignContentWithoutMovingCellBorders) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px 1px 6px; border: 1px solid black; vertical-align: top }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 4px 2px", "table");
    DomElement* row = block(nullptr, "height: 46px", "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cells[] = {block("Top", nullptr, "td", row),
        block("Middle", "vertical-align: middle; height: 30px", "td", row),
        block("Bottom", "vertical-align: bottom", "td", row)};
    for (DomElement* cell : cells) ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cells[1]->set_attribute("colspan", "2"));
    for (const char* algorithm : {"fixed", "auto"}) {
        StrBuf* css = strbuf_new(); strbuf_append_format(css, "table-layout: %s; width: 100%%; border-spacing: 4px 2px", algorithm);
        ASSERT_TRUE(table->set_attribute("style", css->str)); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 1u);
        ViewNodeState* row_state = view_tree_node_state(tree, row, false); ASSERT_NE(row_state, nullptr);
        LayoutViewNode* row_box = row_state->first_occurrence;
        const float content_y[] = {3.0f, 15.0f, 27.0f};
        float baseline = 0.0f;
        for (size_t i = 0; i < 3; i++) {
            ViewNodeState* state = view_tree_node_state(tree, cells[i], false); ASSERT_NE(state, nullptr);
            LayoutViewNode* box = state->first_occurrence; ASSERT_NE(box, nullptr);
            EXPECT_FLOAT_EQ(box->rect.y, row_box->rect.y); EXPECT_FLOAT_EQ(box->rect.height, 46.0f);
            ASSERT_NE(box->computed_boundary, nullptr);
            EXPECT_FLOAT_EQ(box->computed_boundary->padding.top, 2.0f);
            EXPECT_FLOAT_EQ(box->computed_boundary->padding.bottom, 6.0f);
            state = view_tree_node_state(tree, cells[i]->first_child, false); ASSERT_NE(state, nullptr);
            LayoutViewNode* text = state->first_occurrence; ASSERT_NE(text, nullptr); ASSERT_NE(text->glyph_run, nullptr);
            EXPECT_FLOAT_EQ(text->rect.y - row_box->rect.y, content_y[i]);
            if (!i) baseline = text->glyph_run->baseline_y - text->rect.y;
            EXPECT_FLOAT_EQ(text->glyph_run->baseline_y - text->rect.y, baseline);
        }
    }
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, RepeatedTableFurniturePreservesMiddleAndBottomCellAlignment) {
    stylesheet("@page { size: 240px 160px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(4, &header, &footer); ASSERT_NE(table, nullptr);
    for (DomElement* group : {header, footer}) {
        DomElement* row = group->first_child->as_element(); ASSERT_TRUE(row->set_attribute("style", "height: 42px"));
        ASSERT_TRUE(row->first_child->as_element()->set_attribute("style", "vertical-align: middle"));
        ASSERT_TRUE(row->last_child->as_element()->set_attribute("style", "vertical-align: bottom"));
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    for (DomElement* group : {header, footer}) {
        DomElement* row = group->first_child->as_element();
        for (DomNode* cell = row->first_child; cell; cell = cell->next_sibling) {
            ViewNodeState* state = view_tree_node_state(tree, cell->as_element()->first_child, false); ASSERT_NE(state, nullptr);
            size_t bytes[2] = {};
            for (LayoutViewNode* text = state->first_occurrence; text; text = text->next_occurrence) {
                uint32_t page = occurrence_page(text); ASSERT_GE(page, 1u); ASSERT_LE(page, 2u);
                EXPECT_EQ(text->text_start, bytes[page - 1]); bytes[page - 1] += text->text_length;
                EXPECT_EQ(text->role, page == 1 ? VIEW_FRAGMENT_BODY : VIEW_FRAGMENT_REPEATED_TABLE);
                LayoutViewNode* box = text->parent->parent; EXPECT_EQ(box->source.address, cell);
                EXPECT_FLOAT_EQ(box->rect.height, 42.0f);
                EXPECT_FLOAT_EQ(text->rect.y - box->rect.y, cell == row->first_child ? 15.0f : 27.0f);
            }
            EXPECT_EQ(bytes[0], cell->as_element()->first_child->as_text()->length); EXPECT_EQ(bytes[1], bytes[0]);
        }
    }
}

TEST_F(SecondaryViewTest, SplitTableCellsAlignEachSliceAndRetainIndependentSourceCursors) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cells[] = {block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", nullptr, "td", row),
        block("A\nB\nC\nD\nE\nF\nG\nH", "vertical-align: middle", "td", row),
        block("Z", "vertical-align: bottom", "td", row)};
    for (DomElement* cell : cells) ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cells[1]->set_attribute("colspan", "2"));
    for (bool clone : {false, true}) {
        ASSERT_TRUE(row->set_attribute("style", clone ? "box-decoration-break: clone" : "box-decoration-break: slice"));
        for (size_t i = 0; i < 3; i++) {
            StrBuf* css = strbuf_new(); strbuf_append_format(css, "vertical-align: %s; box-decoration-break: %s",
                i == 0 ? "top" : i == 1 ? "middle" : "bottom", clone ? "clone" : "slice");
            ASSERT_TRUE(cells[i]->set_attribute("style", css->str)); strbuf_free(css);
        }
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 2u);
        for (size_t i = 0; i < 3; i++) {
            DomText* source_text = cells[i]->first_child->as_text();
            ViewNodeState* state = view_tree_node_state(tree, source_text, false); ASSERT_NE(state, nullptr);
            size_t bytes = 0; bool second = false;
            for (LayoutViewNode* text = state->first_occurrence; text; text = text->next_occurrence) {
                EXPECT_EQ(text->text_start, bytes); bytes += text->text_length;
                if (!text->glyph_run) continue;
                LayoutViewNode* cell = text->parent->parent;
                if (i == 2) {
                    EXPECT_EQ(occurrence_page(text), 1u); EXPECT_FLOAT_EQ(text->rect.y - cell->rect.y, 63.0f);
                } else if (occurrence_page(text) == 2 && !second) {
                    second = true;
                    EXPECT_FLOAT_EQ(text->rect.y - cell->rect.y, (clone ? 3.0f : 0.0f) + (i == 1 ? 12.0f : 0.0f));
                }
            }
            EXPECT_EQ(bytes, source_text->length); EXPECT_EQ(second, i < 2);
            state = view_tree_node_state(tree, cells[i], false); ASSERT_NE(state, nullptr);
            size_t boxes = 0;
            for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) if (box->paint_box) {
                boxes++;
                EXPECT_FLOAT_EQ(box->computed_boundary->padding.bottom, clone || boxes == 2 ? 2.0f : 0.0f);
                EXPECT_FLOAT_EQ(box->computed_boundary->padding.top, clone || boxes == 1 ? 2.0f : 0.0f);
            }
            EXPECT_EQ(boxes, 2u);
        }
    }
}

TEST_F(SecondaryViewTest, AlignedTableCellImagesAndNestedBoxesRetainTheirPublishedGeometry) {
    stylesheet("@page { size: 240px 120px; margin: 10px } p, div { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    DomElement* row = block(nullptr, "height: 60px", "tr", table);
    DomElement* cell = block(nullptr, "vertical-align: bottom", "td", row);
    DomElement* nested = block("Nested", "padding: 2px; border: 1px solid black", "div", cell);
    DomElement* img = image("width: 12px; height: 8px; vertical-align: top", nested); ASSERT_NE(img, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* state = view_tree_node_state(tree, img, false); ASSERT_NE(state, nullptr);
    LayoutViewNode* old_image = state->first_occurrence; ASSERT_NE(old_image, nullptr); ASSERT_NE(old_image->image_box, nullptr);
    state = view_tree_node_state(tree, row, false); ASSERT_NE(state, nullptr);
    float row_y = state->first_occurrence->rect.y;
    EXPECT_FLOAT_EQ(old_image->rect.y, row_y + 42.0f);
    EXPECT_FLOAT_EQ(old_image->image_box->content_rect.y, old_image->rect.y);
    EXPECT_FLOAT_EQ(old_image->image_box->image_rect.y, old_image->rect.y);
    state = view_tree_node_state(tree, nested, false); ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.y, row_y + 39.0f);
    ViewPreviewOptions preview = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    tree->reset_retained(); ASSERT_TRUE(cell->set_attribute("style", "vertical-align: middle"));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    state = view_tree_node_state(tree, img, false); ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.y, row_y + 24.0f);
    EXPECT_FLOAT_EQ(old_image->rect.y, row_y + 42.0f);
    EXPECT_FLOAT_EQ(old_image->image_box->content_rect.y, row_y + 42.0f);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_FLOAT_EQ(old_image->image_box->image_rect.y, row_y + 42.0f);
}

TEST_F(SecondaryViewTest, TableBaselineAlignmentGrowsTheRowAndMapsNonCellValuesToBaseline) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 1px 2px 30px; border: 1px solid black }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    DomElement* row = block(nullptr, nullptr, "tr", table);
    DomElement* cell = block("Small", nullptr, "td", row);
    DomElement* large = block("Large", "vertical-align: baseline; font: 20px/24px Arial; padding: 10px 2px 1px", "td", row);
    ASSERT_NE(large, nullptr); ASSERT_TRUE(cell->set_attribute("colspan", "2"));
    for (const char* value : {"baseline", "sub", "super", "text-top", "text-bottom", "10px", "20%", "initial"}) {
        SCOPED_TRACE(value);
        StrBuf* css = strbuf_new(); strbuf_append_format(css, "vertical-align: %s", value);
        ASSERT_TRUE(cell->set_attribute("style", css->str)); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 1u);
        LayoutViewNode* small_text = view_tree_node_state(tree, cell->first_child, false)->first_occurrence;
        LayoutViewNode* large_text = view_tree_node_state(tree, large->first_child, false)->first_occurrence;
        ASSERT_NE(small_text->glyph_run, nullptr); ASSERT_NE(large_text->glyph_run, nullptr);
        EXPECT_FLOAT_EQ(small_text->glyph_run->baseline_y, large_text->glyph_run->baseline_y);
        LayoutViewNode* small_box = view_tree_node_state(tree, cell, false)->first_occurrence;
        LayoutViewNode* large_box = view_tree_node_state(tree, large, false)->first_occurrence;
        EXPECT_FLOAT_EQ(small_box->rect.y, large_box->rect.y);
        EXPECT_FLOAT_EQ(small_box->rect.height, large_box->rect.height);
        EXPECT_FLOAT_EQ(large_text->rect.y - large_box->rect.y, 11.0f);
        EXPECT_GT(small_text->rect.y - small_box->rect.y, 2.0f);
        EXPECT_FLOAT_EQ(small_box->rect.y + small_box->rect.height, small_text->rect.y + 12.0f + 31.0f);
        EXPECT_GT(small_box->rect.height, 45.0f);
    }
}

TEST_F(SecondaryViewTest, TableBaselineUsesFirstNestedLineOrTheEmptyCellContentEdge) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p, div { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: baseline; padding: 2px 2px 9px; border: 1px solid black }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    DomElement* row = block(nullptr, nullptr, "tr", table);
    DomElement* empty = block(nullptr, "height: 30px", "td", row);
    DomElement* nested_cell = block("  ", nullptr, "td", row);
    DomElement* nested = block("Nested", "margin-top: 5px; padding: 2px; border: 1px solid black", "div", nested_cell);
    ASSERT_NE(block("Later", "font: 20px/24px Arial", "div", nested_cell), nullptr);
    DomElement* image_cell = block(nullptr, nullptr, "td", row);
    DomElement* img = image("width: 12px; height: 18px; margin-bottom: 2px", image_cell); ASSERT_NE(img, nullptr);
    for (const char* algorithm : {"fixed", "auto"}) {
        StrBuf* css = strbuf_new(); strbuf_append_format(css, "table-layout: %s; width: 100%%; border-spacing: 0", algorithm);
        ASSERT_TRUE(table->set_attribute("style", css->str)); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        LayoutViewNode* empty_box = view_tree_node_state(tree, empty, false)->first_occurrence;
        LayoutViewNode* text = view_tree_node_state(tree, nested->first_child, false)->first_occurrence;
        LayoutViewNode* image_box = view_tree_node_state(tree, img, false)->first_occurrence;
        ASSERT_NE(text->glyph_run, nullptr); ASSERT_NE(image_box->image_box, nullptr);
        EXPECT_FLOAT_EQ(text->glyph_run->baseline_y, empty_box->rect.y + 33.0f);
        EXPECT_FLOAT_EQ(image_box->rect.y + image_box->rect.height + 2.0f, text->glyph_run->baseline_y);
        EXPECT_FLOAT_EQ(empty_box->computed_boundary->padding.top, 2.0f);
        EXPECT_FLOAT_EQ(empty_box->computed_boundary->padding.bottom, 9.0f);
    }
}

TEST_F(SecondaryViewTest, TableCellMarginsDoNotDisplaceAlignedCellsOrFollowingRows) {
    stylesheet("@page { size: 300px 180px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black } tr { height: 60px }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 4px 2px", "table");
    const char* alignments[] = {"top", "middle", "bottom", "baseline"};
    for (size_t i = 0; i < 2; i++) {
        DomElement* row = block(nullptr, nullptr, "tr", table);
        for (const char* alignment : alignments) {
            StrBuf* css = strbuf_new(); strbuf_append_format(css, "vertical-align: %s; margin: %s", alignment,
                i ? "-15px -20px -30px" : "20px 30px 40px");
            ASSERT_NE(block("X", css->str, "td", row), nullptr); strbuf_free(css);
        }
    }
    for (const char* algorithm : {"fixed", "auto"}) {
        StrBuf* css = strbuf_new(); strbuf_append_format(css, "table-layout: %s; width: 100%%; border-spacing: 4px 2px", algorithm);
        ASSERT_TRUE(table->set_attribute("style", css->str)); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 1u); float previous_y = 0.0f;
        for (DomNode* row = table->first_child; row; row = row->next_sibling) {
            LayoutViewNode* row_box = view_tree_node_state(tree, row, false)->first_occurrence;
            if (previous_y) EXPECT_FLOAT_EQ(row_box->rect.y - previous_y, 62.0f);
            previous_y = row_box->rect.y; size_t i = 0; const float offsets[] = {3.0f, 24.0f, 45.0f, 3.0f};
            for (DomNode* cell = row->as_element()->first_child; cell; cell = cell->next_sibling, i++) {
                LayoutViewNode* box = view_tree_node_state(tree, cell, false)->first_occurrence;
                LayoutViewNode* text = view_tree_node_state(tree, cell->as_element()->first_child, false)->first_occurrence;
                EXPECT_FLOAT_EQ(box->rect.y, row_box->rect.y); EXPECT_FLOAT_EQ(box->rect.height, 60.0f);
                EXPECT_FLOAT_EQ(text->rect.y - row_box->rect.y, offsets[i]);
                EXPECT_GE(box->rect.x, row_box->rect.x);
                EXPECT_LE(box->rect.x + box->rect.width, row_box->rect.x + row_box->rect.width);
            }
        }
    }
}

TEST_F(SecondaryViewTest, RepeatedTableFurnitureRetainsItsSharedRowBaseline) {
    stylesheet("@page { size: 240px 160px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(7, &header, &footer); ASSERT_NE(table, nullptr);
    for (DomElement* group : {header, footer}) {
        DomElement* row = group->first_child->as_element();
        ASSERT_TRUE(row->first_child->as_element()->set_attribute("style", "vertical-align: baseline; padding-bottom: 14px"));
        ASSERT_TRUE(row->last_child->as_element()->set_attribute("style", "vertical-align: baseline; padding-top: 14px"));
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_GT(tree->model->page_count, 1u);
    for (DomElement* group : {header, footer}) {
        DomElement* row = group->first_child->as_element(); float baselines[16] = {};
        ASSERT_LE(tree->model->page_count, 16u);
        for (DomNode* cell = row->first_child; cell; cell = cell->next_sibling) {
            ViewNodeState* state = view_tree_node_state(tree, cell->as_element()->first_child, false); ASSERT_NE(state, nullptr);
            for (LayoutViewNode* text = state->first_occurrence; text; text = text->next_occurrence) if (text->glyph_run) {
                uint32_t page = occurrence_page(text); ASSERT_GE(page, 1u); ASSERT_LE(page, tree->model->page_count);
                if (cell == row->first_child) baselines[page - 1] = text->glyph_run->baseline_y;
                else EXPECT_FLOAT_EQ(text->glyph_run->baseline_y, baselines[page - 1]);
                EXPECT_EQ(text->role, page == 1 ? VIEW_FRAGMENT_BODY : VIEW_FRAGMENT_REPEATED_TABLE);
                EXPECT_FLOAT_EQ(text->parent->parent->rect.height, 42.0f);
            }
        }
    }
}

TEST_F(SecondaryViewTest, TableBaselineUsesTheLineStrutRatherThanTheFirstSuperscriptGlyph) {
    stylesheet("@page { size: 240px 120px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { vertical-align: baseline; padding: 2px; border: 1px solid black }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    DomElement* row = block(nullptr, nullptr, "tr", table);
    DomElement* cell = block(nullptr, nullptr, "td", row);
    DomElement* superscript = block("Up", "vertical-align: super; font-size: 6px", "span", cell);
    DomElement* normal = block("Base", "vertical-align: baseline", "span", cell);
    DomElement* peer = block("Peer", "padding-top: 14px", "td", row); ASSERT_NE(peer, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* up = view_tree_node_state(tree, superscript->first_child, false)->first_occurrence;
    LayoutViewNode* base = view_tree_node_state(tree, normal->first_child, false)->first_occurrence;
    LayoutViewNode* other = view_tree_node_state(tree, peer->first_child, false)->first_occurrence;
    ASSERT_NE(up->glyph_run, nullptr); ASSERT_NE(base->glyph_run, nullptr); ASSERT_NE(other->glyph_run, nullptr);
    EXPECT_LT(up->glyph_run->baseline_y, base->glyph_run->baseline_y);
    EXPECT_FLOAT_EQ(base->glyph_run->baseline_y, other->glyph_run->baseline_y);
}

TEST_F(SecondaryViewTest, SplitTableBaselinesReserveSpaceBeforeConsumingIndependentCellLines) {
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; border: 1px solid black; vertical-align: baseline; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    DomElement* row = block(nullptr, nullptr, "tr", table);
    DomElement* cells[] = {block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", nullptr, "td", row),
        block("K\nL\nM\nN\nO\nP\nQ\nR", "padding-top: 14px", "td", row), block("Z", nullptr, "td", row)};
    ASSERT_NE(cells[2], nullptr); ASSERT_TRUE(cells[0]->set_attribute("colspan", "2"));
    for (bool clone : {false, true}) {
        for (size_t i = 0; i < 3; i++) {
            StrBuf* css = strbuf_new(); strbuf_append_format(css, "padding-top: %upx; box-decoration-break: %s",
                i == 1 ? 14u : 2u, clone ? "clone" : "slice");
            ASSERT_TRUE(cells[i]->set_attribute("style", css->str)); strbuf_free(css);
        }
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 2u);
        float first_baselines[2] = {};
        for (size_t i = 0; i < 3; i++) {
            DomText* content = cells[i]->first_child->as_text(); ViewNodeState* state = view_tree_node_state(tree, content, false);
            ASSERT_NE(state, nullptr); size_t bytes = 0, lines[2] = {};
            for (LayoutViewNode* text = state->first_occurrence; text; text = text->next_occurrence) {
                EXPECT_EQ(text->text_start, bytes); bytes += text->text_length;
                if (!text->glyph_run) continue;
                uint32_t page = occurrence_page(text); ASSERT_LE(page, 2u); ASSERT_GE(page, 1u);
                if (!lines[page - 1]++) {
                    if (!i) first_baselines[page - 1] = text->glyph_run->baseline_y;
                    else EXPECT_FLOAT_EQ(text->glyph_run->baseline_y, first_baselines[page - 1]);
                    EXPECT_FLOAT_EQ(text->rect.y - text->parent->parent->rect.y,
                        page == 1 || clone ? 15.0f : 0.0f);
                }
                LayoutViewNode* cell = text->parent->parent;
                EXPECT_LE(text->rect.y + text->rect.height, cell->rect.y + cell->rect.height);
            }
            EXPECT_EQ(bytes, content->length);
            EXPECT_EQ(lines[0], i == 2 ? 1u : 5u);
            EXPECT_EQ(lines[1], i == 0 ? 5u : i == 1 ? 3u : 0u);
        }
    }
}

TEST_F(SecondaryViewTest, BaselineCellGeometrySurvivesFailedRecompositionAndChangedPageWidths) {
    stylesheet("@page { size: 240px 100px; margin: 10px } @page :left { size: 200px 100px } "
        "p { margin: 0; font: 10px/12px Arial } td { padding: 2px; border: 1px solid black; "
        "vertical-align: baseline; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    DomElement* row = block(nullptr, nullptr, "tr", table);
    DomElement* a = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", nullptr, "td", row);
    ASSERT_NE(block("K\nL\nM\nN\nO\nP\nQ\nR", "padding-top: 14px", "td", row), nullptr);
    DomElement* image_cell = block(nullptr, nullptr, "td", row);
    DomElement* img = image("width: 12px; height: 8px", image_cell); ASSERT_NE(img, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* old_text = view_tree_node_state(tree, a->first_child, false)->first_occurrence;
    LayoutViewNode* old_image = view_tree_node_state(tree, img, false)->first_occurrence;
    ASSERT_NE(old_text->glyph_run, nullptr); ASSERT_NE(old_image->image_box, nullptr);
    float old_baseline = old_text->glyph_run->baseline_y, old_image_y = old_image->rect.y;
    ViewPreviewOptions preview = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    tree->reset_retained(); ASSERT_TRUE(a->set_attribute("style", "padding-top: 26px"));
    options.max_pages = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
    tree->reset_retained();
    options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* current = view_tree_node_state(tree, a->first_child, false)->first_occurrence;
    ASSERT_NE(current->glyph_run, nullptr); EXPECT_FLOAT_EQ(current->glyph_run->baseline_y, old_baseline + 12.0f);
    size_t bytes = 0; float widths[2] = {};
    for (LayoutViewNode* text = current; text; text = text->next_occurrence) {
        EXPECT_EQ(text->text_start, bytes); bytes += text->text_length;
        uint32_t page = occurrence_page(text); ASSERT_GE(page, 1u); ASSERT_LE(page, 2u);
        widths[page - 1] = text->parent->parent->rect.width;
    }
    EXPECT_EQ(bytes, a->first_child->as_text()->length); EXPECT_GT(widths[0], widths[1]);
    EXPECT_FLOAT_EQ(old_text->glyph_run->baseline_y, old_baseline);
    EXPECT_FLOAT_EQ(old_image->image_box->content_rect.y, old_image_y);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_FLOAT_EQ(old_image->image_box->image_rect.y, old_image_y);
}

TEST_F(SecondaryViewTest, UnsupportedRepeatedGroupBreaksAndGroupHeightsAreDiagnosed) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* header = nullptr; DomElement* footer = nullptr; DomElement* table = fixed_table(1, &header, &footer);
    ASSERT_NE(table, nullptr);
    for (DomElement* group : {header, footer, table->last_child->as_element()}) {
        for (const char* policy : {"height: 40px", "break-before: page", "break-after: right"}) {
            if (group != header && group != footer && strcmp(policy, "height: 40px")) continue;
            ASSERT_TRUE(group->set_attribute("style", policy));
            ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
            EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
            EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
            ASSERT_NE(diagnostic.reason, nullptr); ASSERT_TRUE(group->set_attribute("style", ""));
        }
    }
}

TEST_F(SecondaryViewTest, PagedColumnWidthsOverrideFirstRowCellsAndFollowEachPageWidth) {
    stylesheet("@page { size: 240px 140px; margin: 10px } @page :left { size: 200px 140px } "
        "p { margin: 0; font: 10px/12px Arial } td { padding: 2px; border: 1px solid black }");
    DomElement* header = nullptr; DomElement* footer = nullptr;
    DomElement* table = fixed_table(9, &header, &footer); ASSERT_NE(table, nullptr);
    DomElement* group = block(nullptr, "width: 30px; background: yellow", "colgroup", table);
    DomElement* first = block(nullptr, "width: 25%; margin: 100px; padding: 33px; border: 8px solid red; height: 500px", "col", group);
    DomElement* second = block(nullptr, nullptr, "col", group); ASSERT_NE(second, nullptr);
    ASSERT_TRUE(group->set_attribute("span", "999"));
    DomElement* cell = header->first_child->as_element()->first_child->as_element();
    ASSERT_TRUE(cell->set_attribute("style", "width: 140px"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    const float widths[] = {55.0f, 45.0f, 55.0f};
    for (DomElement* source_column : {group, first, second}) {
        ViewNodeState* state = view_tree_node_state(tree, source_column, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(state->occurrence_count, 3u);
        for (LayoutViewNode* column = state->first_occurrence; column; column = column->next_occurrence) {
            size_t page = occurrence_page(column); ASSERT_GE(page, 1u); ASSERT_LE(page, 3u);
            EXPECT_FLOAT_EQ(column->rect.width, widths[page - 1] * (source_column == group ? 4.0f : source_column == first ? 1.0f : 3.0f));
            EXPECT_FLOAT_EQ(column->rect.x, source_column == second ? 10.0f + widths[page - 1] : 10.0f);
            EXPECT_EQ(column->first_fragment, page == 1); EXPECT_EQ(column->last_fragment, page == 3);
            EXPECT_EQ(column->parent->source.address, source_column == group ? table : group);
            EXPECT_GT(column->rect.height, 0.0f); EXPECT_LE(column->rect.y + column->rect.height, 130.0f);
        }
    }
    size_t page = 0;
    for (LayoutViewNode* occurrence = view_tree_node_state(tree, cell, false)->first_occurrence; occurrence; occurrence = occurrence->next_occurrence)
        if (occurrence->paint_box) { ASSERT_LT(page, 3u); EXPECT_FLOAT_EQ(occurrence->rect.width, widths[page++]); }
    EXPECT_EQ(page, 3u);
    EXPECT_EQ(first->blk.get(), nullptr); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, PagedColumnSpansAndAutomaticGroupWidthsConstrainIndividualTracks) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { height: 20px; padding: 0 }");
    for (bool children : {false, true}) {
        DomElement* table = block(nullptr, "table-layout: auto; width: 100%; border-spacing: 0", "table");
        DomElement* group = block(nullptr, "width: 40px", "colgroup", table); ASSERT_NE(group, nullptr);
        ASSERT_TRUE(group->set_attribute("span", "2"));
        if (children) {
            ASSERT_NE(block(nullptr, "width: 60px", "col", group), nullptr);
            ASSERT_NE(block(nullptr, nullptr, "col", group), nullptr);
            ASSERT_NE(block("ignored", "display: grid", "div", group), nullptr);
        }
        ASSERT_NE(block(nullptr, nullptr, "col", table), nullptr);
        DomElement* row = block(nullptr, nullptr, "tr", table);
        DomElement* cells[] = {block(nullptr, nullptr, "td", row), block(nullptr, nullptr, "td", row), block(nullptr, nullptr, "td", row)};
        ASSERT_NE(cells[2], nullptr);
        for (const char* algorithm : {"auto", "fixed"}) {
            StrBuf* css = strbuf_new(); strbuf_append_format(css, "table-layout: %s; width: 100%%; border-spacing: 0", algorithm);
            ASSERT_TRUE(table->set_attribute("style", css->str)); strbuf_free(css);
            ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
            ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
            const float expected[] = {children ? 60.0f : 40.0f, children && strcmp(algorithm, "fixed") == 0 ? 80.0f : 40.0f,
                children ? strcmp(algorithm, "fixed") == 0 ? 80.0f : 120.0f : 140.0f};
            for (size_t i = 0; i < 3; i++) EXPECT_NEAR(source_fragment(tree, cells[i], VIEW_FRAGMENT_BODY, true)->rect.width, expected[i], .002f);
        }
        ASSERT_TRUE(source->DomNode::remove_child(table));
    }
}

TEST_F(SecondaryViewTest, PagedColumnsSurviveSplitCellRollbackAndRetainedPageRelease) {
    init_vector_engine();
    stylesheet("@page { size: 240px 100px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
        "td { padding: 2px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 4px 2px; background: magenta", "table");
    DomElement* group = block(nullptr, "background: yellow", "colgroup", table);
    DomElement* column = block(nullptr, "width: 30px; background: red", "col", group);
    ASSERT_TRUE(column->set_attribute("span", "2"));
    ASSERT_NE(block(nullptr, "background: blue", "col", group), nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table);
    DomElement* first = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", nullptr, "td", row); ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->set_attribute("colspan", "2"));
    ASSERT_NE(block("Z", nullptr, "td", row), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    options.max_pages = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
    tree->reset_retained(); options.max_pages = 10;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    size_t bytes = 0;
    for (LayoutViewNode* text = view_tree_node_state(tree, first->first_child, false)->first_occurrence; text; text = text->next_occurrence) {
        EXPECT_EQ(text->text_start, bytes); bytes += text->text_length;
    }
    EXPECT_EQ(bytes, 19u);
    ViewNodeState* state = view_tree_node_state(tree, column, false); ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->occurrence_count, 2u);
    for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) EXPECT_FLOAT_EQ(box->rect.width, 64.0f);
    ViewPreviewOptions preview = view_preview_options_default(); preview.columns = 2;
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ImageSurface* before = render_secondary_view_snapshot(retained); ASSERT_NE(before, nullptr);
    tree->reset_retained(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* after = render_secondary_view_snapshot(retained); ASSERT_NE(after, nullptr);
    EXPECT_EQ(before->width, after->width); EXPECT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    image_surface_destroy(before); image_surface_destroy(after);
}

TEST_F(SecondaryViewTest, PagedExplicitColumnsLeaveOnlyUnsetSpanTracksForFirstRowWidths) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { padding: 0; height: 20px }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0", "table");
    ASSERT_NE(block(nullptr, "width: 60px", "col", table), nullptr);
    DomElement* first = block(nullptr, nullptr, "tr", table);
    DomElement* spanning = block(nullptr, "width: 100px", "td", first); ASSERT_NE(spanning, nullptr);
    ASSERT_TRUE(spanning->set_attribute("colspan", "2")); ASSERT_NE(block(nullptr, nullptr, "td", first), nullptr);
    DomElement* next = block(nullptr, nullptr, "tr", table);
    DomElement* cells[] = {block(nullptr, nullptr, "td", next), block(nullptr, nullptr, "td", next), block(nullptr, nullptr, "td", next)};
    ASSERT_NE(cells[2], nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const float widths[] = {60.0f, 50.0f, 110.0f};
    for (size_t i = 0; i < 3; i++) EXPECT_FLOAT_EQ(source_fragment(tree, cells[i], VIEW_FRAGMENT_BODY, true)->rect.width, widths[i]);
    EXPECT_FLOAT_EQ(source_fragment(tree, spanning, VIEW_FRAGMENT_BODY, true)->rect.width, 110.0f);
    ASSERT_TRUE(table->set_attribute("style", "table-layout: auto; width: 100%; border-spacing: 0"));
    ASSERT_TRUE(table->first_child->as_element()->set_attribute("style", "width: 25%"));
    ASSERT_TRUE(spanning->remove_attribute("style"));
    tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(source_fragment(tree, cells[0], VIEW_FRAGMENT_BODY, true)->rect.width, 55.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, cells[1], VIEW_FRAGMENT_BODY, true)->rect.width, 82.5f);
    EXPECT_FLOAT_EQ(source_fragment(tree, cells[2], VIEW_FRAGMENT_BODY, true)->rect.width, 82.5f);
}

TEST_F(SecondaryViewTest, PagedSubpixelColumnsKeepSpanningBackgroundsByGridIdentity) {
    init_vector_engine();
    stylesheet("@page { size: 120px 80px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { padding: 0; height: 20px }");
    DomElement* table = block(nullptr, "table-layout: fixed; width: 100%; border-spacing: 0; background: yellow", "table");
    DomElement* column = block(nullptr, "width: .005px; background: red", "col", table);
    ASSERT_NE(block(nullptr, nullptr, "col", table), nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table);
    DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cell->set_attribute("colspan", "2"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    LayoutViewNode* box = source_fragment(tree, cell, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    ASSERT_NE(box->table_range, nullptr); EXPECT_EQ(box->table_range->column, 0u); EXPECT_EQ(box->table_range->span, 2u);
    EXPECT_NEAR(view_tree_node_state(tree, column, false)->first_occurrence->rect.width, .005f, .00001f);
    ViewPreviewOptions preview = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    // these low-level writes have no host epoch notification; painting must use the already composed grid.
    ASSERT_TRUE(cell->set_attribute("colspan", "1"));
    ASSERT_TRUE(column->set_attribute("span", "2"));
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* surface = render_secondary_view_snapshot(retained); ASSERT_NE(surface, nullptr);
    Color pixel = {snapshot_pixel(surface, 50, 20)};
    EXPECT_EQ(pixel.r, 255u); EXPECT_EQ(pixel.g, 0u); EXPECT_EQ(pixel.b, 0u);
    image_surface_destroy(surface);
}

TEST_F(SecondaryViewTest, PagedColumnBudgetsAndUnadmittedGridPoliciesRejectIncompleteEditions) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { height: 20px; padding: 0 }");
    DomElement* table = block(nullptr, "table-layout: auto; width: 100%; border-spacing: 0", "table");
    DomElement* column = block(nullptr, nullptr, "col", table);
    DomElement* row = block(nullptr, nullptr, "tr", table);
    ASSERT_NE(block(nullptr, nullptr, "td", row), nullptr); ASSERT_NE(block(nullptr, nullptr, "td", row), nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    for (const char* span : {"0", "-2", "invalid"}) {
        ASSERT_TRUE(column->set_attribute("span", span)); ViewTree* tree = secondary();
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        EXPECT_FLOAT_EQ(view_tree_node_state(tree, column, false)->first_occurrence->rect.width, 110.0f);
    }
    ASSERT_TRUE(column->set_attribute("span", "1001"));
    ViewTree* excess = secondary(); ASSERT_EQ(layout_secondary_view(excess, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* completed = source_fragment(excess, row, VIEW_FRAGMENT_BODY, true); ASSERT_NE(completed, nullptr);
    size_t count = 0;
    for (LayoutViewNode* cell = completed->first_child; cell; cell = cell->next_sibling) count++;
    EXPECT_EQ(count, 1000u);
    options.max_items = 2;
    ViewTree* budget = secondary(); EXPECT_EQ(layout_secondary_view(budget, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "table grid exceeds its track budget"); EXPECT_EQ(budget->model->page_count, 0u);
    options = paged_layout_options_default(); ASSERT_TRUE(column->set_attribute("span", "1"));
    for (const char* css : {"visibility: collapse", "width: calc(10% + 1px)", "position: relative"}) {
        SCOPED_TRACE(css); ASSERT_TRUE(column->set_attribute("style", css)); ViewTree* tree = secondary();
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
    }
    ASSERT_TRUE(table->set_attribute("style", "table-layout: fixed; width: 100%; border-spacing: 0"));
    ASSERT_TRUE(column->set_attribute("style", "width: 0")); ViewTree* zero = secondary();
    EXPECT_EQ(layout_secondary_view(zero, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(zero->model->page_count, 0u); EXPECT_FALSE(zero->model->committed);
}

TEST_F(SecondaryViewTest, UnsupportedTablePoliciesFailBeforePublishingAnEdition) {
    const char* policies[] = {"border-collapse: collapse", "width: 1000px",
        "table-layout: auto; width: min-content"};
    for (const char* policy : policies) {
        SCOPED_TRACE(policy);
        stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } "
            "td { vertical-align: top }");
        DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
        StrBuf* css = strbuf_new(); ASSERT_NE(css, nullptr);
        strbuf_append_str(css, "table-layout: fixed; width: 100%; border-collapse: separate; border-spacing: 0; ");
        strbuf_append_str(css, policy); ASSERT_TRUE(table->set_attribute("style", css->str)); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_NE(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK);
        EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
        ASSERT_TRUE(source->DomNode::remove_child(table));
    }
}

TEST_F(SecondaryViewTest, UnsupportedTableCellAndGroupPoliciesAreDiagnosed) {
    const char* policies[] = {"padding: 5%", "position: relative"};
    for (const char* policy : policies) {
        SCOPED_TRACE(policy);
        stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
        DomElement* header = nullptr; DomElement* table = fixed_table(1, &header); ASSERT_NE(table, nullptr);
        DomElement* cell = header->first_child->as_element()->first_child->as_element();
        ASSERT_TRUE(cell->set_attribute("style", policy));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u); ASSERT_NE(diagnostic.reason, nullptr);
        ASSERT_TRUE(source->DomNode::remove_child(table));
    }
}

TEST_F(SecondaryViewTest, MissingImagesAndUnsupportedContextsAreDiagnosedButHiddenSubtreesAreSkipped) {
    stylesheet("@page { size: 240px 180px; margin: 10px } p { margin: 0 }");
    DomElement* image = block(nullptr, nullptr, "img"); ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->set_attribute("src", "missing-image.png"));
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_STREQ(diagnostic.reason, "image source is unavailable or requires a sampled media producer");
    EXPECT_EQ(diagnostic.source.address, image);
    EXPECT_FALSE(tree->model->committed);
    ASSERT_TRUE(image->set_attribute("style", "display: none"));
    ASSERT_NE(block("hidden grid", "display: grid", "div", image), nullptr);
    tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    DomElement* svg = block(nullptr, nullptr, "svg"); ASSERT_NE(svg, nullptr);
    tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, svg);
}

TEST(PagedCssTest, BreakKeywordsAliasesAndLineMinimaValidateBeforeCascade) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.paged.breaks");
    ASSERT_NE(pool, nullptr);
    const char* valid[] = {"page-break-before: always", "break-inside: avoid-page", "break-before: recto", "break-after: verso", "widows: 3", "position: running(header)", "position: RUNNING(Header)"};
    for (const char* text : valid) EXPECT_NE(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    const char* invalid[] = {"page-break-before: column", "page-break-after: recto", "break-inside: page", "break-before: red", "orphans: 0", "widows: 2.5",
        "position: running()", "position: running(123)", "position: running(\"header\")", "position: running(inherit)"};
    for (const char* text : invalid) EXPECT_EQ(css_parse_declaration_text(text, strlen(text), pool), nullptr) << text;
    for (const CssEnum id : {CSS_VALUE_ALWAYS, CSS_VALUE_AVOID_PAGE, CSS_VALUE_RECTO, CSS_VALUE__REPLACED})
        EXPECT_EQ(css_enum_by_name(css_enum_info(id)->name), id);
    mem_pool_destroy(pool);
}

TEST(PagedCssTest, SharedGeneratedContentKeepsLongStringsAndBalancedQuotes) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.paged.content");
    ASSERT_NE(pool, nullptr);
    StrBuf* declaration = strbuf_create("content: open-quote \"");
    strbuf_append_char_n(declaration, 'A', 1024);
    strbuf_append_str(declaration, "\" close-quote");
    CssDeclaration* parsed = css_parse_declaration_text(declaration->str, declaration->length, pool);
    ASSERT_NE(parsed, nullptr);
    CssContentBindings bindings = {};
    bindings.quote = [](void*, bool open, int depth) { return css_content_quote_char(nullptr, open, depth); };
    StrBuf* text = strbuf_new();
    int depth = 0;
    ASSERT_TRUE(css_content_append(parsed->value, &bindings, &depth, text));
    EXPECT_EQ(text->length, 1030u);
    EXPECT_EQ(depth, 0);
    EXPECT_EQ(memcmp(text->str + 3, declaration->str + strlen("content: open-quote \""), 1024), 0);
    strbuf_free(declaration); strbuf_free(text); mem_pool_destroy(pool);
}

static uint32_t occurrence_page(const LayoutViewNode* node) {
    while (node && node->kind != LAYOUT_VIEW_PAGE) node = node->parent;
    return node ? ((const ViewPageBox*)node)->page_number : 0;
}

TEST(TypesetTest, TargetBindingsKeepProducerPayloadsAndCaseSensitiveIdentities) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.targets"); ASSERT_NE(pool, nullptr);
    struct NativeTarget : TypesetRecord { uint64_t scaled_points; } payload = {{71}, UINT64_C(9007199254740993)};
    TypesetTargetStore store = {71, 5, pool, nullptr, 0, 0, 4, nullptr};
    char name[] = "Red";
    TypesetTarget target = {name, {71, 5, 27, TYPESET_PROVIDER_OFFSETS, nullptr}, 10, lam::up((const TypesetRecord*)&payload)};
    ASSERT_EQ(typeset_target_append(&store, &target), TYPESET_OK);
    name[0] = 'r'; target.page_number = 11;
    ASSERT_EQ(typeset_target_append(&store, &target), TYPESET_OK);
    ASSERT_NE(typeset_target_find(&store, "Red"), nullptr); ASSERT_NE(typeset_target_find(&store, "red"), nullptr);
    EXPECT_EQ(typeset_target_find(&store, "Red")->page_number, 10u);
    EXPECT_EQ(typeset_target_find(&store, "red")->page_number, 11u);
    EXPECT_EQ(typeset_target_find(&store, "Red")->value.get(), &payload);
    EXPECT_EQ(payload.scaled_points, UINT64_C(9007199254740993));
    EXPECT_EQ(typeset_target_append(&store, &target), TYPESET_INVALID);
    EXPECT_EQ(typeset_target_find(&store, "missing"), nullptr);
    target.name = "next"; target.source.generation++;
    EXPECT_EQ(typeset_target_append(&store, &target), TYPESET_STALE);
    typeset_targets_dispose(&store); mem_pool_destroy(pool);
}

TEST(TypesetTest, PublishingStoresAdmitOnlyRegisteredProducerNamespacesAndPreserveTheirRecords) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.namespaces"); ASSERT_NE(pool, nullptr);
    TypesetSourceNamespace namespaces[] = {{71, 5}, {72, 6}};
    TypesetSourceScope scope = {namespaces, 2};
    TypesetMarkStore marks = {41, 7, pool, nullptr, 0, 0, scope};
    TypesetTargetStore targets = {41, 7, pool, nullptr, 0, 0, 4, nullptr, scope};
    NativeRegionFixture exact = {{71}, UINT64_C(9007199254740993)};
    TypesetSource source = {71, 5, 27, TYPESET_PROVIDER_OFFSETS, lam::up((const TypesetRecord*)&exact)};
    TypesetMark mark = {TYPESET_MARK_NATIVE, "chapter", source, nullptr, source.native, 1, true};
    TypesetTarget target = {"anchor", source, 1, source.native};
    ASSERT_EQ(typeset_mark_append(&marks, &mark), TYPESET_OK);
    ASSERT_EQ(typeset_target_append(&targets, &target), TYPESET_OK);
    EXPECT_EQ(typeset_mark_select(&marks, TYPESET_MARK_NATIVE, "chapter", 1, TYPESET_MARK_FIRST)->value.get(), &exact);
    EXPECT_EQ(typeset_target_find(&targets, "anchor")->source.native.get(), &exact);
    mark.source.generation++; target.source.generation++;
    EXPECT_EQ(typeset_mark_append(&marks, &mark), TYPESET_STALE);
    EXPECT_EQ(typeset_target_append(&targets, &target), TYPESET_STALE);
    mark.source = target.source = {41, 7, 27, TYPESET_PROVIDER_OFFSETS, nullptr}; target.name = "document";
    ASSERT_EQ(typeset_mark_append(&marks, &mark), TYPESET_OK);
    ASSERT_EQ(typeset_target_append(&targets, &target), TYPESET_OK);
    EXPECT_FALSE(typeset_source_same_identity(source, target.source));
    typeset_targets_dispose(&targets); mem_pool_destroy(pool);
}

TEST(TypesetTest, ReferenceConvergenceCopiesExactSignaturesAndRejectsCyclesAndBudgets) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_LAYOUT, "test.typeset.convergence"); ASSERT_NE(pool, nullptr);
    TypesetConvergence state = {}; state.pool = pool; state.limit = 4;
    char signature[] = {'A', '\0', '1'}; bool settled = true;
    ASSERT_EQ(typeset_convergence_observe(&state, signature, sizeof(signature), &settled), TYPESET_OK);
    EXPECT_FALSE(settled); signature[2] = '2';
    ASSERT_EQ(typeset_convergence_observe(&state, signature, sizeof(signature), &settled), TYPESET_OK);
    EXPECT_FALSE(settled); signature[2] = '1';
    EXPECT_EQ(typeset_convergence_observe(&state, signature, sizeof(signature), &settled), TYPESET_NO_PROGRESS);
    EXPECT_FALSE(settled); signature[2] = '2';
    EXPECT_EQ(typeset_convergence_observe(&state, signature, sizeof(signature), &settled), TYPESET_OK);
    EXPECT_TRUE(settled);
    TypesetConvergence bounded = {}; bounded.pool = pool; bounded.limit = 1;
    EXPECT_EQ(typeset_convergence_observe(&bounded, signature, sizeof(signature), &settled), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_FALSE(settled);
    mem_pool_destroy(pool);
}

static void append_fragment_text(const LayoutViewNode* node, StrBuf* text) {
    if (node->glyph_run && node->glyph_run->text)
        strbuf_append_str_n(text, node->glyph_run->text, (size_t)node->glyph_run->text_len);
    else if (node->text_length && node->rect.width > 0.0f) strbuf_append_char(text, ' ');
    for (LayoutViewNode* child = node->first_child; child; child = child->next_sibling) append_fragment_text(child, text);
}

static LayoutViewNode* source_fragment(ViewTree* tree, DomNode* source, ViewFragmentRole role, bool box, bool last) {
    ViewNodeState* state = view_tree_node_state(tree, source, false);
    if (!state) return nullptr;
    LayoutViewNode* result = nullptr;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (fragment->role == role && fragment->paint_box == box) {
            result = fragment;
            if (!last) break;
        }
    return result;
}

static LayoutViewNode* source_glyph_text(ViewTree* tree, DomNode* source, const char* text) {
    ViewNodeState* state = view_tree_node_state(tree, source, false);
    if (!state) return nullptr;
    size_t length = strlen(text);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence)
        if (node->glyph_run && (size_t)node->glyph_run->text_len == length && !memcmp(node->glyph_run->text, text, length)) return node;
    return nullptr;
}

TEST_F(SecondaryViewTest, PublishingCorpusCombinesImagesNotesFloatsReferencesAndRetainedPreviews) {
    stylesheet("@page { size: 240px 140px; margin: 20px; @top-center { content: string(Title); font-size: 7px } "
        "@bottom-center { content: counter(page) '/' counter(pages); font-size: 7px } } "
        "p, div, h2 { margin: 0; font: 10px/12px Arial; orphans: 1; widows: 1 } "
        "h2 { string-set: Title content(text) } a::after { content: ' on ' target-counter(attr(href), page) }");
    const char* names[] = {"One", "Two", "Three"};
    const char* blue = "data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='40'%20height='20'%3E%3Crect%20width='40'%20height='20'%20fill='blue'/%3E%3C/svg%3E";
    DomElement* notes[3] = {}, *images[3] = {};
    for (size_t i = 0; i < 3; i++) {
        DomElement* section = block(nullptr, i ? "break-before: page" : nullptr); ASSERT_NE(section, nullptr);
        DomElement* title = block(names[i], nullptr, "h2", section); ASSERT_NE(title, nullptr);
        if (i == 2) ASSERT_TRUE(title->set_attribute("id", "Chapter Δ"));
        DomElement* figure = block(nullptr, "float: top; float-reference: page; height: 14px", "div", section);
        ASSERT_NE(figure, nullptr);
        DomElement* picture = block(nullptr, "display: block", "picture", figure); ASSERT_NE(picture, nullptr);
        DomElement* candidate = block(nullptr, nullptr, "source", picture); ASSERT_NE(candidate, nullptr);
        ASSERT_TRUE(candidate->set_attribute("media", "print"));
        StrBuf* srcset = strbuf_new(); ASSERT_NE(srcset, nullptr);
        strbuf_append_str(srcset, blue); strbuf_append_str(srcset, " 2x");
        ASSERT_TRUE(candidate->set_attribute("srcset", srcset->str)); strbuf_free(srcset);
        images[i] = image("display: block", picture, "missing-fallback.png"); ASSERT_NE(images[i], nullptr);
        DomElement* body = block("Body ", nullptr, "div", section); ASSERT_NE(body, nullptr);
        notes[i] = block("Note", "float: footnote", "span", body); ASSERT_NE(notes[i], nullptr);
        if (!i) {
            DomElement* reference = block("Final chapter", nullptr, "a", body); ASSERT_NE(reference, nullptr);
            ASSERT_TRUE(reference->set_attribute("href", "#Chapter%20%CE%94"));
        }
    }
    ASSERT_TRUE(create_dir("temp/paged-media-combined"));
    EXPECT_TRUE(mem_context_dump_json_file((MemContext*)doc.services.mem_ctx, "temp/paged-media-combined/source-memory.json"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_GT(diagnostic.reference_passes, 1u);
    const TypesetTarget* target = layout_secondary_target(tree, "Chapter Δ"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->page_number, 3u);
    for (size_t i = 0; i < 3; i++) {
        LayoutViewNode* image = source_image_fragment(tree, images[i]); ASSERT_NE(image, nullptr);
        EXPECT_EQ(occurrence_page(image), i + 1); EXPECT_FLOAT_EQ(image->rect.width, 20.0f);
        EXPECT_FLOAT_EQ(image->rect.height, 10.0f);
        LayoutViewNode* note = source_fragment(tree, notes[i], VIEW_FRAGMENT_NOTE, true); ASSERT_NE(note, nullptr);
        EXPECT_EQ(occurrence_page(note), i + 1);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        append_fragment_text(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER], text);
        EXPECT_STREQ(text->str, names[i]); strbuf_free(text);
    }
    EXPECT_TRUE(mem_context_dump_json_file((MemContext*)doc.services.mem_ctx, "temp/paged-media-combined/composed-memory.json"));
    ViewPreviewOptions preview_options = view_preview_options_default(); preview_options.columns = 3;
    ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_TRUE(mem_context_dump_json_file((MemContext*)doc.services.mem_ctx, "temp/paged-media-combined/retained-memory.json"));
    init_vector_engine();
    ASSERT_TRUE(render_secondary_view_to_pdf(preview, "temp/paged-media-combined/publishing.pdf"));
    ASSERT_TRUE(render_secondary_view_to_png(preview, "temp/paged-media-combined/publishing.png"));
    for (uint32_t page = 1; page <= 3; page++) {
        ImageSurface* snapshot = render_secondary_page_snapshot(preview, page); ASSERT_NE(snapshot, nullptr);
        EXPECT_EQ(snapshot_pixel(snapshot, 25, 25), 0xffff0000u);
        EXPECT_EQ(snapshot_pixel(snapshot, 45, 25), 0xffffffffu); image_surface_destroy(snapshot);
    }
}

TEST_F(SecondaryViewTest, RunningStringsAndNamedPagesKeepKeywordSpellingCase) {
    stylesheet("@page { size: 240px 120px; margin: 10px; @top-center { content: string(Red) '|' string(red); font-size: 7px } } "
        "@page Red { size: 200px 100px } @page red { size: 260px 140px } "
        "p { string-set: Red 'Upper', red 'Lower' } p, div { margin: 0; font-size: 10px; line-height: 12px } ");
    ASSERT_NE(block("First", "page: Red"), nullptr); ASSERT_NE(block("Second", "page: red; break-before: page"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->node.rect.width, 200.0f);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[1]->node.rect.width, 260.0f);
    for (size_t i = 0; i < 2; i++) {
        LayoutViewNode* box = tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER]; ASSERT_NE(box, nullptr);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(box, text);
        EXPECT_STREQ(text->str, "Upper|Lower"); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, SourceCountersAndGeneratedContentAreStableAcrossFragmentsAndRepeatedHeaders) {
    stylesheet("@page { size: 240px 120px; margin: 12px; @top-center { content: string(Title); font-size: 7px } } "
        "p { counter-reset: Chapter 0 } p, div { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 } "
        "div { counter-increment: Chapter; string-set: Title content(before) content(text) } "
        "div::before { content: 'Chapter ' counter(Chapter, upper-roman) ': '; color: red } "
        "div::after { content: ' [' counters(Chapter, '.') ']'; color: blue }");
    DomElement* chapter = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ"); ASSERT_NE(chapter, nullptr);
    DomElement* next = block("Next", "break-before: page"); ASSERT_NE(next, nullptr);
    DomNode* original_child = chapter->first_child;
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_GE(tree->model->page_count, 3u);
    size_t before = 0, after = 0;
    ViewNodeState* state = view_tree_node_state(tree, chapter, false); ASSERT_NE(state, nullptr);
    StrBuf* generated = strbuf_new(); ASSERT_NE(generated, nullptr);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) {
        if (!node->glyph_run || !node->computed_style) continue;
        if (node->computed_style->pseudo_element == PSEUDO_ELEMENT_BEFORE) {
            before++; EXPECT_EQ(node->glyph_run->color.r, 255);
            strbuf_append_str_n(generated, node->glyph_run->text, (size_t)node->glyph_run->text_len);
        } else if (node->computed_style->pseudo_element == PSEUDO_ELEMENT_AFTER) {
            after++; EXPECT_EQ(node->glyph_run->color.b, 255);
        }
    }
    EXPECT_EQ(before, 2u); EXPECT_EQ(after, 1u); EXPECT_STREQ(generated->str, "ChapterI:"); strbuf_free(generated);
    for (size_t i = 0; i < tree->model->page_count; i++) {
        LayoutViewNode* header = tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER]; ASSERT_NE(header, nullptr);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(header, text);
        if (i + 1 < tree->model->page_count) EXPECT_STREQ(text->str, "Chapter I: A B C D E F G H I J");
        else EXPECT_STREQ(text->str, "Chapter II: Next");
        strbuf_free(text);
    }
    EXPECT_EQ(chapter->first_child, original_child); EXPECT_EQ(chapter->last_child, original_child);
    EXPECT_EQ(next->parent, source); EXPECT_FLOAT_EQ(source->height, 91.75f);
    ViewTree* other = secondary();
    ASSERT_EQ(layout_secondary_view(other, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(other->model->page_count, tree->model->page_count);
    for (size_t i = 0; i < tree->model->page_count; i++) {
        StrBuf* left = strbuf_new(); StrBuf* right = strbuf_new(); ASSERT_NE(left, nullptr); ASSERT_NE(right, nullptr);
        append_fragment_text(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER], left);
        append_fragment_text(other->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER], right);
        EXPECT_STREQ(left->str, right->str); strbuf_free(left); strbuf_free(right);
    }
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/body-counters.pdf"));
}

TEST_F(SecondaryViewTest, CounterSetFollowsIncrementAndQuotesSpanIndependentPseudoOccurrences) {
    stylesheet("@page { size: 240px 120px; margin: 10px } "
        "p { counter-reset: Chapter 0; quotes: '<' '>' '[' ']' } p, div, span { margin: 0; font-size: 10px; line-height: 12px } "
        "div { counter-set: Chapter 3; counter-increment: Chapter 2 } "
        "div::before { content: counter(Chapter) open-quote } div::after { content: close-quote } "
        "span::before { content: open-quote } span::after { content: close-quote }");
    DomElement* outer = block("Outer"); ASSERT_NE(outer, nullptr);
    ASSERT_NE(block("Inner", nullptr, "span", outer), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* box = source_fragment(tree, outer, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(box, text);
    EXPECT_STREQ(text->str, "3<Outer[Inner]>"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, StyleContainmentScopesCountersAndQuotesInIndependentViews) {
    stylesheet("@page { size: 400px 240px; margin: 10px } "
        "p { counter-reset: N 7; quotes: 'A' 'Z' '1' '9' } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px } "
        ".contained { contain: style; counter-increment: N } "
        ".contained::before { content: counters(N, '.') open-quote } "
        ".contained::after { counter-increment: N 2; content: counters(N, '.') close-quote } "
        ".quote::before { content: open-quote } .tail::before { content: counter(N) close-quote }");
    DomElement* contained = block("", nullptr, "div"); ASSERT_NE(contained, nullptr);
    ASSERT_TRUE(contained->set_attribute("class", "contained"));
    DomElement* quote = block("", "contain:style", "span", contained); ASSERT_NE(quote, nullptr);
    ASSERT_TRUE(quote->set_attribute("class", "quote"));
    DomElement* tail = block("", nullptr, "div"); ASSERT_NE(tail, nullptr);
    ASSERT_TRUE(tail->set_attribute("class", "tail"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* box = source_fragment(tree, contained, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(box, text);
    EXPECT_STREQ(text->str, "8A18.2Z"); strbuf_free(text);
    EXPECT_STREQ(view_css_resolve_pseudo(tree, tail, PSEUDO_ELEMENT_BEFORE)->generated_text, "8");
}

TEST_F(SecondaryViewTest, CounterResetsReachFollowingSiblingsWithoutEscapingTheirParent) {
    stylesheet("@page { size: 400px 240px; margin: 10px } p { counter-reset: N 0 } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px } .show::before { content: counters(N, '.') ' ' } "
        ".container::after { content: counters(N, '.') }");
    const char* declarations[] = {"counter-reset: N 5", "counter-increment: N", "counter-reset: N 10", "counter-increment: N"};
    const char* expected[] = {"0.5 ", "0.6 ", "0.10 ", "0.11 "};
    DomElement* siblings[4] = {};
    for (size_t i = 0; i < 4; i++) {
        siblings[i] = block("A", declarations[i], "span"); ASSERT_NE(siblings[i], nullptr);
        ASSERT_TRUE(siblings[i]->set_attribute("class", "show"));
    }
    DomElement* parent = block("Parent"); ASSERT_NE(parent, nullptr); ASSERT_TRUE(parent->set_attribute("class", "show container"));
    DomElement* nested = block("Nested", "counter-reset: N 30", "span", parent); ASSERT_NE(nested, nullptr);
    ASSERT_TRUE(nested->set_attribute("class", "show"));
    DomElement* following = block("Following", "counter-increment: N", "span", parent); ASSERT_NE(following, nullptr);
    ASSERT_TRUE(following->set_attribute("class", "show"));
    DomElement* tail = block("Tail", "counter-increment: N", "span"); ASSERT_NE(tail, nullptr); ASSERT_TRUE(tail->set_attribute("class", "show"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    for (size_t i = 0; i < 4; i++) EXPECT_STREQ(view_css_resolve_pseudo(tree, siblings[i], PSEUDO_ELEMENT_BEFORE)->generated_text, expected[i]);
    EXPECT_STREQ(view_css_resolve_pseudo(tree, nested, PSEUDO_ELEMENT_BEFORE)->generated_text, "0.11.30 ");
    EXPECT_STREQ(view_css_resolve_pseudo(tree, following, PSEUDO_ELEMENT_BEFORE)->generated_text, "0.11.31 ");
    EXPECT_STREQ(view_css_resolve_pseudo(tree, parent, PSEUDO_ELEMENT_AFTER)->generated_text, "0.11.31");
    EXPECT_STREQ(view_css_resolve_pseudo(tree, tail, PSEUDO_ELEMENT_BEFORE)->generated_text, "0.12 ");
}

TEST_F(SecondaryViewTest, HiddenStringAssignmentsUseInheritedCountersWithoutIncrementingThem) {
    stylesheet("@page { size: 240px 120px; margin: 12px; @top-left { content: string(Title, first); font-size: 7px } "
        "@top-right { content: string(Title, last); font-size: 7px } } "
        "p { counter-reset: Chapter 3 } p, div, span { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("Hidden", "display: none; counter-increment: Chapter 99; string-set: Title counter(Chapter)", "span"), nullptr);
    ASSERT_NE(block("Body", "counter-increment: Chapter 1; string-set: Title counter(Chapter)"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    const CssPageMarginBox boxes[] = {CSS_PAGE_TOP_LEFT, CSS_PAGE_TOP_RIGHT};
    const char* expected[] = {"3", "4"};
    for (size_t i = 0; i < 2; i++) {
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        LayoutViewNode* box = tree->model->pages.get()[0]->margin_boxes[boxes[i]]; ASSERT_NE(box, nullptr);
        append_fragment_text(box, text); EXPECT_STREQ(text->str, expected[i]); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, BlockPseudosHaveIndependentBoxesAndRetainTheirSourceCounterAcrossPages) {
    stylesheet("@page { size: 200px 60px; margin: 0 } p, div { margin: 0; font-size: 10px; line-height: 12px; "
        "white-space: pre-wrap; orphans: 1; widows: 1 } p { counter-reset: chapter 0 } div { counter-increment: chapter } "
        "div::before { display: block; content: 'Chapter ' counter(chapter); padding: 2px; border: 2px solid blue; color: blue } "
        "div::after { display: block; content: 'End ' counter(chapter); color: red }");
    DomElement* body = block("A\nB\nC\nD\nE", nullptr, "div"); ASSERT_NE(body, nullptr);
    DomNode* original = body->first_child;
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, body, false); ASSERT_NE(state, nullptr);
    size_t before_boxes = 0, after_boxes = 0;
    StrBuf* before = strbuf_new(); StrBuf* after = strbuf_new(); ASSERT_NE(before, nullptr); ASSERT_NE(after, nullptr);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) {
        if (!node->computed_style) continue;
        uint8_t pseudo = node->computed_style->pseudo_element;
        if (node->paint_box && pseudo == PSEUDO_ELEMENT_BEFORE) {
            before_boxes++; EXPECT_EQ(occurrence_page(node), 1u); EXPECT_FLOAT_EQ(node->rect.height, 20.0f);
            ASSERT_NE(node->computed_boundary, nullptr); EXPECT_FLOAT_EQ(node->computed_boundary->padding.left, 2.0f);
            append_fragment_text(node, before);
        } else if (node->paint_box && pseudo == PSEUDO_ELEMENT_AFTER) {
            after_boxes++; EXPECT_EQ(occurrence_page(node), 2u); append_fragment_text(node, after);
        }
    }
    EXPECT_EQ(before_boxes, 1u); EXPECT_EQ(after_boxes, 1u);
    EXPECT_STREQ(before->str, "Chapter 1"); EXPECT_STREQ(after->str, "End 1");
    strbuf_free(before); strbuf_free(after);
    EXPECT_EQ(body->first_child, original); EXPECT_EQ(body->last_child, original);
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/block-pseudos.pdf"));
}

TEST_F(SecondaryViewTest, BodyPageCounterBindingIsDiagnosedBeforePublishingPlaceholderText) {
    stylesheet("@page { size: 240px 120px; margin: 10px } div::before { content: counter(page) }");
    ASSERT_NE(block("Body"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "unresolved body generated-content binding");
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, ForwardAndBackwardTargetsSettleCounterTextAndPageBindingsPerView) {
    stylesheet("@page { size: 240px 120px; margin: 10px } @media (max-width: 200px) { @page { size: 240px 80px } } p { counter-reset: Chapter 0 } "
        "p, div, a { margin: 0; font-size: 10px; line-height: 12px } a { display: block } "
        "div { counter-increment: Chapter } div::before { content: 'Chapter ' counter(Chapter) ': ' } "
        "a::after { content: target-text(attr(href), before) target-text(attr(href)) ' on ' target-counter(attr(href url), page) "
        "' (' target-counters(attr(href), Chapter, '.', upper-roman) ')' }");
    DomElement* link = block("", nullptr, "a"); ASSERT_NE(link, nullptr); ASSERT_TRUE(link->set_attribute("href", "#Red"));
    ASSERT_TRUE(link->set_attribute("id", "toc"));
    DomElement* target = block("Title", "break-before: page; height: 80px"); ASSERT_NE(target, nullptr); ASSERT_TRUE(target->set_attribute("id", "Red"));
    DomElement* back = block("", "break-before: page", "a"); ASSERT_NE(back, nullptr); ASSERT_TRUE(back->set_attribute("href", "#Red"));
    ASSERT_TRUE(back->set_attribute("id", "tail"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_GE(diagnostic.reference_passes, 3u);
    ASSERT_NE(layout_secondary_target(tree, "Red"), nullptr);
    EXPECT_EQ(layout_secondary_target(tree, "Red")->page_number, 2u);
    EXPECT_EQ(layout_secondary_target(tree, "red"), nullptr);
    for (DomElement* source_link : {link, back}) {
        ViewNodeState* state = view_tree_node_state(tree, source_link, false); ASSERT_NE(state, nullptr);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence)
            if (node->glyph_run) append_fragment_text(node, text);
        EXPECT_STREQ(text->str, "Chapter1:Titleon2(I)"); strbuf_free(text);
    }
    EXPECT_FLOAT_EQ(source->width, 640.0f); EXPECT_FLOAT_EQ(source->height, 91.75f);
    EXPECT_EQ(link->first_child, link->last_child);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/target-references.pdf"));
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED);
    environment.viewport_width = 180.0f;
    ViewTree* other = view_tree_secondary_create(&doc, &environment); ASSERT_NE(other, nullptr);
    ASSERT_EQ(layout_secondary_view(other, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(other->model->page_count, 4u);
    ASSERT_NE(layout_secondary_target(other, "tail"), nullptr); ASSERT_NE(layout_secondary_target(tree, "tail"), nullptr);
    EXPECT_EQ(layout_secondary_target(tree, "tail")->page_number, 3u);
    EXPECT_EQ(layout_secondary_target(other, "tail")->page_number, 4u);
    EXPECT_NE(tree->model->composition.get(), other->model->composition.get());
    EXPECT_TRUE(tree->model->committed); EXPECT_TRUE(other->model->committed);
}

void SecondaryViewTest::reference_width_boundary(size_t chapters, size_t label_offset) {
    char rules[512]; snprintf(rules, sizeof(rules), "@page { margin: 0 } @page:first { counter-reset: page %zu } "
        "p, div { margin: 0; font-size: 8px; line-height: 12px; font-family: monospace; orphans: 1; widows: 1 } "
        ".entry::before { content: 'AAAAAAAAAAAAA ' target-counter('#end', page) }", label_offset);
    stylesheet(rules);
    DomElement* entry = block(""); ASSERT_NE(entry, nullptr); ASSERT_TRUE(entry->set_attribute("class", "entry"));
    ASSERT_NE(block("x\nx\nx\nx\nx", "white-space: pre-wrap"), nullptr);
    DomElement* near = nullptr;
    for (size_t i = 0; i < chapters; i++) {
        DomElement* chapter = block("Chapter", "break-before: page"); ASSERT_NE(chapter, nullptr);
        if (i + 2 == chapters) { near = chapter; ASSERT_TRUE(chapter->set_attribute("id", "near")); }
        if (i + 1 == chapters) ASSERT_TRUE(chapter->set_attribute("id", "end"));
    }
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED);
    ViewTree* tree = view_tree_secondary_create(&doc, &environment); ASSERT_NE(tree, nullptr);
    ViewCssStyle* style = view_css_resolve(tree, source); ASSERT_NE(style, nullptr);
    float glyph = font_measure_char(style->font.font_handle, '0');
    float digits = chapters + label_offset < 10 ? 1.0f : 2.0f;
    tree->model->environment.page_width = (14.0f + digits) * glyph + 0.1f;
    tree->model->environment.page_height = 72.0f;
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_GE(diagnostic.reference_passes, 4u);
    EXPECT_EQ(tree->model->page_count, chapters + 2);
    ASSERT_NE(layout_secondary_target(tree, "near"), nullptr);
    EXPECT_EQ(layout_secondary_target(tree, "near")->page_number, chapters + 1);
    ASSERT_NE(near, nullptr);
    ViewNodeState* state = view_tree_node_state(tree, entry, false); ASSERT_NE(state, nullptr);
    StrBuf* text = strbuf_new(); StrBuf* expected = strbuf_create("AAAAAAAAAAAAA"); ASSERT_NE(text, nullptr); ASSERT_NE(expected, nullptr);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence)
        if (node->glyph_run) append_fragment_text(node, text);
    strbuf_append_uint64(expected, chapters + 2 + label_offset);
    EXPECT_STREQ(text->str, expected->str); strbuf_free(text); strbuf_free(expected);
}

TEST_F(SecondaryViewTest, ReferenceRepaginationMovesATargetFromNineToTen) { reference_width_boundary(9); }
TEST_F(SecondaryViewTest, ReferenceRepaginationMovesATargetFromNinetyNineToOneHundred) { reference_width_boundary(99); }
TEST_F(SecondaryViewTest, LogicalLabelWidthRepaginatesWithoutChangingPhysicalTargetIdentity) { reference_width_boundary(9, 90); }

TEST_F(SecondaryViewTest, TargetUrlsDecodeUnicodeFragmentsAndResolveOnlyTheSameDocument) {
    ASSERT_TRUE(create_dir("temp/paged-media-responsive"));
    stylesheet("@page { size: 240px 120px; margin: 10px } p,div,a { margin: 0; font-size: 10px; line-height: 12px } "
        "a { display: block } a::after { content: target-text(attr(href)) ' on ' target-counter(attr(href),page) }");
    doc.url = lam::own(url_parse("https://example.test/books/report.html?edition=1#old")); ASSERT_NE(doc.url, nullptr);
    const char* urls[] = {"#Chapter%20%CE%94", "report.html?edition=1#Chapter%20%CE%94",
        "https://example.test/books/report.html?edition=1#Chapter%20%CE%94"};
    DomElement* links[3] = {};
    for (size_t i = 0; i < 3; i++) {
        links[i] = block("", nullptr, "a"); ASSERT_NE(links[i], nullptr);
        ASSERT_TRUE(links[i]->set_attribute("href", urls[i]));
    }
    DomElement* target = block("Encoded title", "break-before: page"); ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->set_attribute("id", "Chapter Δ"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    for (DomElement* link : links) {
        ViewNodeState* state = view_tree_node_state(tree, link, false); ASSERT_NE(state, nullptr);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence)
            if (node->glyph_run) append_fragment_text(node, text);
        EXPECT_STREQ(text->str, "Encodedtitleon2"); strbuf_free(text);
    }
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-responsive/encoded-targets.pdf"));
    const char* invalid[] = {"report.html?edition=2#Chapter%20%CE%94", "https://other.test/books/report.html?edition=1#Chapter%20%CE%94",
        "#Chapter%20%CE%94%00ignored", "#Chapter%20%FF"};
    for (const char* url : invalid) {
        ASSERT_TRUE(links[0]->set_attribute("href", url));
        ViewTree* rejected = secondary();
        EXPECT_EQ(layout_secondary_view(rejected, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_FALSE(rejected->model->committed);
    }
}

TEST_F(SecondaryViewTest, TargetErrorsDoNotPublishMissingExternalOrUnplacedPageBindings) {
    stylesheet("@page { size: 240px 120px; margin: 10px } a::after { content: target-counter(attr(href), page) }");
    DomElement* link = block("Entry", nullptr, "a"); ASSERT_NE(link, nullptr);
    DomElement* hidden = block("Hidden", "display: none"); ASSERT_NE(hidden, nullptr); ASSERT_TRUE(hidden->set_attribute("id", "hidden"));
    const char* urls[] = {"#missing", "other.html#hidden", "#hidden"};
    const TypesetStatus statuses[] = {TYPESET_INVALID, TYPESET_INVALID, TYPESET_UNPLACEABLE};
    const char* reasons[] = {"target reference has no matching source ID", "target reference requires a local fragment URL",
        "target page reference has no placed content box"};
    PagedLayoutOptions options = paged_layout_options_default();
    for (size_t i = 0; i < 3; i++) {
        ASSERT_TRUE(link->set_attribute("href", urls[i]));
        ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), statuses[i]);
        EXPECT_STREQ(diagnostic.reason, reasons[i]); EXPECT_FALSE(tree->model->committed);
        EXPECT_EQ(diagnostic.source.address, link);
    }
}

TEST_F(SecondaryViewTest, SelfReferencingGeneratedTextExhaustsTheConfiguredPassBudget) {
    stylesheet("@page { size: 240px 120px; margin: 10px } div::before { content: 'x' target-text('#cycle', before) }");
    DomElement* target = block("Body"); ASSERT_NE(target, nullptr); ASSERT_TRUE(target->set_attribute("id", "cycle"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); options.max_reference_passes = 4;
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(diagnostic.reference_passes, 4u); EXPECT_FALSE(tree->model->committed);
    EXPECT_STREQ(diagnostic.reason, "reference pagination exhausted its pass budget");
    EXPECT_FALSE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/unsettled-target.pdf"));
}

TEST_F(SecondaryViewTest, ContentsLeadersRepeatPositionedPatternsAndKeepPageLabelsAtTheEndEdge) {
    stylesheet("@page { size: 240px 120px; margin: 10px } "
        "p, div, a { margin: 0; font-size: 10px; line-height: 14px } a { display: block } "
        "a::after { content: leader(dotted) target-counter(attr(href url), page); color: blue }");
    DomElement* entries[2] = {block("First chapter", nullptr, "a"), block("Second chapter", nullptr, "a")};
    const char* names[] = {"#first", "#second"};
    for (size_t i = 0; i < 2; i++) { ASSERT_NE(entries[i], nullptr); ASSERT_TRUE(entries[i]->set_attribute("href", names[i])); }
    DomElement* first = block("First chapter body", "break-before: page"); ASSERT_NE(first, nullptr); ASSERT_TRUE(first->set_attribute("id", "first"));
    DomElement* second = block("Second chapter body", "break-before: page"); ASSERT_NE(second, nullptr); ASSERT_TRUE(second->set_attribute("id", "second"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_GE(diagnostic.reference_passes, 3u);
    for (size_t i = 0; i < 2; i++) {
        ViewNodeState* state = view_tree_node_state(tree, entries[i], false); ASSERT_NE(state, nullptr);
        LayoutViewNode *leader = nullptr, *number = nullptr;
        for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) if (node->glyph_run) {
            if (node->glyph_run->text.get()[0] == '.') leader = node;
            else if (node->glyph_run->text.get()[0] == (char)('2' + i)) number = node;
        }
        ASSERT_NE(leader, nullptr); ASSERT_NE(number, nullptr);
        EXPECT_GT(leader->glyph_run->count, 10); EXPECT_EQ(leader->glyph_run->color.b, 255);
        EXPECT_EQ(leader->glyph_run->text_len, leader->glyph_run->count);
        EXPECT_FLOAT_EQ(number->rect.x + number->rect.width, 230.0f);
        EXPECT_FLOAT_EQ(leader->rect.x + leader->rect.width, number->rect.x);
        EXPECT_FLOAT_EQ(leader->glyph_run->baseline_y, number->glyph_run->baseline_y);
        EXPECT_GE(leader->glyph_run->x, leader->rect.x);
        float advance = font_measure_char(leader->computed_style->font.font_handle, '.');
        EXPECT_LE(leader->glyph_run->x + leader->glyph_run->xs.get()[leader->glyph_run->count - 1] + advance,
            number->rect.x);
    }
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/contents-leaders.pdf"));
}

TEST_F(SecondaryViewTest, LeaderStringsIgnoreLineBreaksAndSupportSolidSpaceAndSharedExpansion) {
    stylesheet("@page { size: 240px 120px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 14px }");
    const char* values[] = {"content: leader(solid) 'END'", "content: leader(space) 'END'",
        "content: leader('\\a .\\a ') 'END'", "content: leader('.') 'Middle' leader('_') 'END'"};
    for (const char* value : values) {
        DomElement* entry = block("Start"); ASSERT_NE(entry, nullptr);
        ASSERT_TRUE(entry->set_attribute("class", "entry"));
        StrBuf* css = strbuf_create("@page { size: 240px 120px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 14px } .entry::after { "); ASSERT_NE(css, nullptr);
        strbuf_append_str(css, value); strbuf_append_str(css, " }");
        stylesheet(css->str); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << value << ": " << diagnostic.reason;
        ViewNodeState* state = view_tree_node_state(tree, entry, false); ASSERT_NE(state, nullptr);
        size_t patterns = 0;
        for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) if (node->glyph_run) {
            const PaintGlyphRun* run = node->glyph_run;
            if (run->text.get()[0] == '.' || run->text.get()[0] == '_' || run->text.get()[0] == ' ') {
                patterns++; EXPECT_GT(run->count, 1);
                EXPECT_EQ(strchr(run->text, '\n'), nullptr); EXPECT_EQ(strchr(run->text, '\r'), nullptr);
            }
            if (strcmp(run->text, "END") == 0) EXPECT_FLOAT_EQ(node->rect.x + node->rect.width, 230.0f);
        }
        EXPECT_EQ(patterns, strstr(value, "Middle") ? 2u : 1u);
        ASSERT_TRUE(entry->set_attribute("class", "old"));
    }
}

TEST_F(SecondaryViewTest, ALeaderWithoutAdjacentContentIsDiagnosedAndNotPublished) {
    stylesheet("@page { size: 240px 120px; margin: 10px } div::after { content: leader('.') }");
    ASSERT_NE(block(""), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed); EXPECT_STREQ(diagnostic.reason, "leader cannot occupy a line alone");
}

TEST_F(SecondaryViewTest, RepeatedLeaderGlyphsRespectTheSelectedViewContributionBudget) {
    stylesheet("@page { size: 240px 120px; margin: 10px } div::after { content: leader('.') 'END' }");
    ASSERT_NE(block("Start"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); options.max_items = 3;
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_FALSE(tree->model->committed);
}

TEST(TypesetTest, InlinePaintGlueContributesVerticalMetricsAndUsesOrderedExpansion) {
    TypesetItem items[3] = {};
    items[0].kind = items[2].kind = TYPESET_BOX;
    items[0].metrics = items[2].metrics = {10.0f, 8.0f, 2.0f, 8.0f, {}, nullptr};
    items[1].kind = TYPESET_GLUE; items[1].glue = {2.0f, 1.0f, 0.0f, 1, 0, false, false};
    items[1].metrics = {2.0f, 10.0f, 4.0f, 10.0f, {}, nullptr};
    items[1].boundary.legality = TYPESET_BREAK_FORBIDDEN;
    TypesetParagraph paragraph = {items, 3, 0.0f, nullptr, nullptr, nullptr};
    TypesetLineCandidate scratch[3] = {}, line = {};
    ASSERT_EQ(typeset_next_line(&paragraph, 0, 100.0f, scratch, 3, &line), TYPESET_OK);
    EXPECT_FLOAT_EQ(line.height, 14.0f); EXPECT_FLOAT_EQ(line.depth, 4.0f);
    EXPECT_EQ(line.packing.order, 1u); EXPECT_FLOAT_EQ(typeset_glue_advance(&items[1].glue, &line.packing), 80.0f);
}

TEST_F(SecondaryViewTest, PageFloatsShareThePageWithNotesAndMoveExistingBodyGeometry) {
    stylesheet("@page { size: 240px 120px; margin: 10px; @bottom-right { content: counter(page) '/' counter(pages); font-size: 8px } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* prelude = block("Prelude"); ASSERT_NE(prelude, nullptr);
    DomElement* top = block("Top A\nTop B", "float: top; float-reference: page; height: 30%; color: blue; background-color: #e8eefc");
    ASSERT_NE(top, nullptr); top->y = 23.25f; top->height = 7.75f;
    DomElement* body = block("Body "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note", "float: footnote; color: green", "span", body); ASSERT_NE(note, nullptr);
    DomElement* bottom = block("Bottom", "float: bottom; float-reference: page; height: 24px; color: red"); ASSERT_NE(bottom, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    LayoutViewNode* top_box = source_fragment(tree, top, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(top_box, nullptr);
    LayoutViewNode* bottom_box = source_fragment(tree, bottom, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(bottom_box, nullptr);
    LayoutViewNode* note_box = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(note_box, nullptr);
    EXPECT_FLOAT_EQ(top_box->rect.y, 10.0f); EXPECT_FLOAT_EQ(top_box->rect.height, 30.0f);
    EXPECT_FLOAT_EQ(bottom_box->rect.y, 74.0f); EXPECT_FLOAT_EQ(bottom_box->rect.height, 24.0f);
    EXPECT_FLOAT_EQ(note_box->rect.y, 98.0f); EXPECT_FLOAT_EQ(note_box->rect.height, 12.0f);
    LayoutViewNode* prelude_text = source_fragment(tree, prelude->first_child, VIEW_FRAGMENT_BODY, false); ASSERT_NE(prelude_text, nullptr);
    LayoutViewNode* body_text = source_fragment(tree, body->first_child, VIEW_FRAGMENT_BODY, false); ASSERT_NE(body_text, nullptr);
    EXPECT_FLOAT_EQ(prelude_text->rect.y, 40.0f); EXPECT_FLOAT_EQ(body_text->rect.y, 52.0f);
    ASSERT_NE(prelude_text->glyph_run, nullptr); ASSERT_NE(body_text->glyph_run, nullptr);
    EXPECT_FLOAT_EQ(body_text->glyph_run->baseline_y - prelude_text->glyph_run->baseline_y, 12.0f);
    EXPECT_LT(body_text->rect.y + body_text->rect.height, bottom_box->rect.y);
    EXPECT_EQ(top->parent, source); EXPECT_FLOAT_EQ(top->y, 23.25f); EXPECT_FLOAT_EQ(top->height, 7.75f);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/page-floats.pdf"));
}

TEST_F(SecondaryViewTest, PageFloatsDeferWhenTheAnchorPageHasNoRemainingSpace) {
    stylesheet("@page { size: 220px 100px; margin: 10px } "
        "p, div { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("Body", "height: 60px"); ASSERT_NE(body, nullptr);
    DomElement* top = block("Top A\nTop B\nTop C", "float: top; float-reference: page; color: blue"); ASSERT_NE(top, nullptr);
    DomElement* bottom = block("Bottom A\nBottom B\nBottom C", "float: bottom; float-reference: page; color: red"); ASSERT_NE(bottom, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, body, VIEW_FRAGMENT_BODY, true)), 1u);
    for (DomElement* element : {top, bottom}) {
        LayoutViewNode* box = source_fragment(tree, element, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(box, nullptr);
        EXPECT_EQ(occurrence_page(box), 2u); EXPECT_TRUE(box->first_fragment); EXPECT_TRUE(box->last_fragment);
        ViewNodeState* state = view_tree_node_state(tree, element, false); ASSERT_NE(state, nullptr);
        size_t count = 0;
        for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
            if (fragment->role == VIEW_FRAGMENT_FLOAT && fragment->paint_box) count++;
        EXPECT_EQ(count, 1u);
    }
    EXPECT_FLOAT_EQ(source_fragment(tree, top, VIEW_FRAGMENT_FLOAT, true)->rect.y, 10.0f);
    EXPECT_FLOAT_EQ(source_fragment(tree, bottom, VIEW_FRAGMENT_FLOAT, true)->rect.y, 54.0f);
}

TEST_F(SecondaryViewTest, ExplicitPageFloatDeferralRetainsThePhysicalAnchorPage) {
    stylesheet("@page { size: 220px 100px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("Body"), nullptr);
    DomElement* top = block("Later", "float: top; float-reference: page; float-defer: 1"); ASSERT_NE(top, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* anchor = source_fragment(tree, top, VIEW_FRAGMENT_BODY, false); ASSERT_NE(anchor, nullptr);
    LayoutViewNode* box = source_fragment(tree, top, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(box, nullptr);
    EXPECT_EQ(occurrence_page(anchor), 1u); EXPECT_FLOAT_EQ(anchor->rect.height, 0.0f);
    EXPECT_EQ(occurrence_page(box), 2u);
}

TEST_F(SecondaryViewTest, OversizedPageFloatFailsWithoutRepeatedEmptyPages) {
    stylesheet("@page { size: 220px 100px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("Body"), nullptr);
    ASSERT_NE(block("Tall", "float: top; float-reference: page; height: 120px"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_LE(tree->model->page_count, 2u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, PageFloatClearanceStartsTheNextRegionWithoutMovingItsAnchor) {
    stylesheet("@page { size: 220px 100px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("Body"), nullptr);
    DomElement* first = block("First", "float: top; float-reference: page"); ASSERT_NE(first, nullptr);
    DomElement* second = block("Second", "float: top; float-reference: page; clear: top"); ASSERT_NE(second, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* first_box = source_fragment(tree, first, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(first_box, nullptr);
    LayoutViewNode* second_box = source_fragment(tree, second, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(second_box, nullptr);
    LayoutViewNode* anchor = source_fragment(tree, second, VIEW_FRAGMENT_BODY, false); ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(occurrence_page(first_box), 1u); EXPECT_EQ(occurrence_page(second_box), 2u);
    EXPECT_EQ(occurrence_page(anchor), 1u);
}

TEST_F(SecondaryViewTest, ForcedBodyBreakDoesNotFlushAPageFloatDeferral) {
    stylesheet("@page { size: 220px 100px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("First body"), nullptr);
    DomElement* delayed = block("Delayed", "float: top; float-reference: page; float-defer: 2"); ASSERT_NE(delayed, nullptr);
    DomElement* next = block("Next body", "break-before: page"); ASSERT_NE(next, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    LayoutViewNode* next_body = source_fragment(tree, next, VIEW_FRAGMENT_BODY, true); ASSERT_NE(next_body, nullptr);
    LayoutViewNode* delayed_box = source_fragment(tree, delayed, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(delayed_box, nullptr);
    EXPECT_EQ(occurrence_page(next_body), 2u); EXPECT_EQ(occurrence_page(delayed_box), 3u);
}

TEST_F(SecondaryViewTest, ClosingBodyDecorationsCannotOverlapPublishedFootnotes) {
    stylesheet("@page { size: 220px 100px; margin: 10px } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap }");
    DomElement* body = block("Call ", "padding-bottom: 40px"); ASSERT_NE(body, nullptr);
    ASSERT_NE(block("A\nB\nC", "float: footnote; footnote-policy: line", "span", body), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed); EXPECT_NE(diagnostic.reason, nullptr);
}

TEST_F(SecondaryViewTest, LateBlockClosuresRollbackBodyNotesMarksAndTargetsTogether) {
    LateClosureFixture fixture = {}; ASSERT_TRUE(late_closure_fixture(&fixture));
    DomElement* prelude = fixture.prelude; DomElement* moved = fixture.moved; DomElement* note = fixture.note;
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, prelude, VIEW_FRAGMENT_BODY, true)), 1u);
    LayoutViewNode* body = source_fragment(tree, moved, VIEW_FRAGMENT_BODY, true); ASSERT_NE(body, nullptr);
    EXPECT_EQ(occurrence_page(body), 2u); EXPECT_TRUE(body->first_fragment); EXPECT_TRUE(body->last_fragment);
    ViewNodeState* state = view_tree_node_state(tree, note, false); ASSERT_NE(state, nullptr);
    size_t calls = 0, notes = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        if (fragment->role == VIEW_FRAGMENT_BODY && fragment->glyph_run) {
            calls++; EXPECT_EQ(occurrence_page(fragment), 2u); EXPECT_STREQ(fragment->glyph_run->text, "1");
        }
        if (fragment->role == VIEW_FRAGMENT_NOTE && fragment->paint_box) {
            notes++; EXPECT_EQ(occurrence_page(fragment), 2u);
            EXPECT_GE(fragment->rect.y, body->rect.y + body->rect.height);
        }
    }
    EXPECT_EQ(calls, 1u); EXPECT_EQ(notes, 1u);
    const TypesetTarget* target = layout_secondary_target(tree, "moved"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->page_number, 2u);
    const char* titles[] = {"Prelude", "Moved"};
    for (size_t i = 0; i < 2; i++) {
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        LayoutViewNode* header = tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER]; ASSERT_NE(header, nullptr);
        append_fragment_text(header, text); EXPECT_STREQ(text->str, titles[i]); strbuf_free(text);
    }
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    append_fragment_text(body, text); EXPECT_STREQ(text->str, "1: Call 1"); strbuf_free(text);
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, OversizedKeepGroupRelaxesOnlyAtAUsableBodyBoundary) {
    stylesheet("@page { size: 180px 24px; margin: 0 } p, div { margin: 0; font-size: 10px; line-height: 12px; "
        "white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* paragraph = block("A\nB\nC", "break-inside: avoid"); ASSERT_NE(paragraph, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 1u);
    EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
    ViewNodeState* state = view_tree_node_state(tree, paragraph->first_child, false); ASSERT_NE(state, nullptr);
    size_t coverage[5] = {}, ink[2] = {};
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        uint32_t page = occurrence_page(fragment); ASSERT_GE(page, 1u); ASSERT_LE(page, 2u);
        if (fragment->glyph_run) ink[page - 1]++;
        for (size_t i = fragment->text_start; i < fragment->text_start + fragment->text_length; i++) {
            ASSERT_LT(i, 5u); coverage[i]++;
        }
    }
    EXPECT_EQ(ink[0], 2u); EXPECT_EQ(ink[1], 1u);
    for (size_t i = 0; i < 5; i++) EXPECT_EQ(coverage[i], 1u);
}

TEST_F(SecondaryViewTest, ForcedPageBoundaryOverridesSiblingAndEnclosingAvoidance) {
    stylesheet("@page { size: 180px 80px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px }");
    DomElement* group = block(nullptr, "break-inside: avoid"); ASSERT_NE(group, nullptr);
    DomElement* first = block("First", "break-after: avoid", "div", group); ASSERT_NE(first, nullptr);
    DomElement* second = block("Second", "break-before: page; break-after: page", "div", group); ASSERT_NE(second, nullptr);
    DomElement* third = block("Third", "break-before: avoid; break-after: page", "div", group); ASSERT_NE(third, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, first->first_child, VIEW_FRAGMENT_BODY, false)), 1u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, second->first_child, VIEW_FRAGMENT_BODY, false)), 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, third->first_child, VIEW_FRAGMENT_BODY, false)), 3u);
}

TEST_F(SecondaryViewTest, FootnoteReservationMovesCallBeforePublishingItsFragments) {
    stylesheet("@page { size: 200px 100px; margin: 10px } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    ASSERT_NE(block("Prelude", "height: 48px"), nullptr);
    DomElement* paragraph = block("Statement "); ASSERT_NE(paragraph, nullptr);
    DomElement* note = block("Note A\nNote B\nNote C", "float: footnote; footnote-policy: line; color: blue", "span", paragraph);
    ASSERT_NE(note, nullptr); note->x = 91.25f; note->width = 7.5f;
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, note, false); ASSERT_NE(state, nullptr);
    size_t calls = 0, notes = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        if (fragment->role == VIEW_FRAGMENT_BODY && fragment->glyph_run) {
            calls++; EXPECT_EQ(occurrence_page(fragment), 2u); EXPECT_STREQ(fragment->glyph_run->text, "1");
        }
        if (fragment->role == VIEW_FRAGMENT_NOTE && fragment->paint_box) {
            notes++; EXPECT_EQ(occurrence_page(fragment), 2u);
            EXPECT_FLOAT_EQ(fragment->rect.y, 54.0f); EXPECT_FLOAT_EQ(fragment->rect.height, 36.0f);
            EXPECT_TRUE(fragment->first_fragment); EXPECT_TRUE(fragment->last_fragment);
        }
    }
    EXPECT_EQ(calls, 1u); EXPECT_EQ(notes, 1u);
    EXPECT_EQ(note->parent, paragraph); EXPECT_FLOAT_EQ(note->x, 91.25f); EXPECT_FLOAT_EQ(note->width, 7.5f);
}

TEST_F(SecondaryViewTest, LateRollbackUndoesTopFloatTranslationOfEarlierText) {
    LateClosureFixture fixture = {}; ASSERT_TRUE(late_closure_fixture(&fixture, true));
    DomElement* prelude = fixture.prelude; DomElement* moved = fixture.moved;
    DomElement* note = fixture.note; DomElement* floating = fixture.floating;
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* previous = source_fragment(tree, prelude, VIEW_FRAGMENT_BODY, true); ASSERT_NE(previous, nullptr);
    EXPECT_FLOAT_EQ(previous->rect.y, 10); EXPECT_FLOAT_EQ(previous->rect.height, 20);
    LayoutViewNode* text = source_fragment(tree, prelude->first_child, VIEW_FRAGMENT_BODY, false); ASSERT_NE(text, nullptr);
    ASSERT_NE(text->glyph_run, nullptr); EXPECT_FLOAT_EQ(text->rect.y, 10);
    EXPECT_GT(text->glyph_run->baseline_y, 10); EXPECT_LT(text->glyph_run->baseline_y, 22);
    EXPECT_EQ(occurrence_page(previous), 1u);
    LayoutViewNode* float_box = source_fragment(tree, floating, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(float_box, nullptr);
    EXPECT_EQ(occurrence_page(float_box), 2u); EXPECT_FLOAT_EQ(float_box->rect.y, 10);
    LayoutViewNode* body = source_fragment(tree, moved, VIEW_FRAGMENT_BODY, true); ASSERT_NE(body, nullptr);
    EXPECT_EQ(occurrence_page(body), 2u); EXPECT_FLOAT_EQ(body->rect.y, 22);
    EXPECT_FLOAT_EQ(body->rect.height, 44);
    LayoutViewNode* note_box = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(note_box, nullptr);
    EXPECT_EQ(occurrence_page(note_box), 2u); EXPECT_FLOAT_EQ(note_box->rect.y, 66);
    ViewNodeState* state = view_tree_node_state(tree, floating, false); ASSERT_NE(state, nullptr);
    size_t floats = 0;
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence)
        if (node->role == VIEW_FRAGMENT_FLOAT && node->paint_box) floats++;
    EXPECT_EQ(floats, 1u);
}

TEST_F(SecondaryViewTest, BlockTrialBudgetCannotBeReplenishedByRollback) {
    LateClosureFixture fixture = {}; ASSERT_TRUE(late_closure_fixture(&fixture));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); options.max_block_trials = 5;
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "block composition exhausted its trial budget");
    EXPECT_EQ(diagnostic.status, TYPESET_BUDGET_EXHAUSTED); EXPECT_FALSE(tree->model->committed);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(tree->model->node_count, 1u);
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, BlockFootnotePolicyMovesTheWholeParagraphBeforePublishingText) {
    stylesheet("@page { size: 200px 100px; margin: 10px; @top-center { content: string(heading) } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    ASSERT_NE(block("Prelude", "height: 36px; string-set: heading 'Prelude'"), nullptr);
    DomElement* paragraph = block("First\nSecond\nThird ", "string-set: heading 'Moved'"); ASSERT_NE(paragraph, nullptr);
    DomElement* note = block("Note A\nNote B", "float: footnote; footnote-policy: block; color: blue", "span", paragraph);
    ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* text = view_tree_node_state(tree, paragraph->first_child, false); ASSERT_NE(text, nullptr);
    size_t covered = 0;
    for (LayoutViewNode* fragment = text->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        EXPECT_EQ(occurrence_page(fragment), 2u); covered += fragment->text_length;
    }
    EXPECT_EQ(covered, paragraph->first_child->as_text()->length);
    ViewNodeState* notes = view_tree_node_state(tree, note, false); ASSERT_NE(notes, nullptr);
    size_t calls = 0, bodies = 0;
    for (LayoutViewNode* fragment = notes->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        if (fragment->role == VIEW_FRAGMENT_BODY && fragment->glyph_run) { calls++; EXPECT_EQ(occurrence_page(fragment), 2u); }
        if (fragment->role == VIEW_FRAGMENT_NOTE && fragment->paint_box) { bodies++; EXPECT_EQ(occurrence_page(fragment), 2u); }
    }
    EXPECT_EQ(calls, 1u); EXPECT_EQ(bodies, 1u);
    StrBuf* heading = strbuf_new(); ASSERT_NE(heading, nullptr);
    append_fragment_text(tree->model->pages.get()[0]->margin_boxes[CSS_PAGE_TOP_CENTER], heading);
    EXPECT_STREQ(heading->str, "Prelude"); strbuf_reset(heading);
    append_fragment_text(tree->model->pages.get()[1]->margin_boxes[CSS_PAGE_TOP_CENTER], heading);
    EXPECT_STREQ(heading->str, "Moved"); strbuf_free(heading);
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/block-footnote-policy.pdf"));
}

TEST_F(SecondaryViewTest, BlockFootnoteMinimaCanRelaxAfterItsRequiredNoteHasBeenAdmitted) {
    stylesheet("@page { size: 200px 60px; margin: 0 } p, div, span { margin: 0; font-size: 10px; line-height: 12px; "
        "white-space: pre-wrap; orphans: 10; widows: 10 }");
    DomElement* paragraph = block("A "); ASSERT_NE(paragraph, nullptr);
    DomElement* note = block("Note", "float: footnote; footnote-policy: block", "span", paragraph); ASSERT_NE(note, nullptr);
    DomText* tail = DomText::create_copy("\nB\nC\nD\nE", 8, paragraph); ASSERT_NE(tail, nullptr);
    ASSERT_TRUE(paragraph->DomNode::append_child(tail));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u); EXPECT_GT(diagnostic.relaxed_line_minima, 0u);
    LayoutViewNode* call = source_fragment(tree, note, VIEW_FRAGMENT_BODY, false); ASSERT_NE(call, nullptr);
    LayoutViewNode* body = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(body, nullptr);
    EXPECT_EQ(occurrence_page(call), 1u); EXPECT_EQ(occurrence_page(body), 1u);
}

TEST_F(SecondaryViewTest, AnImpossibleBlockFootnoteStopsBeforePublishingAnEdition) {
    stylesheet("@page { size: 200px 40px; margin: 0 } p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap }");
    DomElement* paragraph = block("Call "); ASSERT_NE(paragraph, nullptr);
    ASSERT_NE(block("One\nTwo\nThree\nFour", "float: footnote; footnote-policy: block", "span", paragraph), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed); EXPECT_LE(tree->model->page_count, 2u);
    EXPECT_NE(diagnostic.reason, nullptr);
}

TEST_F(SecondaryViewTest, DefaultFootnoteCallsUseAnOverridableSuperscriptStyle) {
    init_vector_engine();
    stylesheet("@page { size: 240px 120px; margin: 10px } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 } "
        ".override::footnote-call { font-size: 8px; vertical-align: baseline } "
        ".inherit::footnote-call { font-size: inherit; vertical-align: inherit } "
        ".initial::footnote-call { font-size: initial; vertical-align: initial } "
        ".unset::footnote-call { all: unset }");
    DomElement* body = block("Statement "); ASSERT_NE(body, nullptr);
    const char* classes[] = {nullptr, "override", "inherit", "initial", "unset"};
    const float sizes[] = {6.5f, 8.0f, 10.0f, 16.0f, 10.0f};
    DomElement* notes[5] = {};
    for (size_t i = 0; i < 5; i++) {
        notes[i] = block("Note", "float: footnote; color: blue", "span", body);
        ASSERT_NE(notes[i], nullptr);
        if (classes[i]) ASSERT_TRUE(notes[i]->set_attribute("class", classes[i]));
    }
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* body_text = source_fragment(tree, body->first_child, VIEW_FRAGMENT_BODY, false);
    ASSERT_NE(body_text, nullptr); ASSERT_NE(body_text->glyph_run, nullptr);
    for (size_t i = 0; i < 5; i++) {
        LayoutViewNode* call = source_fragment(tree, notes[i], VIEW_FRAGMENT_BODY, false);
        ASSERT_NE(call, nullptr); ASSERT_NE(call->glyph_run, nullptr); ASSERT_NE(call->computed_style, nullptr);
        EXPECT_FLOAT_EQ(call->glyph_run->font_size, sizes[i]);
        EXPECT_EQ(call->computed_style->pseudo_element, PSEUDO_ELEMENT_FOOTNOTE_CALL);
        if (i == 0) EXPECT_LT(call->glyph_run->baseline_y, body_text->glyph_run->baseline_y);
        else EXPECT_FLOAT_EQ(call->glyph_run->baseline_y, body_text->glyph_run->baseline_y);
        EXPECT_FLOAT_EQ(view_css_resolve(tree, notes[i])->font.font_size, 10.0f);
        EXPECT_EQ(notes[i]->first_child, notes[i]->last_child);
    }
    EXPECT_EQ(doc.stylesheet_count, 1);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    ASSERT_TRUE(create_dir("temp/paged-media-footnote-defaults"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-footnote-defaults/footnote-defaults.pdf"));
    ImageSurface* surface = render_secondary_page_snapshot(tree, 1, 2.0f); ASSERT_NE(surface, nullptr);
    size_t call_ink = 0;
    LayoutViewNode* call = source_fragment(tree, notes[0], VIEW_FRAGMENT_BODY, false);
    for (size_t y = 0; y < (size_t)(body_text->glyph_run->baseline_y * 2.0f); y++) {
        for (size_t x = (size_t)(call->rect.x * 2.0f); x < (size_t)((call->rect.x + call->rect.width) * 2.0f); x++) {
            Color pixel = {snapshot_pixel(surface, x, y)};
            if (pixel.b > pixel.r + 20 && pixel.b > pixel.g + 20) call_ink++;
        }
    }
    EXPECT_GT(call_ink, 2u);
    save_surface_to_png(surface, "temp/paged-media-footnote-defaults/footnote-defaults-native.png");
    image_surface_destroy(surface);
}

TEST_F(SecondaryViewTest, FootnoteCallAndMarkerStylesBelongToTheirViewOccurrences) {
    stylesheet("@page { size: 240px 120px; margin: 10px } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 } "
        "span::footnote-call { content: '[' counter(footnote, lower-roman) ']'; color: red; font-size: 7px; vertical-align: super } "
        "span::footnote-marker { content: counter(footnote, lower-roman) '. '; color: blue; font-size: 9px }");
    DomElement* body = block("Statement "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note text", "float: footnote; color: green", "span", body); ASSERT_NE(note, nullptr);
    DomNode* original_child = note->first_child;
    note->x = 63.75f; note->width = 9.25f;
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* call = source_fragment(tree, note, VIEW_FRAGMENT_BODY, false); ASSERT_NE(call, nullptr);
    ASSERT_NE(call->glyph_run, nullptr); EXPECT_STREQ(call->glyph_run->text, "[i]");
    EXPECT_EQ(call->glyph_run->color.r, 255); EXPECT_EQ(call->glyph_run->color.g, 0);
    EXPECT_FLOAT_EQ(call->glyph_run->font_size, 7.0f);
    ASSERT_NE(call->computed_style, nullptr); EXPECT_EQ(call->computed_style->pseudo_element, PSEUDO_ELEMENT_FOOTNOTE_CALL);
    LayoutViewNode* body_text = source_fragment(tree, body->first_child, VIEW_FRAGMENT_BODY, false); ASSERT_NE(body_text, nullptr);
    ASSERT_NE(body_text->glyph_run, nullptr); EXPECT_LT(call->glyph_run->baseline_y, body_text->glyph_run->baseline_y);
    ViewNodeState* state = view_tree_node_state(tree, note, false); ASSERT_NE(state, nullptr);
    LayoutViewNode* marker = nullptr;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        if (fragment->role == VIEW_FRAGMENT_NOTE && fragment->glyph_run && fragment->computed_style &&
            fragment->computed_style->pseudo_element == PSEUDO_ELEMENT_FOOTNOTE_MARKER) marker = fragment;
    }
    ASSERT_NE(marker, nullptr); EXPECT_EQ(marker->glyph_run->text_len, 2);
    EXPECT_EQ(memcmp(marker->glyph_run->text, "i.", 2), 0); EXPECT_EQ(marker->glyph_run->color.b, 255);
    EXPECT_FLOAT_EQ(marker->glyph_run->font_size, 9.0f);
    EXPECT_EQ(state->computed_style->pseudo_element, PSEUDO_ELEMENT_NONE); EXPECT_EQ(state->computed_style->color.g, 128);
    EXPECT_EQ(note->first_child, original_child); EXPECT_EQ(note->last_child, original_child);
    EXPECT_FLOAT_EQ(note->x, 63.75f); EXPECT_FLOAT_EQ(note->width, 9.25f);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/footnote-pseudos.pdf"));
}

TEST_F(SecondaryViewTest, HiddenFootnoteCallKeepsAnAnchorWithoutAddingASourceChild) {
    stylesheet("@page { size: 220px 100px; margin: 10px } p, span { margin: 0; font-size: 10px; line-height: 12px } "
        "span::footnote-call { content: none } span::footnote-marker { content: none }");
    DomElement* body = block("Statement "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note", "float: footnote", "span", body); ASSERT_NE(note, nullptr);
    DomNode* original_child = note->first_child;
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* anchor = source_fragment(tree, note, VIEW_FRAGMENT_BODY, false); ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(anchor->glyph_run, nullptr); EXPECT_FLOAT_EQ(anchor->rect.width, 0.0f);
    LayoutViewNode* box = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(box, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(box, text);
    EXPECT_STREQ(text->str, "Note"); strbuf_free(text);
    EXPECT_EQ(note->first_child, original_child); EXPECT_EQ(note->last_child, original_child);
}

TEST_F(SecondaryViewTest, FootnoteAreaEdgesAndLimitShareThePaintedReservation) {
    stylesheet("@page { size: 240px 120px; margin: 10px; @footnote { max-height: 24px; padding: 1px; border: 0 solid blue; background: #eef2ff } "
        "@footnote { padding: 4px 5px; border-top: 2px solid blue } } "
        "@page :first { @footnote { border-top-color: red } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    DomElement* note = block("A\nB\nC\nD\nE\nF", "float: footnote; color: green", "span", body); ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, note, false); ASSERT_NE(state, nullptr);
    size_t count = 0;
    for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) {
        if (box->role != VIEW_FRAGMENT_NOTE || !box->paint_box) continue;
        count++;
        LayoutViewNode* area = box->parent; ASSERT_NE(area, nullptr); ASSERT_NE(area->computed_boundary, nullptr);
        EXPECT_EQ(area->role, VIEW_FRAGMENT_NOTE); EXPECT_FALSE(area->source.expected_id); EXPECT_TRUE(area->generated);
        EXPECT_FLOAT_EQ(area->rect.x, 10.0f); EXPECT_FLOAT_EQ(area->rect.width, 220.0f);
        EXPECT_FLOAT_EQ(area->rect.y + area->rect.height, 110.0f);
        EXPECT_FLOAT_EQ(area->computed_boundary->padding.left, 5.0f);
        EXPECT_FLOAT_EQ(area->computed_boundary->padding.bottom, 4.0f);
        EXPECT_FLOAT_EQ(area->computed_boundary->border->width.top, 2.0f);
        EXPECT_EQ(area->computed_boundary->background->color.r, 238);
        EXPECT_EQ(area->computed_boundary->border->top_color.r, count == 1 ? 255 : 0);
        EXPECT_EQ(area->computed_boundary->border->top_color.b, count == 1 ? 0 : 255);
        EXPECT_FLOAT_EQ(area->rect.height, count == 1 ? 34.0f : 58.0f);
        EXPECT_FLOAT_EQ(box->rect.x, 15.0f); EXPECT_FLOAT_EQ(box->rect.width, 210.0f);
        EXPECT_FLOAT_EQ(box->rect.y, area->rect.y + 6.0f);
        EXPECT_FLOAT_EQ(box->rect.y + box->rect.height, 106.0f);
    }
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(note->parent, body); EXPECT_EQ(source->x, 11.25f); EXPECT_EQ(source->height, 91.75f);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/footnote-area.pdf"));
}

TEST_F(SecondaryViewTest, FootnoteAreaMarginsAndWidthConstrainEveryContinuation) {
    init_vector_engine();
    stylesheet("@page { size: 240px 120px; margin: 10px; @footnote { width: 50%; margin: 6px 8px 4px 12px; "
        "padding: 2px; border: 2px solid blue; max-height: 28px; box-sizing: border-box; background: #eef2ff } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    DomElement* note = block("A\nB\nC\nD", "float: footnote", "span", body); ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, note, false); ASSERT_NE(state, nullptr);
    size_t count = 0;
    for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) {
        if (box->role != VIEW_FRAGMENT_NOTE || !box->paint_box) continue;
        count++; LayoutViewNode* area = box->parent; ASSERT_NE(area, nullptr);
        EXPECT_FLOAT_EQ(area->rect.x, 22.0f); EXPECT_FLOAT_EQ(area->rect.width, 110.0f);
        EXPECT_FLOAT_EQ(area->rect.y + area->rect.height, 106.0f);
        EXPECT_FLOAT_EQ(area->rect.height, count == 1 ? 20.0f : 44.0f);
        EXPECT_FLOAT_EQ(box->rect.x, 26.0f); EXPECT_FLOAT_EQ(box->rect.width, 102.0f);
        EXPECT_FLOAT_EQ(box->rect.y, area->rect.y + 4.0f);
        ASSERT_NE(area->computed_boundary, nullptr);
        EXPECT_FLOAT_EQ(area->computed_boundary->margin.top, 6.0f);
        EXPECT_FLOAT_EQ(area->computed_boundary->margin.bottom, 4.0f);
    }
    EXPECT_EQ(count, 2u); EXPECT_EQ(note->parent, body);
    ASSERT_TRUE(create_dir("temp/paged-media-area-sizing"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-area-sizing/margins-width.pdf"));
    for (uint32_t page_number = 1; page_number <= 2; page_number++) {
        ImageSurface* surface = render_secondary_page_snapshot(tree, page_number, 1.0f); ASSERT_NE(surface, nullptr);
        size_t top = page_number == 1 ? 86 : 62;
        EXPECT_EQ(snapshot_pixel(surface, 23, top + 1), 0xffff0000u);
        EXPECT_EQ(snapshot_pixel(surface, 60, 103), 0xfffff2eeu);
        EXPECT_EQ(snapshot_pixel(surface, 21, 100), 0xffffffffu);
        EXPECT_EQ(snapshot_pixel(surface, 133, 100), 0xffffffffu);
        save_surface_to_png(surface, page_number == 1 ? "temp/paged-media-area-sizing/margins-native-1.png" :
            "temp/paged-media-area-sizing/margins-native-2.png"); image_surface_destroy(surface);
    }
}

TEST_F(SecondaryViewTest, FootnoteAreaMinimumHeightReservesSpaceAndKeepsNotesAtTheTop) {
    init_vector_engine();
    stylesheet("@page { size: 240px 120px; margin: 10px; @footnote { min-height: 48px; max-height: 24px; "
        "margin: 3px 5px 7px; padding: 2px; border-top: 2px solid blue; background: #eef2ff } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("A\nB\nCall "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note", "float: footnote; footnote-policy: line", "span", body); ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    LayoutViewNode* box = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true);
    ASSERT_NE(box, nullptr); LayoutViewNode* area = box->parent; ASSERT_NE(area, nullptr);
    EXPECT_FLOAT_EQ(area->rect.x, 15.0f); EXPECT_FLOAT_EQ(area->rect.width, 210.0f);
    EXPECT_FLOAT_EQ(area->rect.y, 49.0f); EXPECT_FLOAT_EQ(area->rect.height, 54.0f);
    EXPECT_FLOAT_EQ(box->rect.y, 53.0f); EXPECT_FLOAT_EQ(box->rect.height, 12.0f);
    EXPECT_EQ(body->parent, source);
    ASSERT_TRUE(create_dir("temp/paged-media-area-sizing"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-area-sizing/min-height.pdf"));
    ImageSurface* surface = render_secondary_page_snapshot(tree, 1, 1.0f); ASSERT_NE(surface, nullptr);
    EXPECT_EQ(snapshot_pixel(surface, 100, 50), 0xffff0000u);
    EXPECT_EQ(snapshot_pixel(surface, 100, 80), 0xfffff2eeu);
    EXPECT_EQ(snapshot_pixel(surface, 100, 104), 0xffffffffu);
    save_surface_to_png(surface, "temp/paged-media-area-sizing/min-height-native.png"); image_surface_destroy(surface);
}

TEST_F(SecondaryViewTest, FootnoteAreaDeclaredDimensionsRespectSizingAndPagePercentages) {
    const struct { const char* dimensions; float width, height; } cases[] = {
        {"width: 50px; min-width: 120px; max-width: 60px; height: 30px; min-height: 40px; max-height: 20px", 128.0f, 48.0f},
        {"width: 50px; min-width: 120px; max-width: 60px; height: 30px; min-height: 40px; max-height: 20px; box-sizing: border-box", 120.0f, 40.0f},
        {"width: 50%; height: 20%", 118.0f, 28.0f},
        {"width: 50%; height: 20%; box-sizing: border-box", 110.0f, 20.0f}
    };
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note", "float: footnote; footnote-policy: line", "span", body); ASSERT_NE(note, nullptr);
    for (const auto& test : cases) {
        SCOPED_TRACE(test.dimensions);
        StrBuf* css = strbuf_new(); ASSERT_NE(css, nullptr);
        strbuf_append_str(css, "@page { size: 240px 120px; margin: 10px; @footnote { margin: 3px 0 7px; padding: 2px; border: 2px solid blue; ");
        strbuf_append_str(css, test.dimensions);
        strbuf_append_str(css, " } } p, div, span { margin: 0; font-size: 10px; line-height: 12px }");
        stylesheet(css->str); strbuf_free(css);
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 1u);
        LayoutViewNode* box = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true);
        ASSERT_NE(box, nullptr); LayoutViewNode* area = box->parent; ASSERT_NE(area, nullptr);
        EXPECT_FLOAT_EQ(area->rect.width, test.width); EXPECT_FLOAT_EQ(area->rect.height, test.height);
        EXPECT_FLOAT_EQ(area->rect.y, 103.0f - test.height); EXPECT_FLOAT_EQ(box->rect.y, area->rect.y + 4.0f);
        EXPECT_FLOAT_EQ(box->rect.width, test.width - 8.0f); EXPECT_FLOAT_EQ(box->rect.height, 12.0f);
    }
}

TEST_F(SecondaryViewTest, FootnoteAreaSignedMarginsAndLateCallRollbackKeepThePhysicalAnchor) {
    stylesheet("@page { size: 240px 120px; margin: 10px; @footnote { min-height: 48px; "
        "margin: 3px 0 7px -4px; padding: 2px; border-top: 2px solid blue; background: #eef2ff } } "
        "@page :left { @footnote { margin-top: -3px; margin-bottom: -4px } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("A\nB\nC\nCall "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note", "float: footnote; footnote-policy: line", "span", body); ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    LayoutViewNode* box = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true);
    ASSERT_NE(box, nullptr); LayoutViewNode* area = box->parent; ASSERT_NE(area, nullptr);
    EXPECT_EQ(area->parent, &tree->model->pages.get()[1]->node);
    EXPECT_FLOAT_EQ(area->rect.x, 6.0f); EXPECT_FLOAT_EQ(area->rect.width, 224.0f);
    EXPECT_FLOAT_EQ(area->rect.y, 60.0f); EXPECT_FLOAT_EQ(area->rect.height, 54.0f);
    EXPECT_FLOAT_EQ(box->rect.y, 64.0f);
    EXPECT_FLOAT_EQ(area->computed_boundary->margin.top, -3.0f);
    EXPECT_FLOAT_EQ(area->computed_boundary->margin.bottom, -4.0f);
    for (LayoutViewNode* child = tree->model->pages.get()[0]->node.first_child; child; child = child->next_sibling)
        EXPECT_NE(child->role, VIEW_FRAGMENT_NOTE);
    EXPECT_EQ(note->parent, body);
    ASSERT_TRUE(create_dir("temp/paged-media-area-sizing"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-area-sizing/late-call.pdf"));
}

TEST_F(SecondaryViewTest, PageAndFootnoteAreaFontsKeepSemanticNoteInheritanceAcrossSheets) {
    init_vector_engine();
    stylesheet("@page { size: 240px 150px; margin: 20px; font: 20px/1.25 Arial; color: purple; "
        "@top-center { content: 'Header'; font: italic bold 8px/2 Arial; letter-spacing: 2px; color: blue } "
        "@footnote { font-size: 50%; font-weight: bold; font-style: italic; line-height: 2; color: blue; "
        "padding-top: 1em; border-top: .25em solid currentColor; margin-top: .5em; max-height: 34px; box-sizing: border-box } } "
        "@page :left { @footnote { font-size: 100%; color: red } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    DomElement* note = block("A\nB\nC\nD", "float: footnote; color: green", "span", body); ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, note, false); ASSERT_NE(state, nullptr);
    size_t count = 0;
    for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) {
        if (box->role != VIEW_FRAGMENT_NOTE || !box->paint_box) continue;
        LayoutViewNode* area = box->parent; ASSERT_NE(area, nullptr); count++;
        EXPECT_FLOAT_EQ(area->computed_boundary->padding.top, count == 1 ? 10.0f : 20.0f);
        EXPECT_FLOAT_EQ(area->computed_boundary->border->width.top, count == 1 ? 2.5f : 5.0f);
        EXPECT_FLOAT_EQ(area->computed_boundary->margin.top, count == 1 ? 5.0f : 10.0f);
        EXPECT_NE(area->computed_style, nullptr);
        if (area->computed_style) {
            EXPECT_FLOAT_EQ(area->computed_style->font.font_size, count == 1 ? 10.0f : 20.0f);
            EXPECT_FLOAT_EQ(area->computed_style->line_height, count == 1 ? 20.0f : 40.0f);
            EXPECT_EQ(area->computed_style->font.font_weight_numeric, 700);
            EXPECT_EQ(area->computed_style->font.font_style, CSS_VALUE_ITALIC);
        }
        EXPECT_FLOAT_EQ(box->computed_style->font.font_size, 10.0f);
        EXPECT_EQ(box->computed_style->color.g, 128); EXPECT_EQ(box->computed_style->color.r, 0);
    }
    EXPECT_EQ(count, 2u);
    LayoutViewNode* header = source_fragment(tree, source, VIEW_FRAGMENT_MARGIN, false); ASSERT_NE(header, nullptr);
    ASSERT_NE(header->computed_style, nullptr);
    EXPECT_FLOAT_EQ(header->computed_style->font.font_size, 8.0f);
    EXPECT_FLOAT_EQ(header->computed_style->line_height, 16.0f);
    EXPECT_FLOAT_EQ(header->computed_style->font.letter_spacing, 2.0f);
    EXPECT_EQ(header->computed_style->font.font_weight_numeric, 700);
    EXPECT_EQ(header->computed_style->font.font_style, CSS_VALUE_ITALIC);
    EXPECT_STREQ(header->computed_style->font.family.get(), "Arial");
    EXPECT_EQ(note->parent, body); EXPECT_FLOAT_EQ(source->x, 11.25f);
    ASSERT_TRUE(create_dir("temp/paged-media-area-text"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-area-text/page-area-fonts.pdf"));
    for (uint32_t page_number = 1; page_number <= 2; page_number++) {
        ImageSurface* surface = render_secondary_page_snapshot(tree, page_number, 1.0f); ASSERT_NE(surface, nullptr);
        EXPECT_EQ(snapshot_pixel(surface, 100, page_number == 1 ? 107 : 71), page_number == 1 ? 0xffff0000u : 0xff0000ffu);
        size_t green_ink = 0;
        for (size_t y = 118; y < 130; y++) for (size_t x = 20; x < 50; x++) {
            Color pixel = {snapshot_pixel(surface, x, y)};
            if (pixel.g > pixel.r + 20 && pixel.g > pixel.b + 20) green_ink++;
        }
        EXPECT_GT(green_ink, 5u);
        save_surface_to_png(surface, page_number == 1 ? "temp/paged-media-area-text/page-area-fonts-native-1.png" :
            "temp/paged-media-area-text/page-area-fonts-native-2.png"); image_surface_destroy(surface);
    }
}

TEST_F(SecondaryViewTest, PageContextsResolveOwnVariablesQuotesAndWhitespace) {
    stylesheet("@page { size: 240px 120px; margin: 20px; font-size: 16px; --label: 'Page'; --size: 8px; "
        "@top-center { --label: 'Line'; --size: 10px; font-size: var(--size); line-height: 1; color: blue; "
        "white-space: pre; quotes: '[' ']'; content: open-quote var(--label) close-quote '\\A Two' } "
        "@footnote { --size: 6px; font-size: var(--size); padding-top: 1em; border-top: 1px solid blue } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px }");
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Note", "float: footnote; color: green", "span", body); ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    LayoutViewNode* header = tree->model->pages.get()[0]->margin_boxes[CSS_PAGE_TOP_CENTER]; ASSERT_NE(header, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(header, text);
    EXPECT_STREQ(text->str, "[Line]Two"); strbuf_free(text);
    LayoutViewNode* first = header->first_child;
    LayoutViewNode* last = header->last_child;
    ASSERT_NE(first, nullptr); ASSERT_NE(last, nullptr); EXPECT_NE(first, last);
    EXPECT_FLOAT_EQ(last->rect.y - first->rect.y, 10.0f);
    EXPECT_EQ(header->computed_style->white_space, CSS_VALUE_PRE);
    EXPECT_FLOAT_EQ(header->computed_style->font.font_size, 10.0f);
    LayoutViewNode* box = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(box, nullptr);
    ASSERT_NE(box->parent->computed_style, nullptr);
    EXPECT_FLOAT_EQ(box->parent->computed_style->font.font_size, 6.0f);
    EXPECT_FLOAT_EQ(box->parent->computed_boundary->padding.top, 6.0f);
    EXPECT_FLOAT_EQ(box->computed_style->font.font_size, 10.0f);
    EXPECT_EQ(note->parent, body);
    ASSERT_TRUE(create_dir("temp/paged-media-area-text"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-area-text/page-contexts.pdf"));
}

TEST_F(SecondaryViewTest, AutomaticNotesCanFollowTheirCallWhenTheAreaHasNoBodyPageBudget) {
    stylesheet("@page { size: 220px 100px; margin: 10px; @footnote { max-height: 0 } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px }");
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    DomElement* note = block("Later note", "float: footnote", "span", body); ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, note, VIEW_FRAGMENT_BODY, false)), 1u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true)), 2u);
}

TEST_F(SecondaryViewTest, RequiredNotesDiagnoseAnAreaLimitThatCannotAdmitTheirFirstSlice) {
    stylesheet("@page { size: 220px 100px; margin: 10px; @footnote { max-height: 24px } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    ASSERT_NE(block("A\nB\nC", "float: footnote; footnote-policy: line", "span", body), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(diagnostic.status, TYPESET_UNPLACEABLE); EXPECT_LE(tree->model->page_count, 2u);
}

TEST_F(SecondaryViewTest, AFloatOnAContinuationPageKeepsTheFootnoteAreaLimit) {
    stylesheet("@page { size: 240px 120px; margin: 10px; @footnote { max-height: 24px } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* body = block("Call "); ASSERT_NE(body, nullptr);
    DomElement* note = block("A\nB\nC\nD\nE\nF\nG\nH", "float: footnote", "span", body); ASSERT_NE(note, nullptr);
    DomElement* top = block("Deferred float", "float: top; float-reference: page; float-defer: 1"); ASSERT_NE(top, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, top, VIEW_FRAGMENT_FLOAT, true)), 2u);
    ViewNodeState* state = view_tree_node_state(tree, note, false); ASSERT_NE(state, nullptr);
    size_t count = 0;
    for (LayoutViewNode* box = state->first_occurrence; box; box = box->next_occurrence) {
        if (box->role != VIEW_FRAGMENT_NOTE || !box->paint_box) continue;
        count++;
        EXPECT_EQ(occurrence_page(box), count);
        EXPECT_FLOAT_EQ(box->rect.height, count < 3 ? 24.0f : 48.0f);
    }
    EXPECT_EQ(count, 3u);
}

TEST_F(SecondaryViewTest, TwoFootnotesShareReservationAndDrainAfterBodyEnds) {
    stylesheet("@page { size: 240px 100px; margin: 10px; @bottom-right { content: counter(page) '/' counter(pages); font-size: 8px } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap; orphans: 1; widows: 1 }");
    DomElement* paragraph = block("Two calls "); ASSERT_NE(paragraph, nullptr);
    DomElement* first = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", "float: footnote; color: blue", "span", paragraph);
    ASSERT_NE(first, nullptr);
    DomElement* second = block("Second note", "float: footnote; color: red", "span", paragraph); ASSERT_NE(second, nullptr);
    DomText* text_source = first->first_child->as_text(); ASSERT_NE(text_source, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    size_t note_fragments = 0;
    ViewNodeState* first_state = view_tree_node_state(tree, first, false); ASSERT_NE(first_state, nullptr);
    for (LayoutViewNode* fragment = first_state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        if (fragment->role != VIEW_FRAGMENT_NOTE || !fragment->paint_box) continue;
        EXPECT_EQ(occurrence_page(fragment), ++note_fragments);
        EXPECT_EQ(fragment->first_fragment, occurrence_page(fragment) == 1);
        EXPECT_EQ(fragment->last_fragment, occurrence_page(fragment) == 2);
    }
    EXPECT_EQ(note_fragments, 2u);
    ViewNodeState* second_state = view_tree_node_state(tree, second, false); ASSERT_NE(second_state, nullptr);
    for (LayoutViewNode* fragment = second_state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (fragment->role == VIEW_FRAGMENT_NOTE) EXPECT_EQ(occurrence_page(fragment), 1u);
    ViewNodeState* text_state = view_tree_node_state(tree, text_source, false); ASSERT_NE(text_state, nullptr);
    size_t* coverage = (size_t*)pool_calloc(input_pool, text_source->length * sizeof(size_t)); ASSERT_NE(coverage, nullptr);
    for (LayoutViewNode* fragment = text_state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        EXPECT_EQ(fragment->role, VIEW_FRAGMENT_NOTE);
        for (size_t i = fragment->text_start; i < fragment->text_start + fragment->text_length; i++) {
            ASSERT_LT(i, text_source->length); coverage[i]++;
        }
    }
    for (size_t i = 0; i < text_source->length; i++) EXPECT_EQ(coverage[i], 1u) << i;
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/footnotes.pdf"));
}

TEST_F(SecondaryViewTest, UnsplittableFootnoteReturnsAnUnplaceableDiagnostic) {
    stylesheet("@page { size: 200px 100px; margin: 10px } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap }");
    DomElement* paragraph = block("Call "); ASSERT_NE(paragraph, nullptr);
    ASSERT_NE(block("A\nB\nC\nD\nE\nF\nG\nH", "float: footnote; footnote-policy: line", "span", paragraph), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(diagnostic.status, TYPESET_UNPLACEABLE); EXPECT_NE(diagnostic.reason, nullptr);
    EXPECT_LE(tree->model->page_count, 2u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, PublishingFunctionsIgnoreKeywordCaseAndPreserveCustomNames) {
    stylesheet("@page { size: 220px 120px; margin: 20px; "
        "@top-left { content: STRING(Header, LAST); font-size: 8px } "
        "@top-right { content: string(header); font-size: 8px } "
        "@top-center { content: ELEMENT(Furniture, FIRST) } "
        "@bottom-left { content: 'Page ' COUNTER(page); font-size: 8px } } "
        "p, div { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("Furniture Text", "position: RUNNING(Furniture)"), nullptr);
    ASSERT_NE(block("Chapter", "string-set: Header CONTENT(TEXT)"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    const CssPageMarginBox boxes[] = {CSS_PAGE_TOP_LEFT, CSS_PAGE_TOP_RIGHT, CSS_PAGE_TOP_CENTER, CSS_PAGE_BOTTOM_LEFT};
    const char* expected[] = {"Chapter", "", "Furniture Text", "Page 1"};
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    for (size_t i = 0; i < 4; i++) {
        ASSERT_NE(tree->model->pages.get()[0]->margin_boxes[boxes[i]], nullptr);
        strbuf_reset(text); append_fragment_text(tree->model->pages.get()[0]->margin_boxes[boxes[i]], text);
        EXPECT_STREQ(text->str ? text->str : "", expected[i]);
    }
    strbuf_free(text);
}

TEST_F(SecondaryViewTest, MarginBoxesRenderPageTotalsInTheirOwnViewFragments) {
    stylesheet("@page { size: 240px 160px; margin: 24px; "
        "@top-left { content: \"Title\"; font-size: 9px; color: red } "
        "@top-center { content: \"Centered\"; font-size: 9px } "
        "@top-right { content: \"Right label\"; font-size: 9px } "
        "@bottom-center { content: \"Page \" counter(page) \" / \" counter(pages); font-size: 9px; color: blue } } "
        "p, div { margin: 0; font-size: 12px; line-height: 16px } div + div { page-break-before: always }");
    ASSERT_NE(block("First page."), nullptr);
    ASSERT_NE(block("Second page."), nullptr);
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    for (size_t i = 0; i < 2; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        LayoutViewNode* footer = page->margin_boxes[CSS_PAGE_BOTTOM_CENTER];
        ASSERT_NE(footer, nullptr);
        EXPECT_EQ(footer->role, VIEW_FRAGMENT_MARGIN);
        EXPECT_EQ(footer->source.address, source);
        EXPECT_FLOAT_EQ(footer->rect.x, 24.0f);
        EXPECT_FLOAT_EQ(footer->rect.width, 192.0f);
        EXPECT_FLOAT_EQ(footer->rect.y, 136.0f);
        StrBuf* text = strbuf_new(); append_fragment_text(footer, text);
        EXPECT_STREQ(text->str, i ? "Page 2 / 2" : "Page 1 / 2");
        strbuf_free(text);
        LayoutViewNode* center = page->margin_boxes[CSS_PAGE_TOP_CENTER];
        ASSERT_NE(center, nullptr);
        EXPECT_NEAR(center->rect.x + center->rect.width * 0.5f, 120.0f, 0.001f);
        EXPECT_FLOAT_EQ(page->content_rect.height, 112.0f);
    }
    EXPECT_EQ(source->first_child->next_sibling->next_sibling, nullptr);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    ASSERT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/margin-boxes.pdf"));
}

TEST_F(SecondaryViewTest, LogicalPageCountersIncludeBlankSheetsAndIsolateMarginScopes) {
    stylesheet("@page { size: 240px 120px; margin: 20px; counter-increment: page 2 Chapter; "
        "counter-set: pages -9; @bottom-center { content: counter(page) '/' counter(pages) '|' counter(Chapter,upper-roman); font-size: 8px } "
        "@top-left { counter-increment: page; content: counter(page); font-size: 8px } } "
        "@page:first { counter-reset: page 0 Chapter 0 pages 17; @top-left { counter-reset: page 98 } } "
        "@page chapter { counter-reset: page 8; counter-increment: page 0 Chapter } "
        "@page chapter:blank { counter-reset: none; counter-increment: page 2 Chapter } "
        "p, div { margin: 0; font-size: 10px; line-height: 12px } p { counter-reset: Chapter 90 }");
    ASSERT_NE(block("Opening"), nullptr);
    ASSERT_NE(block("Chapter", "page: chapter; break-before: right"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    const char* footers[] = {"2/3|I", "4/3|II", "8/3|III"};
    const char* headers[] = {"99", "100", "101"};
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    for (size_t i = 0; i < 3; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        EXPECT_EQ(page->page_number, i + 1); EXPECT_EQ(page->blank, i == 1);
        ASSERT_NE(page->margin_boxes[CSS_PAGE_BOTTOM_CENTER], nullptr);
        strbuf_reset(text); append_fragment_text(page->margin_boxes[CSS_PAGE_BOTTOM_CENTER], text);
        EXPECT_STREQ(text->str, footers[i]);
        ASSERT_NE(page->margin_boxes[CSS_PAGE_TOP_LEFT], nullptr);
        strbuf_reset(text); append_fragment_text(page->margin_boxes[CSS_PAGE_TOP_LEFT], text);
        EXPECT_STREQ(text->str, headers[i]);
    }
    strbuf_free(text);
}

TEST_F(SecondaryViewTest, MarginCounterDeclarationsAdvanceBeforeContentIsGenerated) {
    stylesheet("@page { size:240px 120px; margin:20px; @top-left { counter-increment: Seq Other } } "
        "@page:first { @top-left { counter-reset: Seq 10 Other 20 } } "
        "@page later { @top-left { content: counter(Seq) '/' counter(Other); font-size:8px } } "
        "p,div { margin:0; font-size:10px; line-height:12px } div + div { break-before:page; page:later }");
    ASSERT_NE(block("First"), nullptr); ASSERT_NE(block("Second"), nullptr); ASSERT_NE(block("Third"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_EQ(tree->model->pages.get()[0]->margin_boxes[CSS_PAGE_TOP_LEFT], nullptr);
    for (size_t i = 1; i < 3; i++) {
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        ASSERT_NE(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_LEFT], nullptr);
        append_fragment_text(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_LEFT], text);
        EXPECT_STREQ(text->str, i == 1 ? "12/22" : "13/23"); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, PageCounterSetZeroAndBoundedIncrementsKeepReadOnlyTotals) {
    stylesheet("@page { size:240px 120px; margin:20px; counter-increment:none; "
        "@bottom-center { content: counter(page) '/' counter(pages); font-size:8px } } "
        "@page:first { counter-reset:page 2147483647 } "
        "@page negative { counter-set:page -2 pages 99 } "
        "@page zero { counter-reset:page 0; counter-increment:page 0 } "
        "p,div { margin:0; font-size:10px; line-height:12px } div + div { break-before:page }");
    ASSERT_NE(block("Maximum"), nullptr); ASSERT_NE(block("Still maximum"), nullptr);
    ASSERT_NE(block("Negative", "page:negative"), nullptr); ASSERT_NE(block("Zero", "page:zero"), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 4u);
    const char* expected[] = {"2147483647/4", "2147483647/4", "-2/4", "0/4"};
    for (size_t i = 0; i < 4; i++) {
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        ASSERT_NE(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_BOTTOM_CENTER], nullptr);
        append_fragment_text(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_BOTTOM_CENTER], text);
        EXPECT_STREQ(text->str, expected[i]); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, TargetPageCountersRetainLogicalLabelsAndPhysicalIdentity) {
    stylesheet("@page { size: 240px 120px; margin: 20px; counter-increment: page -1 } "
        "@page:first { counter-reset: page 11 } "
        "@media (max-width: 200px) { @page:first { counter-reset: page 21 } } "
        "p, div, a { margin: 0; font-size: 10px; line-height: 12px } a { display: block } "
        "a::after { content: target-counter(attr(href),page) '/' target-counters(attr(href),page,'.',upper-roman) }");
    DomElement* link = block("", nullptr, "a"); ASSERT_NE(link, nullptr);
    ASSERT_TRUE(link->set_attribute("href", "#target"));
    DomElement* target = block("Target", "break-before: page"); ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->set_attribute("id", "target"));
    ViewTree* first = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(first, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED); environment.viewport_width = 180.0f;
    ViewTree* second = view_tree_secondary_create(&doc, &environment); ASSERT_NE(second, nullptr);
    ASSERT_EQ(layout_secondary_view(second, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    for (ViewTree* tree : {first, second}) {
        ASSERT_EQ(tree->model->page_count, 2u);
        ASSERT_NE(layout_secondary_target(tree, "target"), nullptr);
        EXPECT_EQ(layout_secondary_target(tree, "target")->page_number, 2u);
        ViewNodeState* state = view_tree_node_state(tree, link, false); ASSERT_NE(state, nullptr);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence)
            if (node->glyph_run) append_fragment_text(node, text);
        EXPECT_STREQ(text->str, tree == first ? "9/IX" : "19/XIX"); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, NamedStringsUsePhysicalPageSnapshotsAndSurviveBlankPages) {
    stylesheet("@page { size: 240px 100px; margin: 20px; "
        "@top-left { content: string(chapter, first); font-size: 8px } "
        "@top-center { content: string(chapter, start); font-size: 8px } "
        "@top-right { content: string(chapter, last); font-size: 8px } "
        "@bottom-left { content: string(chapter, first-except); font-size: 8px } "
        "@bottom-right { content: string(label); font-size: 8px } } "
        "p, div, h2 { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap } "
        "h2 { string-set: chapter content(), label attr(data-title) }");
    DomElement* alpha = block("Alpha", nullptr, "h2");
    ASSERT_NE(alpha, nullptr); ASSERT_TRUE(alpha->set_attribute("data-title", "A"));
    ASSERT_NE(block("Body"), nullptr);
    DomElement* beta = block("Beta", nullptr, "h2");
    ASSERT_NE(beta, nullptr); ASSERT_TRUE(beta->set_attribute("data-title", "B"));
    ASSERT_NE(block("Second page", "break-before: page"), nullptr);
    DomElement* gamma = block("C\nD\nE\nF\nG\nH\nI\nJ\nK\nL",
        "break-before: left; string-set: chapter attr(data-title), label \"C\"", "h2");
    ASSERT_NE(gamma, nullptr); ASSERT_TRUE(gamma->set_attribute("data-title", "Gamma"));
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 5u);
    const char* expected[5][5] = {{"Alpha", "Alpha", "Beta", "", "A"},
        {"Beta", "Beta", "Beta", "Beta", "B"}, {"Beta", "Beta", "Beta", "Beta", "B"},
        {"Gamma", "Gamma", "Gamma", "", "C"}, {"Gamma", "Gamma", "Gamma", "Gamma", "C"}};
    const CssPageMarginBox boxes[] = {CSS_PAGE_TOP_LEFT, CSS_PAGE_TOP_CENTER, CSS_PAGE_TOP_RIGHT,
        CSS_PAGE_BOTTOM_LEFT, CSS_PAGE_BOTTOM_RIGHT};
    StrBuf* text = strbuf_new();
    ASSERT_NE(text, nullptr);
    for (size_t i = 0; i < 5; i++) for (size_t j = 0; j < 5; j++) {
        LayoutViewNode* box = tree->model->pages.get()[i]->margin_boxes[boxes[j]];
        ASSERT_NE(box, nullptr);
        strbuf_reset(text); append_fragment_text(box, text);
        EXPECT_STREQ(text->str ? text->str : "", expected[i][j]) << "page " << i + 1 << " box " << j;
    }
    strbuf_free(text);
    EXPECT_TRUE(tree->model->pages.get()[2]->blank);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/running-strings.pdf"));
}

TEST_F(SecondaryViewTest, HiddenNamedStringsHaveNoBodyHeightAndInlineAssignmentsKeepStartValue) {
    stylesheet("@page { size: 200px 100px; margin: 20px; "
        "@top-left { content: string(title, start); font-size: 8px } "
        "@top-right { content: string(title, last); font-size: 8px } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("Invisible", "display: none; string-set: title content()"), nullptr);
    DomElement* line = block("Body "); ASSERT_NE(line, nullptr);
    MarkBuilder builder(input);
    DomElement* span = DomElement::create(&doc, "span", builder.element("span").final().element);
    ASSERT_NE(span, nullptr); ASSERT_TRUE(line->DomNode::append_child(span));
    ASSERT_TRUE(span->set_attribute("style", "string-set: title \"Inline\""));
    DomText* child = DomText::create_copy("tail", 4, span);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(span->DomNode::append_child(child));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    append_fragment_text(tree->model->pages.get()[0]->margin_boxes[CSS_PAGE_TOP_LEFT], text);
    EXPECT_STREQ(text->str, "Invisible");
    strbuf_reset(text);
    append_fragment_text(tree->model->pages.get()[0]->margin_boxes[CSS_PAGE_TOP_RIGHT], text);
    EXPECT_STREQ(text->str, "Inline");
    strbuf_free(text);
    EXPECT_FLOAT_EQ(view_tree_node_state(tree, line, false)->first_occurrence->rect.y, 20.0f);
    EXPECT_EQ(line->first_child->next_sibling, span);
}

TEST_F(SecondaryViewTest, RunningElementsPreserveInlineStylesAndNestedBlocksWithoutBodySpace) {
    stylesheet("@page { size: 240px 140px; margin: 30px; @top-center { content: element(header) } } "
        "p, div, span { margin: 0; font-size: 10px; line-height: 12px } "
        ".header { position: running(header); text-align: center } "
        ".emphasis { font-size: 14px; color: red; font-style: italic }");
    DomElement* header = block("Book "); ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->set_attribute("class", "header"));
    MarkBuilder builder(input);
    DomElement* span = DomElement::create(&doc, "span", builder.element("span").final().element);
    ASSERT_NE(span, nullptr); ASSERT_TRUE(header->DomNode::append_child(span));
    ASSERT_TRUE(span->set_attribute("class", "emphasis"));
    DomText* emphasis = DomText::create_copy("Title", 5, span);
    ASSERT_NE(emphasis, nullptr); ASSERT_TRUE(span->DomNode::append_child(emphasis));
    DomElement* subtitle = DomElement::create(&doc, "div", builder.element("div").final().element);
    ASSERT_NE(subtitle, nullptr); ASSERT_TRUE(header->DomNode::append_child(subtitle));
    DomText* subtitle_text = DomText::create_copy("Subtitle", 8, subtitle);
    ASSERT_NE(subtitle_text, nullptr); ASSERT_TRUE(subtitle->DomNode::append_child(subtitle_text));
    DomElement* body = block("A\nB\nC\nD\nE\nF\nG", "white-space: pre-wrap");
    ASSERT_NE(body, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_EQ(header->first_child->next_sibling, span);
    EXPECT_EQ(span->parent, header);
    EXPECT_EQ(subtitle->parent, header);
    EXPECT_FLOAT_EQ(view_tree_node_state(tree, body, false)->first_occurrence->rect.y, 30.0f);
    ViewNodeState* state = view_tree_node_state(tree, emphasis, false); ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->occurrence_count, 2u);
    for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) {
        EXPECT_EQ(node->role, VIEW_FRAGMENT_RUNNING);
        ASSERT_NE(node->glyph_run, nullptr);
        EXPECT_EQ(node->glyph_run->color.r, 255); EXPECT_EQ(node->glyph_run->color.g, 0);
        EXPECT_FLOAT_EQ(node->glyph_run->font_size, 14.0f);
        EXPECT_TRUE(node->glyph_run->italic);
    }
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    for (size_t i = 0; i < 2; i++) {
        LayoutViewNode* box = tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER];
        ASSERT_NE(box, nullptr); strbuf_reset(text); append_fragment_text(box, text);
        EXPECT_STREQ(text->str, "Book TitleSubtitle");
        EXPECT_EQ(occurrence_page(box), i + 1);
    }
    strbuf_free(text);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/running-elements.pdf"));
}

TEST_F(SecondaryViewTest, InitialStringResetAndMovedHeadingBindAfterPageSelection) {
    stylesheet("@page { size: 180px 100px; margin: 20px; @top-center { content: string(title); font-size: 8px } } "
        "p, div, h2 { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap } "
        "h2 { string-set: title attr(data-title) }");
    ASSERT_NE(block("A\nB\nC\nD", "string-set: initial"), nullptr);
    DomElement* heading = block("E\nF\nG", nullptr, "h2");
    ASSERT_NE(heading, nullptr); ASSERT_TRUE(heading->set_attribute("data-title", "Moved"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, heading->first_child, false)->first_occurrence), 2u);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    append_fragment_text(tree->model->pages.get()[0]->margin_boxes[CSS_PAGE_TOP_CENTER], text);
    EXPECT_EQ(text->length, 0u);
    strbuf_reset(text);
    append_fragment_text(tree->model->pages.get()[1]->margin_boxes[CSS_PAGE_TOP_CENTER], text);
    EXPECT_STREQ(text->str, "Moved"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, NativeSpaceGlyphWithoutDomSourceExportsNoInk) {
    ViewTree* tree = secondary();
    ViewCssStyle* style = view_css_resolve(tree, source); ASSERT_NE(style, nullptr);
    ViewPageBox* page = view_tree_page_append(tree, 200.0f, 120.0f,
        {0.0f, 0.0f, 200.0f, 120.0f}, VIEW_PAGE_RIGHT); ASSERT_NE(page, nullptr);
    LayoutViewNode* node = view_tree_fragment_append(tree, &page->node, nullptr,
        {10.0f, 10.0f, 20.0f, 20.0f}); ASSERT_NE(node, nullptr);
    uint32_t* glyph = (uint32_t*)arena_alloc(tree->model->arena, sizeof(uint32_t));
    float* position = (float*)arena_alloc(tree->model->arena, sizeof(float));
    PaintGlyphRun* run = (PaintGlyphRun*)arena_calloc(tree->model->arena, sizeof(PaintGlyphRun));
    ASSERT_NE(glyph, nullptr); ASSERT_NE(position, nullptr); ASSERT_NE(run, nullptr);
    *glyph = font_get_glyph_index(style->font.font_handle, ' '); ASSERT_NE(*glyph, 0u);
    *position = 0.0f; run->font = lam::up(&style->font_box); run->font_size = style->font.font_size;
    run->count = 1; run->glyph_ids = lam::up(glyph); run->xs = run->ys = lam::up(position);
    run->x = 10.0f; run->baseline_y = 25.0f; run->color = style->color;
    node->glyph_run = lam::up(run);
    ASSERT_TRUE(view_tree_model_commit(tree));
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/space-glyph.pdf"));
}

TEST_F(SecondaryViewTest, WidowsSelectEarlierBoundaryAndOrphansMoveParagraph) {
    stylesheet("@page { size: 180px 80px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap }");
    ASSERT_NE(block(nullptr, "height: 12px"), nullptr);
    DomElement* paragraph = block("A\nB\nC\nD\nE");
    ASSERT_NE(paragraph, nullptr);
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK);
    EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, paragraph->first_child, false);
    ASSERT_NE(state, nullptr);
    uint32_t ink[2] = {};
    for (LayoutViewNode* occurrence = state->first_occurrence; occurrence; occurrence = occurrence->next_occurrence)
        if (occurrence->glyph_run) ink[occurrence_page(occurrence) - 1]++;
    EXPECT_EQ(ink[0], 3u); EXPECT_EQ(ink[1], 2u);
    // the alternate tree sees one line of space but leaves the paragraph intact.
    ASSERT_TRUE(source->first_child->as_element()->set_attribute("style", "height: 48px"));
    ViewTree* other = secondary();
    ASSERT_EQ(layout_secondary_view(other, &options, &diagnostic), TYPESET_OK);
    state = view_tree_node_state(other, paragraph->first_child, false);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(occurrence_page(state->first_occurrence), 2u);
    EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
}

TEST_F(SecondaryViewTest, KeepInsideAndHeadingWithFollowingLinesMoveBeforeCommit) {
    stylesheet("@page { size: 180px 80px; margin: 10px } p, div, h2 { margin: 0; font-size: 10px; line-height: 12px; white-space: pre-wrap }");
    ASSERT_NE(block(nullptr, "height: 48px"), nullptr);
    DomElement* keep = block("A\nB\nC", "break-inside: avoid-page");
    ASSERT_NE(keep, nullptr);
    DomElement* heading = block("Heading", "break-after: avoid-page", "h2");
    ASSERT_NE(heading, nullptr);
    DomElement* paragraph = block("A\nB\nC\nD");
    ASSERT_NE(paragraph, nullptr);
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK);
    EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, keep->first_child, false)->first_occurrence), 2u);
    uint32_t heading_page = occurrence_page(view_tree_node_state(tree, heading->first_child, false)->first_occurrence);
    EXPECT_EQ(heading_page, 3u);
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, paragraph->first_child, false)->first_occurrence), heading_page);
}

TEST_F(SecondaryViewTest, SiblingKeepChainBacktracksBeforeLateFootnoteReservation) {
    stylesheet("@page { size: 200px 100px; margin: 10px; @top-center { content: string(Title, last) } } "
        "p, div, h2, span { margin: 0; font-size: 10px; line-height: 12px; orphans: 1; widows: 1 }");
    ASSERT_NE(block("Prelude", "height: 24px; string-set: Title 'Prelude'"), nullptr);
    DomElement* headings[3] = {};
    const char* names[] = {"First", "Second", "Third"};
    const char* styles[] = {"break-after: avoid; string-set: Title 'First'",
        "break-after: avoid; string-set: Title 'Second'", "break-after: avoid; string-set: Title 'Third'"};
    for (size_t i = 0; i < 3; i++) {
        headings[i] = block(names[i], styles[i], "h2"); ASSERT_NE(headings[i], nullptr);
    }
    ASSERT_TRUE(headings[2]->set_attribute("id", "third"));
    DomElement* paragraph = block("Call "); ASSERT_NE(paragraph, nullptr);
    DomElement* note = block("Note A\nNote B", "float: footnote; footnote-policy: line; white-space: pre-wrap", "span", paragraph);
    ASSERT_NE(note, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
    for (size_t i = 0; i < 3; i++) {
        ViewNodeState* state = view_tree_node_state(tree, headings[i]->first_child, false); ASSERT_NE(state, nullptr);
        ASSERT_NE(state->first_occurrence, nullptr); EXPECT_EQ(occurrence_page(state->first_occurrence), 2u);
        EXPECT_EQ(state->first_occurrence->next_occurrence, nullptr);
    }
    EXPECT_EQ(occurrence_page(source_fragment(tree, paragraph->first_child, VIEW_FRAGMENT_BODY, false)), 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true)), 2u);
    const TypesetTarget* target = layout_secondary_target(tree, "third"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->page_number, 2u);
    const char* titles[] = {"Prelude", "Third"};
    for (size_t i = 0; i < 2; i++) {
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
        append_fragment_text(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER], text);
        EXPECT_STREQ(text->str, titles[i]); strbuf_free(text);
    }
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
    ASSERT_TRUE(create_dir("temp/paged-media-whole-page"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-whole-page/keep-chain.pdf"));
}

TEST_F(SecondaryViewTest, ImpossibleLineReturnsDiagnosticWithoutPageBudgetLoop) {
    stylesheet("@page { size: 120px 20px; margin: 0 } p { margin: 0; font-size: 10px; line-height: 30px }");
    ASSERT_NE(block("A"), nullptr);
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed);
    EXPECT_LE(tree->model->page_count, 2u);
    EXPECT_EQ(diagnostic.source.address, source->first_child);
    EXPECT_NE(diagnostic.reason, nullptr);
    EXPECT_FALSE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/incomplete.pdf"));
}

TEST_F(SecondaryViewTest, ClonedPaddingCannotConsumeEveryFreshPageForever) {
    stylesheet("@page { size: 120px 40px; margin: 10px } "
        "p { margin: 0; padding-top: 50px; box-decoration-break: clone } div { height: 40px }");
    DomElement* extent = block(nullptr); ASSERT_NE(extent, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed);
    EXPECT_LE(tree->model->page_count, 2u);
    EXPECT_EQ(diagnostic.source.address, extent);
    EXPECT_STREQ(diagnostic.reason, "block extent cannot make progress after reopening page decorations");
}

TEST_F(SecondaryViewTest, NamedFirstPageAndReturnToAutoDoNotLeakTemplates) {
    stylesheet("@page { size: 180px 120px; margin: 10px } @page chapter { size: 220px 140px; margin: 20px } "
        "p, div { margin: 0; font-size: 10px; line-height: 12px }");
    ASSERT_NE(block("Chapter", "page: chapter; break-before: page"), nullptr);
    ASSERT_NE(block("Ordinary page"), nullptr);
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_STREQ(tree->model->pages.get()[0]->name, "chapter");
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->node.rect.width, 220.0f);
    EXPECT_EQ(tree->model->pages.get()[1]->name, nullptr);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[1]->node.rect.width, 180.0f);
}

TEST_F(SecondaryViewTest, LeftStartUsesOneBlankAndConsumesForcedBreakOnce) {
    stylesheet("@page { size: 180px 120px; margin: 10px } p, div { margin: 0; font-size: 10px; line-height: 12px }");
    DomElement* first = block("A\nB\nC\nD\nE\nF\nG\nH\nI", "page-break-before: left; white-space: pre-wrap");
    ASSERT_NE(first, nullptr);
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_TRUE(tree->model->pages.get()[0]->blank);
    EXPECT_EQ(tree->model->pages.get()[1]->side, VIEW_PAGE_LEFT);
    EXPECT_FALSE(tree->model->pages.get()[2]->blank);
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, first->first_child, false)->first_occurrence), 2u);
}

TEST_F(SecondaryViewTest, SharedSourceHasIndependentStateAndFragments) {
    ViewTree* a = secondary();
    ViewTree* b = secondary(VIEW_PRESENTATION_CONTINUOUS);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ViewPageBox* p1 = view_tree_page_append(a, 100.0f, 200.0f,
        {0.0f, 0.0f, 100.0f, 200.0f}, VIEW_PAGE_RIGHT);
    ViewPageBox* p2 = view_tree_page_append(a, 150.0f, 200.0f,
        {0.0f, 0.0f, 150.0f, 200.0f}, VIEW_PAGE_LEFT);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    LayoutViewNode* f1 = view_tree_fragment_append(a, &p1->node, source,
        {1.5f, 2.25f, 80.0f, 44.0f}, 0, 8);
    LayoutViewNode* f2 = view_tree_fragment_append(a, &p2->node, source,
        {1.5f, 2.25f, 120.0f, 22.0f}, 8, 4);
    LayoutViewNode* wide = view_tree_fragment_append(b, b->model->root, source,
        {0.0f, 0.0f, 500.0f, 22.0f}, 0, 12);
    ASSERT_NE(f1, nullptr);
    ASSERT_NE(f2, nullptr);
    ASSERT_NE(wide, nullptr);
    ASSERT_TRUE(view_tree_model_commit(a));
    ASSERT_TRUE(view_tree_model_commit(b));
    ViewNodeState* a_state = view_tree_node_state(a, source, false);
    ViewNodeState* b_state = view_tree_node_state(b, source, false);
    ASSERT_NE(a_state, nullptr);
    ASSERT_NE(b_state, nullptr);
    EXPECT_NE(a_state, b_state);
    EXPECT_EQ(a_state->occurrence_count, 2u);
    EXPECT_EQ(b_state->occurrence_count, 1u);
    EXPECT_EQ(f1->next_occurrence, f2);
    EXPECT_EQ(f1->source.address, source);
    EXPECT_EQ(f2->source.address, source);
    EXPECT_EQ(view_tree_node_resolve(b, f1->ref), nullptr);
    EXPECT_EQ(source->parent, nullptr);
    EXPECT_FLOAT_EQ(source->x, 11.25f);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    LayoutViewRef saved = wide->ref;
    ASSERT_TRUE(view_tree_secondary_release(&doc, a));
    EXPECT_EQ(view_tree_node_resolve(b, saved), wide);
    EXPECT_EQ(doc.view_tree->root, source);
}

TEST_F(SecondaryViewTest, PercentageHeightsUseDefiniteContentBoxesAndPreserveAutoParents) {
    stylesheet("@page { size: 160px 200px; margin: 10px } p, div { margin: 0; font: 10px/10px Arial }");
    DomElement* automatic = block(nullptr); ASSERT_NE(automatic, nullptr);
    DomElement* auto_child = block(nullptr, "height: calc(50% + 2px); background: blue", "div", automatic);
    ASSERT_NE(auto_child, nullptr);
    ASSERT_NE(block(nullptr, "height: 10px", "div", auto_child), nullptr);
    DomElement* fixed = block(nullptr, "height: 40px"); ASSERT_NE(fixed, nullptr);
    DomElement* fixed_child = block(nullptr, "height: 50%", "div", fixed); ASSERT_NE(fixed_child, nullptr);
    DomElement* border = block(nullptr, "height: 40px; box-sizing: border-box; padding: 10px 0");
    ASSERT_NE(border, nullptr);
    DomElement* border_child = block(nullptr, "height: 50%", "div", border); ASSERT_NE(border_child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    DomElement* children[] = {auto_child, fixed_child, border_child};
    const float heights[] = {10.0f, 20.0f, 10.0f};
    for (size_t i = 0; i < 3; i++) {
        LayoutViewNode* fragment = source_fragment(tree, children[i], VIEW_FRAGMENT_BODY, true);
        ASSERT_NE(fragment, nullptr); EXPECT_FLOAT_EQ(fragment->rect.height, heights[i]);
    }
}

TEST_F(SecondaryViewTest, RootPercentageHeightRetainsTheFirstPageAreaAcrossDifferentSheets) {
    stylesheet("@page { size: 120px 100px; margin: 10px } @page :left { size: 120px 180px } "
        "p { margin: 0; height: 50% } div { height: 50%; margin: 0 }");
    DomElement* first = block(nullptr); ASSERT_NE(first, nullptr);
    DomElement* second = block(nullptr, "break-before: page"); ASSERT_NE(second, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->content_rect.height, 80.0f);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[1]->content_rect.height, 160.0f);
    DomElement* children[] = {first, second};
    for (DomElement* child : children) {
        LayoutViewNode* fragment = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true);
        ASSERT_NE(fragment, nullptr); EXPECT_FLOAT_EQ(fragment->rect.height, 20.0f);
    }
}

TEST_F(SecondaryViewTest, ReplacedPercentageHeightsUseTheBlockContainerIncludingZeroAndAuto) {
    stylesheet("@page { size: 160px 200px; margin: 10px } p, div { margin: 0; font: 10px/10px Arial }");
    DomElement* fixed = block(nullptr, "height: 40px"); ASSERT_NE(fixed, nullptr);
    DomElement* atomic = image("display: block; width: 10px; height: 50%", fixed); ASSERT_NE(atomic, nullptr);
    DomElement* span = block(nullptr, "height: 100px", "span", fixed); ASSERT_NE(span, nullptr);
    DomElement* inline_image = image("width: 10px; height: 50%; vertical-align: top", span);
    ASSERT_NE(inline_image, nullptr);
    DomElement* zero = block(nullptr, "height: 0"); ASSERT_NE(zero, nullptr);
    DomElement* zero_image = image("display: block; width: 10px; height: 50%", zero);
    ASSERT_NE(zero_image, nullptr);
    DomElement* automatic = block(nullptr); ASSERT_NE(automatic, nullptr);
    DomElement* auto_image = image("display: block; width: 10px; height: 50%; min-height: 60%; max-height: 70%", automatic);
    ASSERT_NE(auto_image, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    DomElement* images[] = {atomic, inline_image, zero_image, auto_image};
    const float heights[] = {20.0f, 20.0f, 0.0f, 5.0f};
    for (size_t i = 0; i < 4; i++) {
        LayoutViewNode* fragment = source_fragment(tree, images[i], VIEW_FRAGMENT_BODY, true);
        ASSERT_NE(fragment, nullptr); EXPECT_FLOAT_EQ(fragment->rect.height, heights[i]);
    }
}

TEST_F(SecondaryViewTest, MediaCascadeAndLengthsBelongToEachView) {
    stylesheet("p { font-size: 18px; color: red; margin: 8px 12px; width: calc(50vw - 10px); "
        "line-height: 1.5; --space: 3px; padding: var(--space); break-before: page; } "
        "@media (max-width: 400px) { p { color: green; font-size: 14px; } } "
        "@media print { p { color: blue; font-size: 20px; margin-left: 30px; page-break-before: always; } }");
    ViewEnvironment wide_environment = view_environment_default(VIEW_PRESENTATION_CONTINUOUS);
    wide_environment.viewport_width = 800.0f;
    ViewEnvironment narrow_environment = wide_environment;
    narrow_environment.viewport_width = 320.0f;
    ViewTree* wide = view_tree_secondary_create(&doc, &wide_environment);
    ViewTree* narrow = view_tree_secondary_create(&doc, &narrow_environment);
    ViewTree* print = secondary();
    ASSERT_NE(wide, nullptr);
    ASSERT_NE(narrow, nullptr);
    ASSERT_NE(print, nullptr);
    StyleTree* browsing_style = source->specified_style;
    ViewCssStyle* a = view_css_resolve(wide, source);
    ViewCssStyle* b = view_css_resolve(narrow, source);
    ViewCssStyle* c = view_css_resolve(print, source);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(a->font.font_size, 18.0f);
    EXPECT_FLOAT_EQ(b->font.font_size, 14.0f);
    EXPECT_FLOAT_EQ(c->font.font_size, 20.0f);
    EXPECT_EQ(a->color.r, 255);
    EXPECT_EQ(b->color.g, 128);
    EXPECT_EQ(c->color.b, 255);
    EXPECT_FLOAT_EQ(a->line_height, 27.0f);
    EXPECT_FLOAT_EQ(view_css_length(wide, a, a->width, CSS_PROPERTY_WIDTH, 150.0f, 100.0f), 390.0f);
    EXPECT_FLOAT_EQ(view_css_length(narrow, b, b->width, CSS_PROPERTY_WIDTH, 150.0f, 100.0f), 150.0f);
    EXPECT_FLOAT_EQ(view_css_length(print, c, c->margin[3], CSS_PROPERTY_MARGIN_LEFT, 100.0f, 100.0f), 30.0f);
    EXPECT_FLOAT_EQ(view_css_length(wide, a, a->padding[0], CSS_PROPERTY_PADDING_TOP, 100.0f, 100.0f), 3.0f);
    EXPECT_EQ(c->break_before, VIEW_BREAK_PAGE);
    EXPECT_EQ(source->font, nullptr);
    EXPECT_EQ(source->specified_style, browsing_style);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    CssEngine* default_engine = (CssEngine*)doc.services.cached_css_engine;
    EXPECT_FALSE(default_engine->context.print_media);
    EXPECT_DOUBLE_EQ(default_engine->context.viewport_width, 1000.0);
    narrow->reset_retained();
    EXPECT_EQ(view_css_resolve(wide, source), a);
    EXPECT_EQ(view_css_resolve(print, source), c);
}

TEST_F(SecondaryViewTest, SizeContainerConditionsAndUnitsUseIndependentEditionGeometry) {
    stylesheet("@page{size:600px 500px;margin:0}p{margin:0}"
        "#container{container:card / size;width:50vw;height:40px}"
        "#target{width:20px;height:10px}#units{width:50cqw;height:10cqh}"
        "@container card (width > 200px){#target{width:100px}}"
        "@media print{#container{width:120px}}");
    DomElement* container = identified_block("container"); ASSERT_NE(container, nullptr);
    DomElement* target = identified_block("target", container); ASSERT_NE(target, nullptr);
    DomElement* units = identified_block("units", container); ASSERT_NE(units, nullptr);
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_CONTINUOUS);
    environment.viewport_width = 800.0f;
    ViewTree* wide = view_tree_secondary_create(&doc, &environment);
    environment.viewport_width = 320.0f;
    ViewTree* narrow = view_tree_secondary_create(&doc, &environment);
    environment.presentation = VIEW_PRESENTATION_PAGED;
    environment.print_media = true;
    ViewTree* print = view_tree_secondary_create(&doc, &environment);
    ViewTree* trees[] = {wide, narrow, print};
    const float expected_targets[] = {100.0f, 20.0f, 20.0f};
    const float expected_units[] = {200.0f, 80.0f, 60.0f};
    PagedLayoutOptions options = paged_layout_options_default();
    for (size_t index = 0; index < 3; index++) {
        SCOPED_TRACE(index);
        ViewTree* tree = trees[index]; ASSERT_NE(tree, nullptr);
        PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ViewNodeState* target_state = view_tree_node_state(tree, target, false);
        ViewNodeState* unit_state = view_tree_node_state(tree, units, false);
        ASSERT_NE(target_state, nullptr); ASSERT_NE(unit_state, nullptr);
        ASSERT_NE(target_state->first_occurrence, nullptr); ASSERT_NE(unit_state->first_occurrence, nullptr);
        EXPECT_FLOAT_EQ(target_state->first_occurrence->rect.width, expected_targets[index]);
        EXPECT_FLOAT_EQ(unit_state->first_occurrence->rect.width, expected_units[index]);
        EXPECT_FLOAT_EQ(unit_state->first_occurrence->rect.height, 4.0f);
    }
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    EXPECT_EQ(source->font, nullptr);
}

TEST_F(SecondaryViewTest, SizeContainerQueryMathUsesEditionOwnerFontsAndVariables) {
    stylesheet("@page{size:400px 200px;margin:0}p{margin:0}"
        "#container{container:card / size;width:180px;height:30px;font-size:20px;--threshold:150px}"
        "#target{font-size:40px;width:20px;height:10px}"
        "@container card (width > calc(var(--threshold) + 1em)){#target{width:100px}}"
        "@media print{#container{font-size:30px}}");
    DomElement* container = identified_block("container"); ASSERT_NE(container, nullptr);
    DomElement* target = identified_block("target", container); ASSERT_NE(target, nullptr);
    PagedLayoutOptions options = paged_layout_options_default();
    for (bool print : {false, true}) {
        ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_CONTINUOUS);
        environment.print_media = print;
        ViewTree* tree = view_tree_secondary_create(&doc, &environment); ASSERT_NE(tree, nullptr);
        ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
        ViewNodeState* state = view_tree_node_state(tree, target, false); ASSERT_NE(state, nullptr);
        ASSERT_NE(state->first_occurrence, nullptr);
        EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, print ? 20.0f : 100.0f);
        EXPECT_FLOAT_EQ(state->computed_style->font.font_size, 40.0f);
    }
}

TEST_F(SecondaryViewTest, SizeContainerFragmentsRetainTheUnfragmentedContentExtent) {
    stylesheet("@page{size:180px 80px;margin:0}p{margin:0}"
        "#container{container:card / size;width:100px;height:140px;padding:5px;border:2px solid}"
        "#target{width:20px;height:10px}#units{width:50cqw;height:10cqh}"
        "@container card (width = 100px) and (height = 140px){#target{width:60px}}");
    DomElement* container = identified_block("container"); ASSERT_NE(container, nullptr);
    DomElement* target = identified_block("target", container); ASSERT_NE(target, nullptr);
    DomElement* units = identified_block("units", container); ASSERT_NE(units, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_GE(tree->model->page_count, 2u);
    ViewNodeState* container_state = view_tree_node_state(tree, container, false);
    ViewNodeState* target_state = view_tree_node_state(tree, target, false);
    ViewNodeState* unit_state = view_tree_node_state(tree, units, false);
    ASSERT_NE(container_state, nullptr); ASSERT_NE(target_state, nullptr); ASSERT_NE(unit_state, nullptr);
    EXPECT_GT(container_state->occurrence_count, 1u);
    EXPECT_FLOAT_EQ(container_state->container_width, 100.0f);
    EXPECT_FLOAT_EQ(container_state->container_height, 140.0f);
    EXPECT_FLOAT_EQ(target_state->first_occurrence->rect.width, 60.0f);
    EXPECT_FLOAT_EQ(unit_state->first_occurrence->rect.width, 50.0f);
    EXPECT_FLOAT_EQ(unit_state->first_occurrence->rect.height, 14.0f);
}

TEST_F(SecondaryViewTest, SizeContainerPassBudgetsFailBeforeCommitAndResetDropsMeasurements) {
    stylesheet("@page{size:400px 200px;margin:0}p{margin:0}"
        "#container{container-type:size;width:180px;height:30px}"
        "#target{width:20px;height:10px}@container(width > 100px){#target{width:100px}}");
    DomElement* container = identified_block("container"); ASSERT_NE(container, nullptr);
    DomElement* target = identified_block("target", container); ASSERT_NE(target, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    options.max_container_passes = 1;
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_FALSE(tree->model->committed);
    ASSERT_NE(diagnostic.reason, nullptr);
    EXPECT_NE(strstr(diagnostic.reason, "condition-pass budget"), nullptr);
    EXPECT_EQ(diagnostic.container_passes, 1u);
    ASSERT_NE(tree->model->containers, nullptr);
    tree->reset_retained();
    EXPECT_EQ(tree->model->containers, nullptr);
    options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_TRUE(tree->model->committed);
    EXPECT_FLOAT_EQ(view_tree_node_state(tree, target, false)->first_occurrence->rect.width, 100.0f);
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options);
    ASSERT_NE(preview, nullptr);
    ViewTree* retained = view_tree_page_content_owner(preview); ASSERT_NE(retained, nullptr);
    ViewCssContainerState* measurements = retained->model->containers;
    tree->reset_retained();
    EXPECT_EQ(tree->model->containers, nullptr);
    retained = view_tree_page_content_owner(preview); ASSERT_NE(retained, nullptr);
    EXPECT_EQ(retained->model->containers.get(), measurements);
    EXPECT_FLOAT_EQ(view_tree_node_state(retained, target, false)->first_occurrence->rect.width, 100.0f);
    EXPECT_TRUE(css_evaluate_container_query(retained->model->css->engine, target,
        "(width > calc(160px + 1em))"));
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    EXPECT_NE(tree->model->containers.get(), measurements);
}

TEST_F(SecondaryViewTest, VariablesAndLineHeightInheritComputedOwnerValues) {
    DomElement* child = DomElement::create(&doc, "span", elmt_arena(doc.node_arena));
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(source->DomNode::append_child(child));
    stylesheet("p { --base: 4px; --derived: var(--base); font-size: 10px; line-height: 2em; } "
        "span { --base: 12px; padding-left: var(--derived); font-size: 30px; }");
    ViewTree* tree = secondary();
    ViewCssStyle* parent = view_css_resolve(tree, source);
    ViewCssStyle* style = view_css_resolve(tree, child);
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(style, nullptr);
    EXPECT_FLOAT_EQ(style->line_height, 20.0f);
    EXPECT_FLOAT_EQ(view_css_length(tree, style, style->padding[3], CSS_PROPERTY_PADDING_LEFT, 100.0f, 100.0f), 4.0f);
}

TEST_F(SecondaryViewTest, ComputedPropertyBindingsUseTheSemanticParentAndRetainTheirOwnerValues) {
    init_vector_engine();
    stylesheet("@page{size:180px 120px;margin:10px}p,div{margin:0;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, "padding-top:2em;color:red", "div"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:start-indent", "6px"));
    DomElement* wrapper = block(nullptr, "font-size:80px;padding-top:0;color:blue", "div", parent); ASSERT_NE(wrapper, nullptr);
    ASSERT_TRUE(wrapper->set_attribute("r:style-transparent", "true"));
    DomElement* child = block("Child", "--size:99px;font-size:calc(var(--size) + 2px);padding-top:var(--edge);color:var(--ink);background-color:var(--ink)", "div", wrapper);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:property-bindings", "--\\73 ize:parent(font-\\73 ize);--edge:parent(padding-top);--ink:parent(color);--indent:parent(start-indent);--initial:ancestor(99,font-size)"));
    ASSERT_TRUE(child->set_attribute("r:start-indent", "calc(var(--indent) + var(--size))"));
    DomElement* grandchild = block("Grandchild", "font-size:calc(var(--initial) + 14px);padding-top:var(--size)", "div", child); ASSERT_NE(grandchild, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->parent->source, parent); EXPECT_FLOAT_EQ(style->font.font_size, 12);
    ASSERT_NE(style->padding[0], nullptr); EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 20);
    EXPECT_EQ(style->color.r, 255); EXPECT_EQ(style->color.b, 0); EXPECT_FLOAT_EQ(style->flow_traits->indents[0], 16);
    style = view_css_resolve(tree, grandchild); ASSERT_NE(style, nullptr);
    EXPECT_FLOAT_EQ(style->font.font_size, 30); ASSERT_NE(style->padding[0], nullptr); EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 10);
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(retained, nullptr);
    LayoutViewNode* occurrence = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(occurrence, nullptr);
    size_t sample_x = static_cast<size_t>(occurrence->rect.x + 80.0f), sample_y = static_cast<size_t>(occurrence->rect.y + 5.0f);
    ASSERT_TRUE(parent->set_attribute("style", "font:20px Arial;padding-top:1px;color:green"));
    ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    style = view_css_resolve(tree, child); EXPECT_FLOAT_EQ(style->font.font_size, 22);
    EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 1); EXPECT_EQ(style->color.g, 128);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* surface = render_secondary_page_snapshot(retained, 1, 1.0f); ASSERT_NE(surface, nullptr);
    EXPECT_EQ(snapshot_pixel(surface, sample_x, sample_y), 0xff0000ffu); image_surface_destroy(surface);
}

TEST_F(SecondaryViewTest, ComputedDecorationBindingsCaptureOwnerColorsKeywordsAndLineLimits) {
    stylesheet("@page{size:180px 160px;margin:10px}p,div{margin:0;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, "color:red;background-color:rgba(0,0,255,.5);font-style:italic;text-align:right;"
        "orphans:4;widows:5;border-left:2px solid green;border-right:1px solid currentcolor", "div"); ASSERT_NE(parent, nullptr);
    DomElement* wrapper = block(nullptr, "color:black;font-style:normal", "div", parent); ASSERT_NE(wrapper, nullptr);
    ASSERT_TRUE(wrapper->set_attribute("r:style-transparent", "true"));
    DomElement* child = block("Child", "color:var(--ink);background-color:var(--fill);font-style:var(--slant);text-align:var(--align);"
        "orphans:var(--first);widows:var(--last);border-left-width:1px;border-left-style:var(--pattern);border-left-color:var(--current);"
        "border-bottom:1px solid var(--initial)", "div", wrapper); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:property-bindings", "--ink:parent(border-left-color);--fill:parent(background-color);"
        "--slant:parent(font-style);--align:parent(text-align);--first:parent(orphans);--last:parent(widows);"
        "--pattern:parent(border-left-style);--current:parent(border-right-color);--initial:ancestor(99,background-color)"));
    DomElement* descendant = block("Descendant", "color:blue;border-left:1px solid var(--current)", "div", child); ASSERT_NE(descendant, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->parent->source, parent); EXPECT_EQ(style->color.g, 128); EXPECT_EQ(style->color.r, 0);
    EXPECT_EQ(style->background.b, 255); EXPECT_EQ(style->background.a, 128);
    EXPECT_EQ(style->font.font_style, CSS_VALUE_ITALIC); EXPECT_EQ(style->text_align, CSS_VALUE_RIGHT);
    EXPECT_EQ(style->orphans, 4u); EXPECT_EQ(style->widows, 5u); EXPECT_EQ(style->border_style[3], CSS_VALUE_SOLID);
    EXPECT_EQ(style->border_color[3].r, 255); EXPECT_EQ(style->border_color[3].g, 0); EXPECT_EQ(style->border_color[2].a, 0);
    style = view_css_resolve(tree, descendant); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->color.b, 255); EXPECT_EQ(style->border_color[3].r, 255); EXPECT_EQ(style->border_color[3].b, 0);
    for (const char* name : {"font-style", "text-align", "orphans", "widows", "background-color",
            "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
            "border-top-style", "border-right-style", "border-bottom-style", "border-left-style"}) {
        SCOPED_TRACE(name); EXPECT_TRUE(view_css_computed_property_supported(name));
        EXPECT_NE(view_css_computed_property(tree, nullptr, name), nullptr);
    }
}

TEST_F(SecondaryViewTest, InvalidComputedBindingsAndTypedConsumersRejectTheWholeEdition) {
    stylesheet("@page{size:180px 120px;margin:10px}p,div{margin:0;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, "color:red;padding-top:10%", "div"); ASSERT_NE(parent, nullptr);
    DomElement* child = block("Child", nullptr, "div", parent); ASSERT_NE(child, nullptr);
    const char* invalid[] = {"--n:parent(width)", "--n:parent(font-size);--n:parent(color)", "--n:parent(font-size) garbage",
        "--n:parent(font-size,color)", "--n:parent(padding-top)", "ordinary:parent(font-size)", "--n:parent(font-size",
        "--n:ancestor(0,font-size)", "--n:ancestor(1.5,font-size)", "--n:ancestor(-2,font-size)", "--n:ancestor(2 font-size)"};
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    for (const char* binding : invalid) {
        SCOPED_TRACE(binding); ASSERT_TRUE(child->set_attribute("r:property-bindings", binding)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        EXPECT_EQ(diagnostic.source.address, child); EXPECT_NE(diagnostic.reason, nullptr);
    }
    ASSERT_TRUE(child->set_attribute("r:property-bindings", "--ink:parent(color);--size:parent(font-size)"));
    for (const char* css : {"font-size:var(--ink)", "font-size:calc(var(--size) / 0)", "padding-top:var(--missing)"}) {
        SCOPED_TRACE(css); ASSERT_TRUE(child->set_attribute("style", css)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        EXPECT_EQ(diagnostic.source.address, child);
    }
}

TEST_F(SecondaryViewTest, ComputedSpaceRangesKeepTheirDeclaringFontAndNormalizedComponents) {
    stylesheet("@page{size:220px 240px;margin:10px}p,div{margin:0;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, nullptr, "div"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:space-before", "2em"));
    ASSERT_TRUE(parent->set_attribute("r:space-before.minimum", "1em"));
    ASSERT_TRUE(parent->set_attribute("r:space-before.maximum", "3em"));
    ASSERT_TRUE(parent->set_attribute("r:space-after", "3px"));
    ASSERT_TRUE(parent->set_attribute("r:space-after.minimum", "5px"));
    ASSERT_TRUE(parent->set_attribute("r:space-after.maximum", "1px"));
    DomElement* wrapper = block(nullptr, "font-size:99px", "div", parent); ASSERT_NE(wrapper, nullptr);
    ASSERT_TRUE(wrapper->set_attribute("r:style-transparent", "true"));
    DomElement* child = block("Child", "font-size:20px;padding-top:var(--min);padding-right:var(--opt);padding-bottom:var(--max)", "div", wrapper);
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:property-bindings", "--min:parent(space-before\\.minimum);--opt:parent(space-before\\.optimum);"
        "--max:parent(space-before\\.maximum);--tail:parent(space-after\\.minimum);--initial:ancestor(99,space-after\\.maximum)"));
    ASSERT_TRUE(child->set_attribute("r:space-after", "calc(var(--tail) + var(--initial))"));
    DomElement* descendant = block("Next", "font-size:40px;padding-left:var(--min)", "div", child); ASSERT_NE(descendant, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->parent->source, parent);
    const double expected[] = {10, 20, 30};
    for (size_t i = 0; i < 3; i++) { ASSERT_NE(style->padding[i], nullptr); EXPECT_DOUBLE_EQ(style->padding[i]->data.length.value, expected[i]); }
    ASSERT_NE(style->flow_traits, nullptr); EXPECT_FLOAT_EQ(style->flow_traits->after.optimum, 3);
    style = view_css_resolve(tree, descendant); ASSERT_NE(style, nullptr); ASSERT_NE(style->padding[3], nullptr);
    EXPECT_DOUBLE_EQ(style->padding[3]->data.length.value, 10);
    for (const char* name : {"space-before.minimum", "space-before.optimum", "space-before.maximum",
            "space-after.minimum", "space-after.optimum", "space-after.maximum"}) {
        SCOPED_TRACE(name); ASSERT_TRUE(view_css_computed_property_supported(name));
        const CssValue* initial = view_css_computed_property(tree, nullptr, name); ASSERT_NE(initial, nullptr);
        EXPECT_EQ(initial->type, CSS_VALUE_TYPE_LENGTH); EXPECT_DOUBLE_EQ(initial->data.length.value, 0);
    }
    ASSERT_TRUE(parent->set_attribute("style", "font-size:12px")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr); EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 12);
    for (const char* query : {"--bad:parent(space-before)", "--bad:parent(space-before\\.unknown)"}) {
        ASSERT_TRUE(child->set_attribute("r:property-bindings", query)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        EXPECT_EQ(diagnostic.source.address, child);
    }
}

TEST_F(SecondaryViewTest, ComputedSpacePoliciesAndKeepStrengthsStayTypedAcrossNativeBindings) {
    stylesheet("@page{size:220px 180px;margin:10px}p,div{margin:0;font:10px/12px Arial;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, nullptr, "div"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:space-before.precedence", "force"));
    ASSERT_TRUE(parent->set_attribute("r:space-before.conditionality", "retain"));
    ASSERT_TRUE(parent->set_attribute("r:space-after.precedence", "-7"));
    ASSERT_TRUE(parent->set_attribute("r:keep-together", "always"));
    ASSERT_TRUE(parent->set_attribute("r:keep-together.within-page", "30"));
    ASSERT_TRUE(parent->set_attribute("r:keep-with-previous.within-column", "2147483647"));
    DomElement* wrapper = block(nullptr, "font-size:99px", "div", parent); ASSERT_NE(wrapper, nullptr);
    ASSERT_TRUE(wrapper->set_attribute("r:style-transparent", "true"));
    DomElement* child = block("Child", "padding-top:calc(var(--n) * -1px)", "div", wrapper); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:property-bindings", "--force:parent(space-before\\.precedence);--retain:parent(space-before\\.conditionality);"
        "--n:parent(space-after\\.precedence);--always:parent(keep-together\\.within-line);--page:parent(keep-together\\.within-page);"
        "--max:parent(keep-with-previous\\.within-column);--auto:ancestor(99,keep-with-next\\.within-page)"));
    ASSERT_TRUE(child->set_attribute("r:space-before.precedence", "var(--force)"));
    ASSERT_TRUE(child->set_attribute("r:space-before.conditionality", "var(--retain)"));
    ASSERT_TRUE(child->set_attribute("r:space-after.precedence", "calc(var(--n) + 2)"));
    ASSERT_TRUE(child->set_attribute("r:keep-together.within-line", "var(--always)"));
    ASSERT_TRUE(child->set_attribute("r:keep-together.within-column", "calc(var(--page) / 3)"));
    ASSERT_TRUE(child->set_attribute("r:keep-together.within-page", "var(--max)"));
    ASSERT_TRUE(child->set_attribute("r:keep-with-next.within-page", "var(--auto)"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr); ASSERT_NE(style->flow_traits, nullptr);
    EXPECT_EQ(style->parent->source, parent); EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 7);
    const RadiantFlowTraits* traits = style->flow_traits;
    EXPECT_TRUE(traits->before.force); EXPECT_TRUE(traits->before.retain); EXPECT_EQ(traits->after.precedence, -5);
    EXPECT_EQ(traits->together.scope[0].kind, RADIANT_KEEP_ALWAYS); EXPECT_EQ(traits->together.scope[1].value, 10);
    EXPECT_EQ(traits->together.scope[2].value, INT32_MAX); EXPECT_EQ(traits->next.scope[2].kind, RADIANT_KEEP_AUTO);
    for (const char* name : {"space-before.precedence", "space-after.precedence", "space-before.conditionality", "space-after.conditionality",
            "keep-together.within-line", "keep-together.within-column", "keep-together.within-page",
            "keep-with-next.within-line", "keep-with-next.within-column", "keep-with-next.within-page",
            "keep-with-previous.within-line", "keep-with-previous.within-column", "keep-with-previous.within-page"}) {
        SCOPED_TRACE(name); EXPECT_TRUE(view_css_computed_property_supported(name));
        const CssValue* initial = view_css_computed_property(tree, nullptr, name); ASSERT_NE(initial, nullptr);
        if (strstr(name, ".precedence")) { EXPECT_EQ(initial->type, CSS_VALUE_TYPE_NUMBER); EXPECT_DOUBLE_EQ(initial->data.number.value, 0); }
        else EXPECT_STREQ(css_value_identifier_name(initial), strstr(name, ".conditionality") ? "discard" : "auto");
    }
    ASSERT_TRUE(child->set_attribute("r:space-after.precedence", "calc(2em / 1em)")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(view_css_resolve(tree, child)->flow_traits->after.precedence, 2);
    struct Case { const char* name; const char* bad; const char* restore; };
    for (const Case& item : {Case{"r:space-before.precedence", "var(--retain)", "var(--force)"},
            Case{"r:space-before.conditionality", "var(--n)", "var(--retain)"},
            Case{"r:keep-together.within-page", "calc(var(--max) + 1)", "var(--max)"},
            Case{"r:keep-together.within-page", "1.5", "var(--max)"},
            Case{"r:keep-together.within-page", "calc(1em / 0em)", "var(--max)"}}) {
        SCOPED_TRACE(item.bad); ASSERT_TRUE(child->set_attribute(item.name, item.bad)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        EXPECT_EQ(diagnostic.source.address, child); ASSERT_TRUE(child->set_attribute(item.name, item.restore));
    }
}

TEST_F(SecondaryViewTest, ElementNavigationSkipsTextNodesInMixedContent) {
    DomText* leading = DomText::create_copy(" leading ", 9, source); ASSERT_NE(leading, nullptr);
    ASSERT_TRUE(source->DomNode::append_child(leading));
    DomElement* first = block("First"); ASSERT_NE(first, nullptr);
    DomText* middle = DomText::create_copy(" between ", 9, source); ASSERT_NE(middle, nullptr);
    ASSERT_TRUE(source->DomNode::append_child(middle));
    DomElement* last = block("Last"); ASSERT_NE(last, nullptr);
    DomText* trailing = DomText::create_copy(" trailing ", 10, source); ASSERT_NE(trailing, nullptr);
    ASSERT_TRUE(source->DomNode::append_child(trailing));
    EXPECT_EQ(source->first_child_element(), first); EXPECT_EQ(source->last_child_element(), last);
    EXPECT_EQ(first->next_sibling_element(), last); EXPECT_EQ(last->prev_sibling_element(), first);
    EXPECT_EQ(first->prev_sibling_element(), nullptr); EXPECT_EQ(last->next_sibling_element(), nullptr);
    EXPECT_EQ(first->first_child_element(), nullptr); EXPECT_EQ(last->last_child_element(), nullptr);
    EXPECT_EQ(first->parent_element(), source); EXPECT_EQ(source->parent_element(), nullptr);
}

struct XmlFormattingSpans {
    Element* elements[4];
    size_t start[4], end[4], count;
};

static void xml_formatting_span(void* context, Element* element, size_t start, size_t end) {
    XmlFormattingSpans* spans = (XmlFormattingSpans*)context;
    if (spans->count >= 4) return;
    size_t index = spans->count++;
    spans->elements[index] = element; spans->start[index] = start; spans->end[index] = end;
}

TEST_F(SecondaryViewTest, XmlFormattingIntakePreservesWhitespaceCdataAndElementRanges) {
    const char* xml = "<a> \n<b> x </b> <c/> </a>";
    XmlFormattingSpans spans = {};
    XmlParseOptions options = {true, true, &spans, xml_formatting_span};
    parse_xml_with_options(input, xml, &options);
    ASSERT_FALSE(input->parse_failed);
    ElementReader root = ElementReader(input->root).childAt(0).asElement();
    ASSERT_EQ(root.childCount(), 5);
    EXPECT_STREQ(root.childAt(0).cstring(), " \n");
    EXPECT_STREQ(root.childAt(1).asElement().childAt(0).cstring(), " x ");
    EXPECT_STREQ(root.childAt(2).cstring(), " "); EXPECT_STREQ(root.childAt(4).cstring(), " ");
    ASSERT_EQ(spans.count, 3u);
    EXPECT_EQ(spans.start[0], 5u); EXPECT_EQ(spans.end[0], 15u);
    EXPECT_EQ(spans.start[1], 16u); EXPECT_EQ(spans.end[1], 20u);
    EXPECT_EQ(spans.start[2], 0u); EXPECT_EQ(spans.end[2], strlen(xml));
    EXPECT_EQ(spans.elements[2], root.element());
    parse_xml(input, xml);
    root = ElementReader(input->root).childAt(0).asElement();
    EXPECT_EQ(root.childCount(), 2);
    EXPECT_STREQ(root.childAt(0).asElement().childAt(0).cstring(), "x");
    parse_xml_with_options(input, "<a>  <![CDATA[ x ]]>  </a>", &options);
    root = ElementReader(input->root).childAt(0).asElement();
    ASSERT_EQ(root.childCount(), 1);
    EXPECT_STREQ(root.childAt(0).cstring(), "   x   ");
}

TEST_F(SecondaryViewTest, XmlFormattingIntakeDiagnosesMismatchedAndMissingClosingTags) {
    XmlParseOptions options = {true, true, nullptr, nullptr};
    parse_xml_with_options(input, "<a><b>text</a>", &options);
    EXPECT_TRUE(input->parse_failed);
    input->parse_failed = false;
    parse_xml_with_options(input, "<a><b>text", &options);
    EXPECT_TRUE(input->parse_failed);
}

TEST_F(SecondaryViewTest, XmlFormattingIntakeAcceptsDottedAndUnicodeNames) {
    XmlParseOptions options = {true, true, nullptr, nullptr};
    parse_xml_with_options(input, "<文.書 space-before.minimum='3pt' 属性.値='保持'>text</文.書>", &options);
    ASSERT_FALSE(input->parse_failed);
    ElementReader element = ElementReader(input->root).childAt(0).asElement(); ASSERT_TRUE(element.isValid());
    EXPECT_STREQ(element.get_attr_string("space-before.minimum"), "3pt");
    EXPECT_STREQ(element.get_attr_string("属性.値"), "保持");
    parse_xml_with_options(input, "<1bad/>", &options); EXPECT_TRUE(input->parse_failed);
}

TEST_F(SecondaryViewTest, XmlFormattingIntakeRejectsMalformedAttributesEntitiesAndCharacters) {
    XmlParseOptions options = {true, true, nullptr, nullptr};
    const char* invalid[] = {"<a x='1' x='2'/>", "<a x='1'y='2'/>", "<a x='unterminated>",
        "<a x='literal < value'/>", "<a>&unknown;</a>", "<a>&amp</a>", "<a>&#;</a>", "<a>&#x;</a>",
        "<a>&#X41;</a>", "<a>&#0;</a>", "<a>&#xD800;</a>", "<a>&#1114112;</a>",
        "<a>&#4294967361;</a>", "<a>&#x1000000000041;</a>", "<a>\x01</a>", "<a>\xc0\xaf</a>",
        "<a>\xed\xa0\x80</a>", "<a>\xf4\x90\x80\x80</a>", "<a>]]></a>", "<a></ a>", "<a></a extra>"};
    for (const char* xml : invalid) {
        SCOPED_TRACE(xml); input->parse_failed = false; parse_xml_with_options(input, xml, &options);
        EXPECT_TRUE(input->parse_failed); EXPECT_EQ(input->root.item, ITEM_ERROR);
    }
    input->parse_failed = false;
    parse_xml_with_options(input, "<a x='&lt;&gt;&amp;&apos;&quot;&#x9;&#10;&#13;'>&lt;&gt;&amp;&apos;&quot;&#x1F642;]]&gt;</a>", &options);
    ASSERT_FALSE(input->parse_failed);
    ElementReader root = ElementReader(input->root).childAt(0).asElement(); ASSERT_TRUE(root.isValid());
    EXPECT_STREQ(root.get_attr_string("x"), "<>&'\"\t\n\r");
    EXPECT_STREQ(root.childAt(0).cstring(), "<>&'\"🙂]]>");
    parse_xml(input, "<a>&nbsp;&unknown;</a>"); // legacy data-oriented entity handling remains available.
    root = ElementReader(input->root).childAt(0).asElement(); EXPECT_STREQ(root.childAt(0).cstring(), " &unknown;");
}

TEST_F(SecondaryViewTest, XmlFormattingIntakeRequiresOneRootAndCompleteCommentsCdataAndInstructions) {
    XmlParseOptions options = {true, true, nullptr, nullptr};
    const char* invalid[] = {"", "   ", "<!-- only a comment -->", "<a/><b/>", "text<a/>", "<a/>text",
        "<![CDATA[text]]><a/>", "<a><!-- unclosed</a>", "<a><!-- bad--comment --></a>", "<a><!-- bad---></a>",
        "<a><![CDATA[unclosed</a>", "<a><?target unclosed</a>", "<a><?target/data?></a>",
        "<a><?xml version='1.0'?></a>", " <?xml version='1.0'?><a/>", "<?XML version='1.0'?><a/>",
        "<?xml?><a/>", "<?xml version='1.1'?><a/>", "<?xml encoding='UTF-8' version='1.0'?><a/>",
        "<?xml version='1.0' encoding='UTF-16'?><a/>", "<?xml version='1.0' standalone='maybe'?><a/>",
        "<?xml version='1.0' extra='value'?><a/>", "<?xml version='&#49;.0'?><a/>",
        "<!DOCTYPE a [<!ENTITY value 'text'>]><a>&value;</a>"};
    for (const char* xml : invalid) {
        SCOPED_TRACE(xml); input->parse_failed = false; parse_xml_with_options(input, xml, &options); EXPECT_TRUE(input->parse_failed);
    }
    input->parse_failed = false;
    parse_xml_with_options(input, "\xef\xbb\xbf<?xml version='1.0' encoding='utf-8' standalone='yes'?>"
        "<!-- before --><?target data?><a><![CDATA[<&]]><!-- content --><?target?></a><!-- after -->", &options);
    EXPECT_FALSE(input->parse_failed);
}

TEST_F(SecondaryViewTest, XmlFormattingIntakeNormalizesPhysicalLineEndsWithoutChangingReferenceWhitespaceOrRanges) {
    const char* xml = "<a x='one\r\ntwo\rthree\nfour\tfive&#xD;&#xA;&#x9;'>A\r\nB\rC<![CDATA[D\r\nE\rF]]></a>";
    XmlFormattingSpans spans = {}; XmlParseOptions options = {true, true, &spans, xml_formatting_span};
    parse_xml_with_options(input, xml, &options); ASSERT_FALSE(input->parse_failed);
    ElementReader root = ElementReader(input->root).childAt(0).asElement(); ASSERT_TRUE(root.isValid());
    EXPECT_STREQ(root.get_attr_string("x"), "one two three four five\r\n\t");
    EXPECT_STREQ(root.childAt(0).cstring(), "A\nB\nCD\nE\nF");
    ASSERT_EQ(spans.count, 1u); EXPECT_EQ(spans.start[0], 0u); EXPECT_EQ(spans.end[0], strlen(xml));
    parse_xml(input, xml); root = ElementReader(input->root).childAt(0).asElement();
    EXPECT_STREQ(root.get_attr_string("x"), "one\r\ntwo\rthree\nfour\tfive\r\n\t");
}

TEST_F(SecondaryViewTest, XmlFormattingNamespaceValidationHonorsRebindingReservedNamesAndExpandedAttributeIdentity) {
    XmlParseOptions options = {true, true, nullptr, nullptr, true};
    const char* invalid[] = {"<p:a/>", "<a p:x='value'/>", "<:a/>", "<a: xmlns:a='urn:a'/>",
        "<a:b:c xmlns:a='urn:a'/>", "<a:1bad xmlns:a='urn:a'/>", "<xmlns:a/>",
        "<a xmlns:p=''/>", "<a xmlns:xmlns='urn:wrong'/>", "<a xmlns:xml='urn:wrong'/>",
        "<a xmlns='http://www.w3.org/XML/1998/namespace'/>", "<a xmlns:p='http://www.w3.org/XML/1998/namespace'/>",
        "<a xmlns:p='http://www.w3.org/2000/xmlns/'/>", "<a xmlns:1bad='urn:a'/>",
        "<a xmlns:p='urn:a' xmlns:q='urn:a' p:x='one' q:x='two'/>",
        "<a xmlns:p='urn:a'><b xmlns:p=''/></a>"};
    for (const char* xml : invalid) {
        SCOPED_TRACE(xml); input->parse_failed = false; parse_xml_with_options(input, xml, &options);
        EXPECT_TRUE(input->parse_failed); EXPECT_EQ(input->root.item, ITEM_ERROR);
    }
    input->parse_failed = false;
    parse_xml_with_options(input,
        "<p:a xmlns='urn:default' xmlns:p='urn:outer' xmlns:q='urn:outer' xml:lang='en' x='local' p:x='qualified'>"
        "<p:b xmlns:p='urn:inner' q:x='outer'/><c xmlns=''/><p:d/></p:a>", &options);
    ASSERT_FALSE(input->parse_failed);
    ElementReader root = ElementReader(input->root).childAt(0).asElement(); ASSERT_TRUE(root.isValid());
    DomElement* dom = build_dom_tree_from_element(const_cast<Element*>(root.element()), &doc, nullptr); ASSERT_NE(dom, nullptr);
    EXPECT_STREQ(dom_element_namespace_uri(dom), "urn:outer");
    EXPECT_STREQ(dom_element_namespace_uri(dom->first_child_element()), "urn:inner");
    EXPECT_STREQ(dom_element_namespace_uri(dom->first_child_element()->next_sibling_element()), "");
    EXPECT_STREQ(dom_element_namespace_uri(dom->last_child_element()), "urn:outer");
}

TEST_F(SecondaryViewTest, XmlFormattingLongNamespaceAliasesResolveElementsAttributesAndNativeFlowTraits) {
    char prefix[257]; memset(prefix, 'p', sizeof(prefix) - 1); prefix[sizeof(prefix) - 1] = '\0';
    StrBuf* xml = strbuf_new(); ASSERT_NE(xml, nullptr);
    strbuf_append_format(xml, "<%s:root xmlns:%s='%s'><div %s:space-before='3pt'>Text</div></%s:root>",
        prefix, prefix, RADIANT_PAGE_NAMESPACE, prefix, prefix);
    XmlParseOptions options = {true, true, nullptr, nullptr, true}; parse_xml_with_options(input, xml->str, &options);
    strbuf_free(xml); ASSERT_FALSE(input->parse_failed);
    ElementReader root = ElementReader(input->root).childAt(0).asElement(); ASSERT_TRUE(root.isValid());
    DomElement* dom = build_dom_tree_from_element(const_cast<Element*>(root.element()), &doc, nullptr); ASSERT_NE(dom, nullptr);
    EXPECT_STREQ(dom_element_namespace_uri(dom), RADIANT_PAGE_NAMESPACE);
    EXPECT_STREQ(dom_element_lookup_namespace_uri(dom, prefix), RADIANT_PAGE_NAMESPACE);
    DomElement* child = dom->first_child_element(); ASSERT_NE(child, nullptr);
    StrBuf* name = strbuf_new(); ASSERT_NE(name, nullptr); strbuf_append_format(name, "%s:space-before", prefix);
    const char* local = nullptr;
    EXPECT_STREQ(dom_element_attribute_namespace_uri(child, name->str, &local), RADIANT_PAGE_NAMESPACE);
    EXPECT_STREQ(local, "space-before"); strbuf_free(name);
    doc.root = lam::up(dom); ViewTree* tree = secondary(); ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    ASSERT_NE(style->flow_traits, nullptr); EXPECT_EQ(style->flow_traits->status, VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(style->flow_traits->before.optimum, 4.0f);
}

TEST_F(SecondaryViewTest, FoBlocksTranslateToNativePagesAndHtmlWithOriginalInheritance) {
    const char* xml =
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='10pt' color='blue'>"
        "<f:layout-master-set font-size='2em'><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt' margin='7.5pt'>"
        "<f:region-body margin='3pt' region-name='main'/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' font-size='2em'><f:flow flow-name='main'>"
        "<f:block id='first'> Alpha <f:inline font-weight='bold'>Beta</f:inline> </f:block>"
        "<f:block break-before='page'>Second</f:block>"
        "</f:flow></f:page-sequence></f:root>";
    XmlParseOptions xml_options = {true, true, nullptr, nullptr};
    parse_xml_with_options(input, xml, &xml_options); ASSERT_FALSE(input->parse_failed);
    ElementReader original = ElementReader(input->root).childAt(0).asElement(); ASSERT_TRUE(original.isValid());
    DomElement* fo = build_dom_tree_from_element(const_cast<Element*>(original.element()), &doc, nullptr); ASSERT_NE(fo, nullptr);
    RadiantFoSourceSpan root_span = {original.element(), 0, strlen(xml), nullptr};
    DomElement* original_sequence = fo->last_child_element(); ASSERT_NE(original_sequence, nullptr);
    RadiantFoSourceSpan sequence_span = {dom_element_render_source(original_sequence),
        (size_t)(strstr(xml, "<f:page-sequence") - xml),
        (size_t)(strstr(xml, "</f:page-sequence>") - xml) + strlen("</f:page-sequence>"), &root_span};
    RadiantFoOptions options = radiant_fo_options_default(); options.spans = &sequence_span;
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.qname << " "
        << translated->diagnostic.property << ": " << translated->diagnostic.reason;
    ASSERT_NE(translated->root, nullptr);
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins));
    doc.root = lam::up(generated);
    ViewTree* tree = secondary(); PagedLayoutOptions layout_options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &layout_options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->node.rect.width, 200.0f);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->content_rect.x, 14.0f);
    EXPECT_STREQ(tree->model->pages.get()[0]->style->body_region->name, "main");
    EXPECT_NEAR(tree->model->pages.get()[0]->style->computed_style->font.font_size, 80.0f / 3.0f, 0.001f);
    DomElement* sequence = generated->last_child_element(); ASSERT_NE(sequence, nullptr);
    DomElement* flow = sequence->first_child_element(); ASSERT_NE(flow, nullptr);
    DomElement* first = flow->first_child_element(); ASSERT_NE(first, nullptr);
    DomElement* span = first->first_child_element(); ASSERT_NE(span, nullptr);
    ViewCssStyle* first_style = view_css_resolve(tree, first), *span_style = view_css_resolve(tree, span);
    ASSERT_NE(first_style, nullptr); ASSERT_NE(span_style, nullptr);
    EXPECT_NEAR(first_style->font.font_size, 80.0f / 3.0f, 0.001f);
    EXPECT_FLOAT_EQ(span_style->font.font_size, first_style->font.font_size);
    EXPECT_EQ(span_style->color.b, 255); EXPECT_EQ(span_style->font.font_weight_numeric, 700);
    size_t origins = 0;
    for (RadiantFoOrigin* origin = translated->origins; origin; origin = origin->next) {
        EXPECT_NE(origin->source.address, nullptr); EXPECT_NE(origin->translated, nullptr); origins++;
    }
    EXPECT_EQ(origins, translated->node_count);
    EXPECT_EQ(fo->parent.get(), nullptr); EXPECT_STREQ(fo->tag_name, "f:root");
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    append_fragment_text(tree->model->root, text);
    EXPECT_NE(strstr(text->str, "Alpha"), nullptr); EXPECT_NE(strstr(text->str, "Beta"), nullptr); EXPECT_NE(strstr(text->str, "Second"), nullptr);
    strbuf_free(text);
    ASSERT_TRUE(sequence->set_attribute("master-reference", "missing")); ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &layout_options, &diagnostic), TYPESET_INVALID);
    ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, original_sequence);
    EXPECT_STREQ(diagnostic.origin->qname, "f:page-sequence"); EXPECT_TRUE(diagnostic.origin->has_range);
    EXPECT_EQ(diagnostic.origin->start, sequence_span.start); EXPECT_EQ(diagnostic.origin->end, sequence_span.end);
}

TEST_F(SecondaryViewTest, FoTablesShareCommonTracksRepeatedGroupsSpansAndCellContinuations) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt' border-collapse='separate'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='60pt'><f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:table width='100%' table-layout='fixed' border-collapse='inherit'><f:table-column column-width='30pt'/>"
        "<f:table-column column-width='60pt' number-columns-repeated='1.5'/>"
        "<f:table-header><f:table-row><f:table-cell number-columns-spanned='3'><f:block>Head</f:block></f:table-cell></f:table-row></f:table-header>"
        "<f:table-footer><f:table-row><f:table-cell number-columns-spanned='3'><f:block>Foot</f:block></f:table-cell></f:table-row></f:table-footer>"
        "<f:table-body><f:table-row><f:table-cell number-rows-spanned='2.49' display-align='center'><f:block>S</f:block></f:table-cell>"
        "<f:table-cell number-columns-spanned='1.5'><f:block>A</f:block></f:table-cell></f:table-row>"
        "<f:table-row><f:table-cell number-columns-spanned='2'><f:block>B</f:block></f:table-cell></f:table-row>"
        "<f:table-row><f:table-cell number-columns-spanned='2'><f:block linefeed-treatment='preserve' white-space-treatment='preserve'>"
        "C\nD\nE\nF\nG\nH\nI\nJ\nK\nL</f:block></f:table-cell><f:table-cell><f:block>X</f:block></f:table-cell></f:table-row>"
        "</f:table-body></f:table></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* table = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(table, nullptr);
    EXPECT_STREQ(table->tag_name, "table");
    DomElement* repeated = table->first_child_element()->next_sibling_element(); ASSERT_NE(repeated, nullptr); EXPECT_STREQ(repeated->get_attribute("span"), "2");
    DomElement* header = repeated->next_sibling_element(); ASSERT_NE(header, nullptr);
    DomElement* footer = header->next_sibling_element(); ASSERT_NE(footer, nullptr);
    DomElement* body = table->last_child_element(); ASSERT_NE(body, nullptr);
    DomElement* cell = body->first_child_element()->first_child_element(); ASSERT_NE(cell, nullptr);
    EXPECT_STREQ(cell->get_attribute("rowspan"), "2"); EXPECT_STREQ(cell->next_sibling_element()->get_attribute("colspan"), "2");
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_TRUE(css_value_keyword_equals(view_css_resolve(tree, cell)->vertical_align, CSS_VALUE_MIDDLE));
    for (DomElement* group : {header, footer}) {
        ViewNodeState* state = view_tree_node_state(tree, group, false); ASSERT_NE(state, nullptr);
        size_t count = 0;
        for (LayoutViewNode* occurrence = state->first_occurrence; occurrence; occurrence = occurrence->next_occurrence) {
            EXPECT_EQ(occurrence_page(occurrence), count + 1);
            EXPECT_EQ(occurrence->role, count ? VIEW_FRAGMENT_REPEATED_TABLE : VIEW_FRAGMENT_BODY); count++;
        }
        EXPECT_EQ(count, 3u);
    }
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(tree->model->root, text);
    EXPECT_NE(strstr(text->str, "L"), nullptr); strbuf_free(text);
}

TEST_F(SecondaryViewTest, NativeReferenceBlocksRetainThePageInlineExtentAcrossPaddingAndPageChanges) {
    stylesheet("@page{size:160px 80px;margin:10px}@page :left{size:140px 80px}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* outer = block(nullptr, "padding:4px;border:1px solid red", "div"); ASSERT_NE(outer, nullptr);
    DomElement* inner = block("A\nB\nC\nD\nE\nF\nG\nH\nI", "padding:3px;border:2px solid blue", "div", outer);
    ASSERT_NE(inner, nullptr);
    for (const char* policy : {"css", "reference"}) {
        SCOPED_TRACE(policy); ASSERT_TRUE(outer->set_attribute("r:block-inline-geometry", policy));
        ASSERT_TRUE(inner->set_attribute("r:block-inline-geometry", "inherit"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_GE(tree->model->page_count, 2u);
        ViewNodeState* state = view_tree_node_state(tree, inner, false); ASSERT_NE(state, nullptr);
        for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
            float width = tree->model->pages.get()[occurrence_page(part) - 1]->content_rect.width;
            EXPECT_FLOAT_EQ(part->rect.x, !strcmp(policy, "reference") ? 5.0f : 15.0f);
            EXPECT_FLOAT_EQ(part->rect.width, !strcmp(policy, "reference") ? width + 10.0f : width - 10.0f);
        }
        ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    }
}

TEST_F(SecondaryViewTest, NativeReferenceBlocksUseCellAndStaticRegionReferenceAreas) {
    stylesheet("p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}"
        "table{table-layout:fixed;width:100%;border-spacing:0}td{padding:2px;border:1px solid black}");
    DomElement* master = page_master("sheet", "size:160px 100px;margin:10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "body", "main"), nullptr);
    ASSERT_NE(page_region(master, "before", "head", "20px", "padding:3px;border:1px solid green"), nullptr);
    DomElement* sequence = page_sequence("sheet"); ASSERT_NE(sequence, nullptr);
    DomElement* binding = static_content(sequence, "head"); ASSERT_NE(binding, nullptr);
    DomElement* furniture = block("Head", "padding:2px;border:1px solid red", "div", binding); ASSERT_NE(furniture, nullptr);
    ASSERT_TRUE(furniture->set_attribute("r:block-inline-geometry", "reference"));
    DomElement* table = block(nullptr, nullptr, "table", sequence); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr);
    DomElement* child = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL", "padding:2px;border:1px solid blue", "div", cell);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(child->set_attribute("r:block-inline-geometry", "reference"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_GE(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, child, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
        EXPECT_FLOAT_EQ(part->rect.x, 10.0f); EXPECT_FLOAT_EQ(part->rect.width, 140.0f);
    }
    state = view_tree_node_state(tree, furniture, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
        EXPECT_FLOAT_EQ(part->rect.x, 11.0f); EXPECT_FLOAT_EQ(part->rect.width, 138.0f);
    }
}

TEST_F(SecondaryViewTest, NativeReferenceIndentsInheritComputedLengthsAndRetainNegativeOutdents) {
    stylesheet("@page{size:160px 80px;margin:10px}@page :left{size:140px 80px}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, nullptr, "div"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_TRUE(parent->set_attribute("r:start-indent", "2em")); ASSERT_TRUE(parent->set_attribute("r:end-indent", "-5px"));
    DomElement* child = block("A\nB\nC\nD\nE\nF\nG\nH\nI", "font-size:20px;padding:2px;border:1px solid blue", "div", parent);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(child->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_TRUE(child->set_attribute("r:start-indent", "inherit"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_GE(tree->model->page_count, 2u);
    const RadiantFlowTraits* traits = view_css_resolve(tree, child)->flow_traits.get(); ASSERT_NE(traits, nullptr);
    EXPECT_FLOAT_EQ(traits->indents[0], 20.0f); EXPECT_FLOAT_EQ(traits->indents[1], -5.0f);
    ViewNodeState* state = view_tree_node_state(tree, child, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
        float width = tree->model->pages.get()[occurrence_page(part) - 1]->content_rect.width;
        EXPECT_FLOAT_EQ(part->rect.x, 27.0f); EXPECT_FLOAT_EQ(part->rect.width, width - 9.0f);
    }
    for (const char* invalid : {"auto", "nan", "calc(3px + 2deg)", "calc(3% / 0)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(child->set_attribute("r:start-indent", invalid)); ASSERT_TRUE(view_tree_model_reset(tree));
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_EQ(diagnostic.source.address, child); EXPECT_EQ(tree->model->page_count, 0u);
        ASSERT_NE(diagnostic.reason, nullptr); EXPECT_NE(strstr(diagnostic.reason, "indents"), nullptr);
    }
}

TEST_F(SecondaryViewTest, PercentageIndentsRetainTheirDeclaringFontAndFirstPageAcrossContinuations) {
    init_vector_engine();
    stylesheet("@page{size:160px 80px;margin:10px}@page :left{size:140px 80px}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, nullptr, "div"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_TRUE(parent->set_attribute("r:start-indent", "calc(10% + 1em)"));
    ASSERT_TRUE(parent->set_attribute("r:end-indent", "-5%"));
    DomElement* child = block("A\nB\nC\nD\nE\nF\nG\nH\nI", "font-size:20px;padding:2px;border:1px solid red;background-color:blue", "div", parent);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(child->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_TRUE(child->set_attribute("r:start-indent", "inherit"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_GE(tree->model->page_count, 2u);
    const RadiantFlowTraits* traits = view_css_resolve(tree, child)->flow_traits; ASSERT_NE(traits, nullptr);
    EXPECT_EQ(traits->indent_owners[0], view_css_resolve(tree, parent)); ASSERT_NE(traits->indent_expressions[0], nullptr);
    ViewNodeState* state = view_tree_node_state(tree, child, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
        float width = tree->model->pages.get()[occurrence_page(part) - 1]->content_rect.width;
        EXPECT_FLOAT_EQ(part->rect.x, 31.0f); EXPECT_FLOAT_EQ(part->rect.width, width - 11.0f);
    }
    ViewPreviewOptions preview = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ViewTree* control = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(control, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:start-indent", "calc(10% / 0)")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    for (uint32_t page = 1; page <= retained->model->page_count; page++) expect_same_page_pixels(retained, control, page);
}

TEST_F(SecondaryViewTest, DeferredPercentageDeclarationsUseTheirFirstSelectedAreaAndResetWithTheEdition) {
    stylesheet("@page{size:160px 80px;margin:10px}@page :left{size:140px 80px}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    ASSERT_NE(block("Lead\nTwo\nThree\nFour"), nullptr);
    DomElement* target = block("A\nB", "break-before:page", "div"); ASSERT_NE(target, nullptr);
    ASSERT_TRUE(target->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_TRUE(target->set_attribute("r:start-indent", "max(10%, 1px)"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    for (const char* mode : {"break-before:page", "break-inside:avoid", "break-before:auto"}) {
        SCOPED_TRACE(mode); bool deferred = strcmp(mode, "break-before:auto") != 0;
        ASSERT_TRUE(target->set_attribute("style", mode));
        ASSERT_TRUE(view_tree_model_reset(tree));
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ViewNodeState* state = view_tree_node_state(tree, target, false); ASSERT_NE(state, nullptr);
        EXPECT_EQ(occurrence_page(state->first_occurrence), deferred ? 2u : 1u);
        for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
            float width = tree->model->pages.get()[occurrence_page(part) - 1]->content_rect.width;
            float indent = deferred ? 12.0f : 14.0f;
            EXPECT_FLOAT_EQ(part->rect.x, 10.0f + indent); EXPECT_FLOAT_EQ(part->rect.width, width - indent);
        }
    }
}

TEST_F(SecondaryViewTest, PercentageIndentsRebindWhenAnEmptySheetSelectsItsOnlyMaster) {
    stylesheet("p,div{margin:0;font:10px/12px Arial;orphans:1;widows:1}");
    ASSERT_NE(page_master("normal", "size:160px 100px;margin:10px"), nullptr);
    ASSERT_NE(page_master("only", "size:180px 100px;margin:10px"), nullptr);
    DomElement* master = page_control("r:sequence-master"); ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* run = page_control("r:master-run", master); ASSERT_NE(run, nullptr);
    ASSERT_NE(master_choice(run, "only", "only"), nullptr); ASSERT_NE(master_choice(run, "normal"), nullptr);
    DomElement* sequence = page_sequence("chapter", "1", "no-force"); ASSERT_NE(sequence, nullptr);
    DomElement* child = block("Only", nullptr, "div", sequence); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:block-inline-geometry", "reference")); ASSERT_TRUE(child->set_attribute("r:start-indent", "10%"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 1u);
    EXPECT_STREQ(tree->model->pages.get()[0]->name, "only");
    LayoutViewNode* box = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->rect.x, 26.0f); EXPECT_FLOAT_EQ(box->rect.width, 144.0f);
}

TEST_F(SecondaryViewTest, PercentageIndentsComputeAtTheirFirstCellAreaAndCoverEverySourceByte) {
    stylesheet("@page{size:160px 80px;margin:10px}@page :left{size:140px 80px}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}"
        "table{table-layout:fixed;width:100%;border-spacing:0}td{padding:0;vertical-align:top}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr);
    DomElement* parent = block(nullptr, nullptr, "div", cell); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_TRUE(parent->set_attribute("r:start-indent", "calc(10% + 1em)"));
    DomElement* child = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL", "font-size:20px", "div", parent);
    ASSERT_NE(child, nullptr); ASSERT_TRUE(child->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_NE(block("X", nullptr, "td", row), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_GE(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, child, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
        float width = tree->model->pages.get()[occurrence_page(part) - 1]->content_rect.width * 0.5f;
        EXPECT_FLOAT_EQ(part->rect.x, 27.0f); EXPECT_FLOAT_EQ(part->rect.width, width - 17.0f);
    }
    DomText* text = child->first_child->as_text(); ASSERT_NE(text, nullptr); size_t bytes = 0;
    state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) {
        EXPECT_EQ(part->text_start, bytes); bytes += part->text_length;
    }
    EXPECT_EQ(bytes, text->length);
}

TEST_F(SecondaryViewTest, PercentageIndentsUseTheStaticRegionsContentReferenceArea) {
    stylesheet("p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    DomElement* master = page_master("sheet", "size:160px 100px;margin:10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "body", "main"), nullptr);
    ASSERT_NE(page_region(master, "before", "head", "20px", "padding:3px;border:1px solid green"), nullptr);
    DomElement* sequence = page_sequence("sheet"); ASSERT_NE(sequence, nullptr);
    DomElement* binding = static_content(sequence, "head"); ASSERT_NE(binding, nullptr);
    DomElement* furniture = block("Head", "padding:2px;border:1px solid red", "div", binding); ASSERT_NE(furniture, nullptr);
    ASSERT_TRUE(furniture->set_attribute("r:block-inline-geometry", "reference")); ASSERT_TRUE(furniture->set_attribute("r:start-indent", "10%"));
    ASSERT_NE(block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", nullptr, "div", sequence), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_GE(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, furniture, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) if (part->paint_box) {
        EXPECT_NEAR(part->rect.x, 24.2f, 0.0001f); EXPECT_NEAR(part->rect.width, 124.8f, 0.0001f);
    }
}

TEST_F(SecondaryViewTest, PercentageIndentBindingsRespectBudgetsAndDiagnoseUnsupportedDeclarationContexts) {
    stylesheet("@page{size:160px 80px;margin:10px}p,div{margin:0;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* first = block(nullptr, "padding-top:1px", "div"); ASSERT_NE(first, nullptr);
    DomElement* second = block(nullptr, "padding-top:1px", "div"); ASSERT_NE(second, nullptr);
    for (DomElement* item : {first, second}) {
        ASSERT_TRUE(item->set_attribute("r:block-inline-geometry", "reference")); ASSERT_TRUE(item->set_attribute("r:start-indent", "10%"));
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    options = paged_layout_options_default();
    ASSERT_TRUE(second->set_attribute("style", "display:inline")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, second);
    ASSERT_NE(diagnostic.reason, nullptr); EXPECT_NE(strstr(diagnostic.reason, "ordinary block"), nullptr);
    ASSERT_TRUE(second->set_attribute("style", "display:block"));
    ASSERT_TRUE(second->set_attribute("r:property-bindings", "--n:parent(start-indent)"));
    ASSERT_TRUE(source->set_attribute("r:start-indent", "10%")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, second);
    ASSERT_NE(diagnostic.reason, nullptr); EXPECT_NE(strstr(diagnostic.reason, "definite"), nullptr);
}

TEST_F(SecondaryViewTest, PercentageIndentsRetainTheFirstNoteReferenceAreaAcrossAuxiliaryOnlyPages) {
    stylesheet("@page{size:160px 80px;margin:10px;@footnote{max-height:24px}}@page :left{size:140px 80px}"
        "p,div,span{margin:0;font:10px/12px Arial;orphans:1;widows:1}span::footnote-marker{content:''}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* call = block("Body", nullptr, "div"); ASSERT_NE(call, nullptr);
    DomElement* note = block(nullptr, "float:footnote", "span", call); ASSERT_NE(note, nullptr);
    DomElement* child = block("A\nB\nC\nD\nE\nF\nG\nH", "white-space:pre", "div", note); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:block-inline-geometry", "reference")); ASSERT_TRUE(child->set_attribute("r:start-indent", "10%"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_GE(tree->model->page_count, 2u);
    DomText* text = child->first_child->as_text(); ASSERT_NE(text, nullptr); size_t bytes = 0;
    ViewNodeState* state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) {
        EXPECT_EQ(part->text_start, bytes); bytes += part->text_length;
        if (part->glyph_run) EXPECT_FLOAT_EQ(part->rect.x, 24.0f);
    }
    EXPECT_EQ(bytes, text->length);
}

TEST_F(SecondaryViewTest, FoReferenceIndentsRetainTheirAncestryAndDiagnoseUnsupportedGridRefinement) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'>"
        "<f:flow flow-name='xsl-region-body'><f:block start-indent='2em' end-indent='-3pt'>"
        "<f:block font-size='15pt' start-indent='inherit' background-color='red'>Child</f:block>"
        "</f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* parent = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(parent, nullptr);
    DomElement* child = parent->first_child_element(); ASSERT_NE(child, nullptr);
    EXPECT_STREQ(parent->get_attribute("r:start-indent"), "2em"); EXPECT_STREQ(child->get_attribute("r:start-indent"), "inherit");
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* box = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->rect.x, 20.0f); EXPECT_FLOAT_EQ(box->rect.width, 184.0f);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    ASSERT_TRUE(original->set_attribute("start-indent", "10%"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK);
    generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins));
    tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element();
    box = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->rect.x, 20.0f); EXPECT_FLOAT_EQ(box->rect.width, 184.0f);
    DomElement* unsupported = block(nullptr, nullptr, "f:table", original); ASSERT_NE(unsupported, nullptr);
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, unsupported);
    EXPECT_STREQ(translated->diagnostic.property, "start-indent");
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

TEST_F(SecondaryViewTest, NativeMathIndentsUseTheirDeclaringFontAndRemainComputedOnInheritance) {
    stylesheet("@page{size:160px 80px;margin:10px}p,div{margin:0;font:10px/12px Arial;orphans:1;widows:1}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr, nullptr, "div"); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:block-inline-geometry", "reference"));
    ASSERT_TRUE(parent->set_attribute("r:start-indent", "calc((2em * 2em) / 2em + 3px)"));
    ASSERT_TRUE(parent->set_attribute("r:end-indent", "calc(-3px * 2)"));
    DomElement* child = block("Child", "font-size:20px", "div", parent); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:text-altitude", "calc(50% + 25%)"));
    ASSERT_TRUE(child->set_attribute("r:text-depth", "calc(10% * 2)"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const RadiantFlowTraits* traits = view_css_resolve(tree, child)->flow_traits.get(); ASSERT_NE(traits, nullptr);
    EXPECT_FLOAT_EQ(traits->indents[0], 23); EXPECT_FLOAT_EQ(traits->indents[1], -6);
    EXPECT_FLOAT_EQ(traits->text_metrics[0], 15); EXPECT_FLOAT_EQ(traits->text_metrics[1], 4);
    LayoutViewNode* box = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->rect.x, 33); EXPECT_FLOAT_EQ(box->rect.width, 123);
    ASSERT_TRUE(child->set_attribute("r:start-indent", "calc(1em + 3px)"));
    ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(view_css_resolve(tree, child)->flow_traits->indents[0], 23);
    ASSERT_TRUE(child->set_attribute("r:start-indent", "calc(1em / 0)"));
    ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_EQ(diagnostic.source.address, child);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
}

TEST_F(SecondaryViewTest, StructuralStyleWrappersPreserveDomSelectorsAndComputedParentInheritance) {
    stylesheet("@page{size:240px 160px;margin:10px}p,div{margin:0}.wrapper > .nested > p{color:red}");
    ASSERT_TRUE(source->set_attribute("xmlns:page", RADIANT_PAGE_NAMESPACE));
    ASSERT_TRUE(source->set_attribute("page:style-transparent", "true"));
    DomElement* outer = block(nullptr, "font:10px/12px Arial;padding-left:5px", "div"); ASSERT_NE(outer, nullptr);
    DomElement* wrapper = block(nullptr, "font-size:30px;padding-left:17px", "div", outer); ASSERT_NE(wrapper, nullptr);
    DomElement* nested = block(nullptr, "font-size:40px;padding-left:23px", "div", wrapper); ASSERT_NE(nested, nullptr);
    ASSERT_TRUE(wrapper->set_attribute("class", "wrapper")); ASSERT_TRUE(nested->set_attribute("class", "nested"));
    DomElement* child = block("Child", "font-size:inherit;padding-left:inherit", "p", nested); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(wrapper->set_attribute("page:style-transparent", "true"));
    ASSERT_TRUE(nested->set_attribute("page:style-transparent", "true"));
    ViewTree* tree = secondary(); ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->parent.get(), view_css_resolve(tree, outer));
    EXPECT_EQ(view_css_resolve(tree, outer)->parent.get(), view_css_resolve(tree, source));
    EXPECT_FLOAT_EQ(style->font.font_size, 10); ASSERT_NE(style->padding[3], nullptr);
    EXPECT_DOUBLE_EQ(style->padding[3]->data.length.value, 5);
    EXPECT_EQ(style->color.r, 255); EXPECT_EQ(style->color.g, 0); EXPECT_EQ(style->color.b, 0);
    EXPECT_EQ(child->parent_element(), nested); EXPECT_EQ(nested->parent_element(), wrapper);
    ASSERT_TRUE(nested->set_attribute("page:style-transparent", "false")); ASSERT_TRUE(view_tree_model_reset(tree));
    style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->parent.get(), view_css_resolve(tree, nested)); EXPECT_FLOAT_EQ(style->font.font_size, 40);
    ASSERT_NE(style->padding[3], nullptr); EXPECT_DOUBLE_EQ(style->padding[3]->data.length.value, 23);
    ASSERT_TRUE(nested->set_attribute("page:style-transparent", "maybe")); ASSERT_TRUE(view_tree_model_reset(tree));
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, nested); EXPECT_STREQ(diagnostic.reason, "style transparency requires true or false");
    EXPECT_EQ(tree->model->page_count, 0u); ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
}

TEST_F(SecondaryViewTest, FoNumericRefinementReachesNativeTraitsAndKeepsOriginalPropertyDiagnostics) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='(75pt * 4pt) div 2pt' page-height='(30pt * 30pt + 60pt * 30pt) div 30pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'>"
        "<f:flow flow-name='xsl-region-body'><f:block start-indent='(2em * 2em) div 2em + 3pt' end-indent='-3pt * 2' padding-start='1pt + 2pt' space-before='3pt * 2'>"
        "<f:block font-size='from-parent(font-size) * from-parent(font-size) div 3.75pt' start-indent='inherit' background-color='red'>Child</f:block>"
        "</f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const RadiantFlowTraits* traits = view_css_resolve(tree, child)->flow_traits.get(); ASSERT_NE(traits, nullptr);
    EXPECT_FLOAT_EQ(traits->indents[0], 24); EXPECT_FLOAT_EQ(traits->indents[1], -8);
    EXPECT_FLOAT_EQ(view_css_resolve(tree, child)->font.font_size, 20);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->node.rect.width, 200);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    for (const char* invalid : {"floor(1pt)", "1pt + 2", "max(1pt)", "1pt div 0", "1pt * 2pt", "1 div 2pt"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original->set_attribute("padding-start", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
        EXPECT_EQ(translated->diagnostic.source.address, original); EXPECT_STREQ(translated->diagnostic.property, "padding-start");
    }
    ASSERT_TRUE(original->set_attribute("padding-start", "1pt + 2pt"));
    fo_options.max_nodes = 16;
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(translated->root, nullptr);
}

TEST_F(SecondaryViewTest, FoWholeParentReferencesReuseComputedInheritanceAndCompoundComponents) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt' "
        "line-stacking-strategy='from-parent()' text-altitude='from-parent(text-altitude)' white-space-collapse='inherited-property-value()'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block font-size='10pt' color='red' padding-top='3pt' start-indent='6pt' space-before='3pt' space-before.conditionality='retain' "
        "border-before-width='2pt' border-before-style='solid' border-before-width.conditionality='retain'>"
        "<f:block font-size='from-parent(font-size)' color='inherited-property-value(color)' padding-top='from-parent()' "
        "start-indent='from-parent(start-indent)' space-before='from-parent(space-before)' "
        "border-before-width='from-parent(border-before-width)' border-before-style='from-parent()'>Child</f:block>"
        "</f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr); ASSERT_NE(style->flow_traits, nullptr);
    EXPECT_NEAR(style->font.font_size, 40.0f / 3.0f, 0.0001f);
    EXPECT_EQ(style->color.r, 255); EXPECT_EQ(style->color.g, 0);
    ASSERT_NE(style->padding[0], nullptr); EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 4);
    EXPECT_FLOAT_EQ(style->flow_traits->indents[0], 8); EXPECT_FLOAT_EQ(style->flow_traits->before.optimum, 4);
    EXPECT_TRUE(style->flow_traits->before.retain);
    EXPECT_EQ(style->flow_traits->decoration[0][0], RADIANT_DECORATION_RETAIN);
    EXPECT_EQ(view_css_resolve(tree, generated)->flow_traits->line_stacking, RADIANT_LINE_STACK_MAX);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    for (const char* invalid : {"from-parent(width)", "from-parent(font-size,font-size)", "from-parent(",
        "from-table-column(font-size) + 1pt", "inherited-property-value(width)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original->set_attribute("font-size", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
        EXPECT_EQ(translated->diagnostic.source.address, original); EXPECT_STREQ(translated->diagnostic.property, "font-size");
    }
    ASSERT_TRUE(original->set_attribute("font-size", " inherited-property-value( font-size ) "));
    ASSERT_TRUE(original->set_attribute("padding-top", "inherited-property-value()"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_STREQ(translated->diagnostic.property, "padding-top");
    ASSERT_TRUE(original->set_attribute("padding-top", "from-parent(padding-top)"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    ASSERT_TRUE(fo->set_attribute("font-size", "from-parent(font-size)"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_FLOAT_EQ(view_css_resolve(tree, generated)->font.font_size, 16.0f);
}

TEST_F(SecondaryViewTest, FoNumericAndCrossPropertyReferencesLowerToCommonComputedBindings) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block font-size='10pt' color='red' start-indent='6pt'><f:block font-size='from-parent(font-size) + 2pt' "
        "font-weight='round(from-parent(font-weight) div 100) * 100' "
        "padding-before='inherited-property-value(font-size) div 4' background-color='from-parent(color)' "
        "start-indent='from-parent(start-indent) + from-parent(font-size) div 2' "
        "space-before='from-parent(font-size) div 2'>Child</f:block></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins));
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_FLOAT_EQ(style->font.font_size, 16); EXPECT_EQ(style->background.r, 255); EXPECT_EQ(style->background.b, 0);
    EXPECT_EQ(style->font.font_weight_numeric, 400);
    ASSERT_NE(style->padding[0], nullptr); EXPECT_NEAR(style->padding[0]->data.length.value, 10.0 / 3.0, 0.0001);
    ASSERT_NE(style->flow_traits, nullptr); EXPECT_NEAR(style->flow_traits->indents[0], 44.0f / 3.0f, 0.0001f);
    EXPECT_NEAR(style->flow_traits->before.optimum, 20.0f / 3.0f, 0.0001f);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    for (const char* invalid : {"from-parent(color)", "floor(from-parent(font-size))"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original->set_attribute("font-size", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
        generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
        ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins)); tree = secondary();
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, original);
    }
}

TEST_F(SecondaryViewTest, NearestSpecifiedFoReferencesSelectComputedAncestorValuesAndInitials) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block padding='3pt' font-size='10pt' color='red'><f:block font-size='20pt' color='blue'>"
        "<f:block font-size='from-nearest-specified-value() div 2' padding-top='from-nearest-specified-value()' "
        "start-indent='from-nearest-specified-value(padding-top) + from-parent(font-size)' "
        "end-indent='from-nearest-specified-value(end-indent)' background-color='from-nearest-specified-value(color)'>Child"
        "</f:block></f:block></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_NEAR(style->font.font_size, 40.0f / 3.0f, 0.0001f); ASSERT_NE(style->padding[0], nullptr);
    EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 4); EXPECT_EQ(style->background.b, 255); EXPECT_EQ(style->background.r, 0);
    ASSERT_NE(style->flow_traits, nullptr); EXPECT_NEAR(style->flow_traits->indents[0], 92.0f / 3.0f, 0.0001f);
    EXPECT_FLOAT_EQ(style->flow_traits->indents[1], 0);
    const CssValue* ancestor = view_css_computed_property(tree, style->parent->parent, "padding-top"); ASSERT_NE(ancestor, nullptr);
    EXPECT_DOUBLE_EQ(ancestor->data.length.value, 4);
    DomElement* owner = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(owner, nullptr);
    ASSERT_TRUE(owner->remove_attribute("padding")); ASSERT_TRUE(owner->set_attribute("padding-before.length", "3pt"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK);
    generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element();
    tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_DOUBLE_EQ(view_css_resolve(tree, child)->padding[0]->data.length.value, 4);
}

TEST_F(SecondaryViewTest, FoDecorationReferencesRefineLogicalComponentsThroughCommonBindings) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt' "
        "font-style='italic' text-align='right' orphans='4' widows='5'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block border-before-color='#00ff00' border-before-style='solid' border-before-width.length='3pt' padding-start.length='4pt'>"
        "<f:block color='red' background-color='#446688'><f:block color='from-parent(background-color)' "
        "background-color='from-nearest-specified-value(border-before-color)' border-start-color='from-parent(color)' "
        "border-start-style='from-nearest-specified-value(border-before-style)' "
        "border-start-width.length='from-nearest-specified-value(border-before-width.length)' "
        "padding-end.length='from-nearest-specified-value(padding-start.length) * 2' font-style='from-nearest-specified-value(font-style)' "
        "text-align='from-parent(text-align)' orphans='from-nearest-specified-value(widows)' widows='from-parent(orphans)'>Child"
        "</f:block></f:block></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins));
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->color.r, 0x44); EXPECT_EQ(style->color.g, 0x66); EXPECT_EQ(style->color.b, 0x88);
    EXPECT_EQ(style->background.g, 255); EXPECT_EQ(style->background.r, 0); EXPECT_EQ(style->border_color[3].r, 255);
    EXPECT_EQ(style->border_style[3], CSS_VALUE_SOLID); ASSERT_NE(style->border_width[3], nullptr);
    EXPECT_DOUBLE_EQ(style->border_width[3]->data.length.value, 4); ASSERT_NE(style->padding[1], nullptr);
    EXPECT_NEAR(style->padding[1]->data.length.value, 32.0 / 3.0, 0.0001);
    EXPECT_EQ(style->font.font_style, CSS_VALUE_ITALIC); EXPECT_EQ(style->text_align, CSS_VALUE_RIGHT);
    EXPECT_EQ(style->orphans, 5u); EXPECT_EQ(style->widows, 4u);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    for (const char* invalid : {"inherited-property-value(background-color)", "from-parent(border-before-width)", "from-parent(padding-start)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original->set_attribute("background-color", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, original);
        EXPECT_STREQ(translated->diagnostic.property, "background-color");
    }
    ASSERT_TRUE(original->set_attribute("background-color", "from-nearest-specified-value(border-before-color)"));
    for (const char* name : {"font-style", "text-align", "orphans", "widows", "background-color", "border-before-color", "border-before-style"})
        ASSERT_TRUE(fo->set_attribute(name, "from-parent()"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    style = view_css_resolve(tree, generated); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->font.font_style, CSS_VALUE_NORMAL); EXPECT_EQ(style->text_align, CSS_VALUE_START);
    EXPECT_EQ(style->orphans, 2u); EXPECT_EQ(style->widows, 2u); EXPECT_EQ(style->background.a, 0);
    EXPECT_EQ(style->border_style[0], CSS_VALUE_NONE); EXPECT_EQ(style->border_color[0].r, 0); EXPECT_EQ(style->border_color[0].a, 255);
}

TEST_F(SecondaryViewTest, FoRgbExpressionsRetainComputedBindingsAndDiagnoseSubstitutedChannelDomains) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='180pt' page-height='180pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block font-size='10pt' font-weight='400'><f:block font-size='15pt' "
        "color='rgb(from-parent(font-weight) div 2 + 10% - 25.5, round(from-parent(font-size) div 1pt), 50)' "
        "background-color='rgb(100%, 100% div 3, max(0, 100 - 255))' "
        "border-before-color='rgb(0, from-nearest-specified-value(font-weight) div 4, 100%)'>Child"
        "</f:block></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins));
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->color.r, 200); EXPECT_EQ(style->color.g, 10); EXPECT_EQ(style->color.b, 50);
    EXPECT_EQ(style->background.r, 255); EXPECT_EQ(style->background.g, 85); EXPECT_EQ(style->background.b, 0);
    EXPECT_EQ(style->border_color[0].r, 0); EXPECT_EQ(style->border_color[0].g, 100); EXPECT_EQ(style->border_color[0].b, 255);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    for (const char* invalid : {"rgb(from-parent(font-size), 0, 0)", "rgb(from-parent(color), 0, 0)",
            "rgb(255 div (from-parent(font-weight) - 400), 0, 0)",
            "rgb(100% * from-parent(font-size), 0, 0)",
            "rgb(100% div (from-parent(font-weight) - 400), 0, 0)"}) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(original->set_attribute("color", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
        generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
        ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins)); tree = secondary();
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, original);
    }
    ASSERT_TRUE(original->set_attribute("color", "black"));
    ASSERT_TRUE(original->set_attribute("width", "rgb(1,2,3)"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, original);
    EXPECT_STREQ(translated->diagnostic.property, "width");
}

TEST_F(SecondaryViewTest, FoSpaceRangeReferencesUseComputedComponentsAndNearestCompoundAssignments) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='180pt' page-height='180pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block space-before='6pt' space-before.minimum='3pt' space-before.maximum='9pt' space-after='4pt' space-after.minimum='2pt' space-after.maximum='8pt'>"
        "<f:block space-before='5pt' font-size='15pt'><f:block padding-start='from-parent(space-before.minimum)' "
        "padding-end='from-parent(space-before.maximum) * 2' space-after.optimum='from-nearest-specified-value(space-after.optimum)' "
        "space-after.minimum='from-nearest-specified-value(space-after.minimum)' space-after.maximum='from-nearest-specified-value(space-after.maximum)' "
        "padding-before='from-nearest-specified-value(space-before.minimum)' padding-after='from-nearest-specified-value(space-before.maximum)'>Child"
        "</f:block></f:block></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element();
    ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr); ASSERT_NE(style->flow_traits, nullptr);
    const double expected[] = {20.0 / 3.0, 40.0 / 3.0, 20.0 / 3.0, 20.0 / 3.0};
    for (size_t i = 0; i < 4; i++) { ASSERT_NE(style->padding[i], nullptr); EXPECT_NEAR(style->padding[i]->data.length.value, expected[i], 0.0001); }
    EXPECT_NEAR(style->flow_traits->after.minimum, 8.0f / 3.0f, 0.0001f);
    EXPECT_NEAR(style->flow_traits->after.optimum, 16.0f / 3.0f, 0.0001f);
    EXPECT_NEAR(style->flow_traits->after.maximum, 32.0f / 3.0f, 0.0001f);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element();
    ASSERT_NE(original, nullptr);
    for (const char* invalid : {"inherited-property-value(space-before.minimum)", "from-parent(space-before)",
            "from-parent(space-before.unknown)", "from-parent(space-before . minimum)"}) {
        ASSERT_TRUE(original->set_attribute("padding-before", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
        EXPECT_EQ(translated->diagnostic.source.address, original); EXPECT_STREQ(translated->diagnostic.property, "padding-before");
    }
    ASSERT_TRUE(original->set_attribute("padding-before", "from-nearest-specified-value(space-before.optimum)"));
    ASSERT_TRUE(fo->set_attribute("padding-before", "from-parent(space-before.minimum)"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    style = view_css_resolve(tree, generated); ASSERT_NE(style, nullptr); ASSERT_NE(style->padding[0], nullptr);
    EXPECT_DOUBLE_EQ(style->padding[0]->data.length.value, 0);
}

TEST_F(SecondaryViewTest, FoPolicyQueriesUseCompoundAssignmentsAndWholeKeepTogetherInheritance) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='180pt' page-height='180pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block keep-together='5' space-before.precedence='force' space-before.conditionality='retain' space-after.precedence='-7'>"
        "<f:block keep-together.within-page='8' space-before='0pt'><f:block "
        "space-before.precedence='from-nearest-specified-value(space-before.precedence)' "
        "space-before.conditionality='from-nearest-specified-value(space-before.conditionality)' "
        "space-after.precedence='from-nearest-specified-value(space-after.precedence) + 2' "
        "keep-together.within-line='from-parent(keep-with-next.within-page)' "
        "keep-with-next.within-page='inherited-property-value(keep-together.within-page)' "
        "keep-with-previous.within-column='from-nearest-specified-value(keep-together.within-column)'>Child"
        "</f:block></f:block></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins));
    DomElement* child = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const RadiantFlowTraits* traits = view_css_resolve(tree, child)->flow_traits; ASSERT_NE(traits, nullptr);
    // the nearer space short form assigns initial policy components as well as all lengths.
    EXPECT_FALSE(traits->before.force); EXPECT_EQ(traits->before.precedence, 0); EXPECT_FALSE(traits->before.retain);
    EXPECT_EQ(traits->after.precedence, -5); EXPECT_EQ(traits->together.scope[0].kind, RADIANT_KEEP_AUTO);
    EXPECT_EQ(traits->together.scope[1].value, 5); EXPECT_EQ(traits->together.scope[2].value, 8);
    EXPECT_EQ(traits->next.scope[2].value, 8); EXPECT_EQ(traits->previous.scope[1].value, 5);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    for (const char* invalid : {"inherited-property-value(space-before.precedence)", "inherited-property-value(keep-with-next.within-page)",
            "from-parent(keep-together)", "from-parent(keep-together.unknown)"}) {
        ASSERT_TRUE(original->set_attribute("keep-with-next.within-page", invalid));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->root, nullptr);
        EXPECT_EQ(translated->diagnostic.source.address, original); EXPECT_STREQ(translated->diagnostic.property, "keep-with-next.within-page");
    }
    ASSERT_TRUE(original->set_attribute("keep-with-next.within-page", "from-parent(space-after.precedence)"));
    DomElement* parent = original->parent_element(); ASSERT_NE(parent, nullptr);
    ASSERT_TRUE(parent->set_attribute("space-after.precedence", "force"));
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK);
    generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins)); tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, original);
}

TEST_F(SecondaryViewTest, XmlNamespaceIdentityStorageRemainsInternalToFoTranslation) {
    doc.xml_document = true;
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'>"
        "<f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'>"
        "<f:flow flow-name='xsl-region-body'><f:block>Hello</f:block></f:flow></f:page-sequence></f:root>");
    ASSERT_NE(fo, nullptr);
    ASSERT_STREQ(dom_element_namespace_uri(fo), RADIANT_FO_NAMESPACE);
    ASSERT_NE(fo->get_attribute("__lambda_ns_uri"), nullptr);
    RadiantFoOptions options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &options);
    ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
}

TEST_F(SecondaryViewTest, FoInheritedNominalMetricKeywordsUseTheSelectedChildFont) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block font-size='10pt' text-altitude='use-font-metrics'><f:block font-size='20pt' text-altitude='from-parent()' "
        "text-depth='inherit'>Keyword</f:block></f:block>"
        "<f:block font-size='10pt' text-altitude='50%' text-depth='0.2em'><f:block text-altitude='inherit' text-depth='from-parent()'>"
        "<f:block font-size='20pt' text-altitude='from-parent(text-altitude)' text-depth='inherit'>Length</f:block>"
        "</f:block></f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* flow = generated->last_child_element()->last_child_element(); ASSERT_NE(flow, nullptr);
    DomElement* keyword = flow->first_child_element()->first_child_element(); ASSERT_NE(keyword, nullptr);
    DomElement* length = flow->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(length, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewCssStyle* style = view_css_resolve(tree, keyword); ASSERT_NE(style, nullptr); ASSERT_NE(style->flow_traits, nullptr);
    const FontMetrics* metrics = font_get_metrics(style->font.font_handle); ASSERT_NE(metrics, nullptr);
    EXPECT_FLOAT_EQ(style->flow_traits->text_metrics[0], metrics->typo_ascender);
    EXPECT_FLOAT_EQ(style->flow_traits->text_metrics[1], metrics->typo_descender);
    style = view_css_resolve(tree, length); ASSERT_NE(style, nullptr); ASSERT_NE(style->flow_traits, nullptr);
    EXPECT_NEAR(style->flow_traits->text_metrics[0], 20.0f / 3.0f, 0.0001f);
    EXPECT_NEAR(style->flow_traits->text_metrics[1], 8.0f / 3.0f, 0.0001f);
}

TEST_F(SecondaryViewTest, FoCorrespondingDecorationRefinesExplicitPropertiesBeforeCommonLayout) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='120pt'><f:region-body overflow='visible'/>"
        "</f:simple-page-master></f:layout-master-set><f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block border='9pt solid black' border-width='6pt' border-top='3pt solid red' border-before-width='1.5pt' border-top-width='2.25pt' "
        "border-after-width='0.75pt' border-start-width='3pt' border-end-width='3.75pt' border-before-color='blue' border-top-color='green' "
        "border-start-color='red' border-end-color='blue' padding='9pt' padding-before='2.25pt' padding-top='1.5pt' padding-after='3pt' "
        "padding-start='3.75pt' padding-end='4.5pt'><f:block font-size='15pt' padding-start='inherit' border-start-width='inherit' "
        "border-start-style='inherit' border-start-color='inherit' border-before-width='12pt' border-before-style='none'>Child</f:block>"
        "</f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* parent = generated->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(parent, nullptr);
    DomElement* child = parent->first_child_element(); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* box = source_fragment(tree, parent, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr); ASSERT_NE(box->computed_boundary, nullptr);
    const BoundaryProp* boundary = box->computed_boundary.get(); ASSERT_NE(boundary->border, nullptr);
    EXPECT_FLOAT_EQ(boundary->border->width.top, 3.0f); EXPECT_FLOAT_EQ(boundary->border->width.bottom, 1.0f);
    EXPECT_FLOAT_EQ(boundary->border->width.left, 4.0f); EXPECT_FLOAT_EQ(boundary->border->width.right, 5.0f);
    EXPECT_EQ(boundary->border->top_color.g, 128); EXPECT_EQ(boundary->border->left_color.r, 255); EXPECT_EQ(boundary->border->right_color.b, 255);
    EXPECT_FLOAT_EQ(boundary->padding.top, 2.0f); EXPECT_FLOAT_EQ(boundary->padding.bottom, 4.0f);
    EXPECT_FLOAT_EQ(boundary->padding.left, 5.0f); EXPECT_FLOAT_EQ(boundary->padding.right, 6.0f);
    box = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr); ASSERT_NE(box->computed_boundary, nullptr);
    boundary = box->computed_boundary.get(); ASSERT_NE(boundary->border, nullptr);
    EXPECT_FLOAT_EQ(boundary->border->width.left, 4.0f); EXPECT_EQ(boundary->border->left_color.r, 255);
    EXPECT_FLOAT_EQ(boundary->border->width.top, 0.0f); EXPECT_FLOAT_EQ(boundary->padding.left, 5.0f);
    EXPECT_FLOAT_EQ(boundary->padding.top, 0.0f); EXPECT_FLOAT_EQ(box->rect.x, -9.0f);
}

TEST_F(SecondaryViewTest, PagedMulticolorBordersPaintSharedMitersAndRetainOwnedPaths) {
    init_vector_engine();
    stylesheet("@page{size:160px 100px;margin:10px}p,div{margin:0;font:10px/12px Arial}");
    DomElement* child = block(nullptr, "width:80px;height:40px;border-style:solid;border-width:4px 8px 6px 10px;"
        "border-top-color:red;border-right-color:blue;border-bottom-color:green;border-left-color:yellow"); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewPreviewOptions preview = view_preview_options_default(); preview.padding = 0.0f;
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* surface = render_secondary_view_snapshot(retained); ASSERT_NE(surface, nullptr);
    EXPECT_EQ(snapshot_pixel(surface, 50, 11), 0xff0000ffu); EXPECT_EQ(snapshot_pixel(surface, 106, 35), 0xffff0000u);
    EXPECT_EQ(snapshot_pixel(surface, 50, 58), 0xff008000u); EXPECT_EQ(snapshot_pixel(surface, 11, 35), 0xff00ffffu);
    EXPECT_EQ(snapshot_pixel(surface, 11, 12), 0xff00ffffu); EXPECT_EQ(snapshot_pixel(surface, 18, 10), 0xff0000ffu);
    image_surface_destroy(surface);
}

TEST_F(SecondaryViewTest, ComputedBoxInheritanceRetainsOwnerFontsAndCurrentColors) {
    stylesheet("@page{size:200px 160px;margin:10px}p,div{margin:0}");
    DomElement* parent = block(nullptr, "font:10px/12px Arial;color:red;background:blue;"
        "padding-left:calc(10% + 1em);border-left:1em solid currentcolor;border-top:4px none"); ASSERT_NE(parent, nullptr);
    DomElement* child = block("Text", "font-size:20px;color:green;background-color:inherit;padding-left:inherit;"
        "border-left:inherit;border-top-width:inherit;border-top-style:solid", "div", parent); ASSERT_NE(child, nullptr);
    ViewTree* tree = secondary(); ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    ViewCssBoxEdges box = {};
    ASSERT_EQ(view_css_box_edges(tree, style, 100.0f, &box), VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(box.padding[3], 20.0f); EXPECT_FLOAT_EQ(box.border.width.left, 10.0f);
    EXPECT_FLOAT_EQ(box.border.width.top, 0.0f); EXPECT_EQ(box.border.left_color.c, 0xff0000ffu);
    EXPECT_EQ(box.background.color.c, 0xffff0000u);
    ASSERT_EQ(view_css_box_edges(tree, style, 150.0f, &box), VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(box.padding[3], 25.0f); EXPECT_FLOAT_EQ(box.border.width.left, 10.0f);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* occurrence = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(occurrence, nullptr);
    ASSERT_NE(occurrence->computed_boundary, nullptr); ASSERT_NE(occurrence->computed_boundary->border, nullptr);
    EXPECT_FLOAT_EQ(occurrence->computed_boundary->border->width.left, 10.0f);
}

TEST(PagedCssTest, PhysicalAndLogicalBorderComponentsUseTheSameValidatedGrammar) {
    Pool* pool = mem_pool_create(nullptr, MEM_ROLE_VIEW, "test.paged.border.grammar"); ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(css_property_system_init(pool));
    const struct { const char* property; const char* value; bool supported; } cases[] = {
        {"border-left-style", "invalid-value", false}, {"border-inline-start-style", "invalid-value", false},
        {"border-style", "solid dashed dotted double", true}, {"border-style", "solid invalid-value", false},
        {"border-style", "solid solid solid solid solid", false}, {"border-left-style", "solid dashed", false},
        {"border-style", "solid, dashed", false}, {"border-color", "red, blue", false},
        {"border-width", "1px 2px 3px 4px", true}, {"border-left-width", "-1px", false}, {"border-left-width", "5%", false},
        {"border-color", "red green blue black", true}, {"border-color", "red invalid-value", false},
        {"border-left-style", "inherit", true}, {"border-color", "inherit red", false}
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.property);
        SCOPED_TRACE(test.value);
        CssDeclaration* parsed = css_parse_property_value_declaration(test.property, strlen(test.property), test.value, strlen(test.value), pool);
        EXPECT_EQ(css_declaration_is_supported(parsed), test.supported);
    }
    mem_pool_destroy(pool);
}

TEST_F(SecondaryViewTest, FoCorrespondingDecorationDiagnosticsRetainOriginalPropertyNames) {
    DomElement* fo = formatting_root("<f:root xmlns:f='http://www.w3.org/1999/XSL/Format'><f:layout-master-set>"
        "<f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master>"
        "</f:layout-master-set><f:page-sequence master-reference='sheet'><f:flow flow-name='xsl-region-body'><f:block padding-left='3pt'>"
        "Text</f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    RadiantFoOptions options = radiant_fo_options_default();
    for (const char* property : {"padding-start", "border-before-width", "border-end-style", "border-after-color"}) {
        SCOPED_TRACE(property); ASSERT_TRUE(original->set_attribute(property, "invalid-value"));
        RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, original);
        EXPECT_STREQ(translated->diagnostic.property, property); EXPECT_EQ(translated->root, nullptr);
        ASSERT_TRUE(original->remove_attribute(property));
    }
}

TEST_F(SecondaryViewTest, NativeReferenceGeometryRejectsConflictingSizingWithoutPublishingPages) {
    stylesheet("@page{size:160px 100px;margin:10px}p,div{margin:0;font:10px/12px Arial}");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* child = block("Text"); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->set_attribute("r:block-inline-geometry", "reference"));
    for (const char* css : {"width:30px", "min-width:30px", "max-width:30px", "margin-left:5px"}) {
        SCOPED_TRACE(css); ASSERT_TRUE(child->set_attribute("style", css));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_EQ(diagnostic.source.address, child); EXPECT_EQ(tree->model->page_count, 0u);
        ASSERT_NE(diagnostic.reason, nullptr); EXPECT_NE(strstr(diagnostic.reason, "reference block geometry"), nullptr);
        ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    }
}

TEST_F(SecondaryViewTest, SplitCellSiblingBoundariesPreserveLineMinimaBeforeRelaxingThem) {
    stylesheet("@page{size:140px 100px;margin:10px}table{table-layout:fixed;width:100%;border-spacing:0}"
        "td{padding:0;vertical-align:top}p{margin:0;font:10px/12px Arial;white-space:pre;orphans:3;widows:3}");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr);
    DomElement* first = block("A\nB\nC\nD\nE", nullptr, "p", cell); ASSERT_NE(first, nullptr);
    DomElement* second = block("F\nG\nH\nI\nJ\nK", nullptr, "p", cell); ASSERT_NE(second, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, second, false); ASSERT_NE(state, nullptr);
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) EXPECT_EQ(occurrence_page(part), 2u);
    EXPECT_EQ(diagnostic.relaxed_line_minima, 0u);
    EXPECT_EQ(source_fragment(tree, first, VIEW_FRAGMENT_BODY, true)->rect.height, 60.0f);
    EXPECT_EQ(source_fragment(tree, second, VIEW_FRAGMENT_BODY, true)->rect.height, 72.0f);
}

TEST_F(SecondaryViewTest, SplitCellBlockForestsShareAncestorsAndRetainEmptySiblingBoundaries) {
    stylesheet("@page{size:140px 100px;margin:10px}@page :left{size:120px 100px}"
        "table{table-layout:fixed;width:100%;border-spacing:0}td{padding:0;vertical-align:top}"
        "p,div{margin:0;font:10px/12px Arial;white-space:pre;orphans:1;widows:1}");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* label = block(nullptr, "width:20px", "td", row); ASSERT_NE(label, nullptr);
    DomElement* labels[2] = {block("X", nullptr, "p", label), block("Y", nullptr, "p", label)};
    DomElement* body = block(nullptr, nullptr, "td", row); ASSERT_NE(body, nullptr);
    DomElement* leading = block("Lead", nullptr, "p", body); ASSERT_NE(leading, nullptr);
    DomElement* ancestor = block(nullptr, "border:1px solid red;padding:2px", "div", body); ASSERT_NE(ancestor, nullptr);
    DomElement* first = block("A\nB\nC\nD\nE\nF\nG", nullptr, "p", ancestor); ASSERT_NE(first, nullptr);
    DomElement* empty = block(nullptr, "padding:3px 0;background:blue", "div", ancestor); ASSERT_NE(empty, nullptr);
    DomElement* second = block("H\nI\nJ\nK\nL\nM\nN\nO\nP", nullptr, "p", ancestor); ASSERT_NE(second, nullptr);
    DomElement* trailing = block("Tail", nullptr, "p", body); ASSERT_NE(trailing, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason; ASSERT_EQ(tree->model->page_count, 3u);
    for (DomElement* element : {labels[0], labels[1], leading, first, second, trailing}) {
        ASSERT_NE(element, nullptr); DomText* text = element->first_child->as_text(); ASSERT_NE(text, nullptr);
        ViewNodeState* state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr); size_t bytes = 0;
        for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) {
            EXPECT_EQ(part->text_start, bytes); bytes += part->text_length;
        }
        EXPECT_EQ(bytes, text->length);
    }
    ViewNodeState* state = view_tree_node_state(tree, ancestor, false); ASSERT_NE(state, nullptr);
    size_t boxes[3] = {};
    for (LayoutViewNode* part = state->first_occurrence; part; part = part->next_occurrence) {
        if (!part->paint_box) continue; uint32_t page = occurrence_page(part); ASSERT_GE(page, 1u); ASSERT_LE(page, 3u); boxes[page - 1]++;
        EXPECT_EQ(part->first_fragment, page == 1); EXPECT_EQ(part->last_fragment, page == 3);
        EXPECT_FLOAT_EQ(part->rect.width, page == 2 ? 80.0f : 100.0f); EXPECT_LE(part->rect.y + part->rect.height, 90.0f);
        ASSERT_NE(part->computed_boundary, nullptr); ASSERT_NE(part->computed_boundary->border, nullptr);
        EXPECT_FLOAT_EQ(part->computed_boundary->border->width.top, page == 1 ? 1.0f : 0.0f);
        EXPECT_FLOAT_EQ(part->computed_boundary->border->width.bottom, page == 3 ? 1.0f : 0.0f);
    }
    for (size_t count : boxes) EXPECT_EQ(count, 1u);
    LayoutViewNode* empty_box = source_fragment(tree, empty, VIEW_FRAGMENT_BODY, true); ASSERT_NE(empty_box, nullptr);
    EXPECT_FLOAT_EQ(empty_box->rect.height, 6.0f); EXPECT_EQ(occurrence_page(empty_box), 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, trailing, VIEW_FRAGMENT_BODY, true)), 3u);
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    const ViewPageBox* last = view_tree_page_material(retained, retained->model->pages.get()[2]); ASSERT_NE(last, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&last->node, text);
    EXPECT_NE(strstr(text->str, "Tail"), nullptr); EXPECT_EQ(strstr(text->str, "Lead"), nullptr); strbuf_free(text);
}

TEST_F(SecondaryViewTest, SplitTableCellsRetainNestedBlockDecorationsAndCompletedSourceCoverage) {
    stylesheet("@page { size:120px 80px; margin:10px } p,div { margin:0; font:10px/12px Arial } "
        "table { table-layout:fixed; width:100%; border-spacing:0 } td { padding:0; vertical-align:top } ");
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* a = block(nullptr, nullptr, "td", row); ASSERT_NE(a, nullptr);
    DomElement* long_block = block(nullptr, nullptr, "div", a); ASSERT_NE(long_block, nullptr);
    DomElement* inner = block("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ", "white-space:pre; orphans:1; widows:1", "div", long_block); ASSERT_NE(inner, nullptr);
    DomElement* b = block(nullptr, nullptr, "td", row); ASSERT_NE(b, nullptr);
    DomElement* short_block = block("Short", "border:1px solid blue; padding:2px", "div", b); ASSERT_NE(short_block, nullptr);
    for (bool clone : {false, true}) {
        SCOPED_TRACE(clone ? "cloned decoration" : "sliced decoration");
        ASSERT_TRUE(long_block->set_attribute("style", clone ? "border:1px solid red; padding:2px; box-decoration-break:clone" :
            "border:1px solid red; padding:2px"));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 3u);
        ViewNodeState* state = view_tree_node_state(tree, long_block, false); ASSERT_NE(state, nullptr); EXPECT_EQ(state->occurrence_count, 3u);
        size_t page = 0;
        for (LayoutViewNode* occurrence = state->first_occurrence; occurrence; occurrence = occurrence->next_occurrence, page++) {
            EXPECT_EQ(occurrence_page(occurrence), page + 1);
            EXPECT_EQ(occurrence->first_fragment, page == 0); EXPECT_EQ(occurrence->last_fragment, page == 2);
            ASSERT_NE(occurrence->computed_boundary, nullptr); ASSERT_NE(occurrence->computed_boundary->border, nullptr);
            EXPECT_FLOAT_EQ(occurrence->computed_boundary->border->width.top, clone || page == 0 ? 1.0f : 0.0f);
            EXPECT_FLOAT_EQ(occurrence->computed_boundary->border->width.bottom, clone || page == 2 ? 1.0f : 0.0f);
            EXPECT_FLOAT_EQ(occurrence->rect.height, page == 2 ? (clone ? 30.0f : 15.0f) : (clone ? 54.0f : page ? 60.0f : 51.0f));
            EXPECT_LE(occurrence->rect.y + occurrence->rect.height, 70.0f);
            ASSERT_NE(occurrence->first_child, nullptr); EXPECT_EQ(occurrence->first_child->source.address, inner);
            EXPECT_FLOAT_EQ(occurrence->first_child->rect.y, occurrence->rect.y + (clone || page == 0 ? 3.0f : 0.0f));
        }
        ViewNodeState* short_state = view_tree_node_state(tree, short_block, false); ASSERT_NE(short_state, nullptr);
        EXPECT_EQ(short_state->occurrence_count, 2u); // block box and its single line share source identity.
        size_t boxes = 0;
        for (LayoutViewNode* occurrence = short_state->first_occurrence; occurrence; occurrence = occurrence->next_occurrence) {
            EXPECT_EQ(occurrence_page(occurrence), 1u);
            if (occurrence->paint_box) {
                boxes++; EXPECT_TRUE(occurrence->first_fragment); EXPECT_TRUE(occurrence->last_fragment);
                EXPECT_FLOAT_EQ(occurrence->rect.height, 18.0f);
            }
        }
        EXPECT_EQ(boxes, 1u);
        ViewPreviewOptions preview = view_preview_options_default();
        ViewTree* retained = view_tree_page_instances_create(tree, nullptr, &preview); ASSERT_NE(retained, nullptr);
        ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 1;
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
        ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
        const ViewPageBox* page_box = view_tree_page_material(retained, retained->model->pages.get()[2]); ASSERT_NE(page_box, nullptr);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&page_box->node, text);
        EXPECT_NE(strstr(text->str, "J"), nullptr); EXPECT_EQ(strstr(text->str, "Short"), nullptr); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, FoAlignmentRefinesInheritedAutoRelativeAndRegionValues) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' display-align='center' relative-align='baseline' border-collapse='separate' "
        "font-family='Arial' font-size='7.5pt' line-height='9pt'><f:layout-master-set>"
        "<f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt' display-align='after'>"
        "<f:region-body display-align='inherit'/><f:region-before extent='15pt' display-align='auto'/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:static-content flow-name='xsl-region-before'><f:block>Head</f:block></f:static-content>"
        "<f:flow flow-name='xsl-region-body'><f:table table-layout='fixed' width='100%'><f:table-body><f:table-row>"
        "<f:table-cell><f:block>Center</f:block></f:table-cell>"
        "<f:table-cell display-align='auto'><f:block font-size='15pt' line-height='18pt'>B</f:block></f:table-cell>"
        "<f:table-cell display-align='before'><f:block>Before</f:block></f:table-cell>"
        "<f:table-cell display-align='auto' relative-align='before'><f:block>Relative</f:block></f:table-cell>"
        "</f:table-row></f:table-body></f:table></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr); doc.root = lam::up(generated);
    DomElement* cell = generated->last_child_element()->last_child_element()->first_child_element()->first_child_element()->first_child_element()->first_child_element();
    ASSERT_NE(cell, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    EXPECT_EQ(tree->model->pages.get()[0]->style->body_region->align, RADIANT_REGION_ALIGN_AFTER);
    EXPECT_EQ(tree->model->pages.get()[0]->style->edge_regions[0]->align, RADIANT_REGION_ALIGN_BEFORE);
    const CssEnum expected[] = {CSS_VALUE_MIDDLE, CSS_VALUE_BASELINE, CSS_VALUE_TOP, CSS_VALUE_TOP};
    for (size_t i = 0; i < 4; i++, cell = cell->next_sibling_element()) {
        ASSERT_NE(cell, nullptr); ViewCssStyle* style = view_css_resolve(tree, cell); ASSERT_NE(style, nullptr);
        EXPECT_TRUE(css_value_keyword_equals(style->vertical_align, expected[i])) << i;
        EXPECT_EQ(style->flow_traits && style->flow_traits->relative_cell_before, i == 3);
    }
    for (const char* property : {"display-align", "relative-align"}) {
        ASSERT_TRUE(fo->set_attribute(property, "bad"));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, fo);
        EXPECT_STREQ(translated->diagnostic.property, property);
        ASSERT_TRUE(fo->remove_attribute(property));
    }
}

TEST_F(SecondaryViewTest, RelativeCellBeforeAlignsContentEdgesSeparatelyFromBaselinesAndBorderEdges) {
    stylesheet("@page { size:220px 120px; margin:10px } div { margin:0; font:10px/12px Arial } "
        "table { table-layout:fixed; width:100%; border-spacing:0 } td { padding:0; vertical-align:top }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* cells[5] = {}, *children[5] = {};
    for (size_t i = 0; i < 5; i++) {
        cells[i] = block(nullptr, i == 1 ? "padding-top:8px" : i == 2 ? "padding-top:2px" :
            i == 3 ? "vertical-align:baseline" : i == 4 ? "vertical-align:baseline; padding-top:3px" : nullptr, "td", row);
        ASSERT_NE(cells[i], nullptr);
        if (i < 2) ASSERT_TRUE(cells[i]->set_attribute("r:cell-alignment", "relative-before"));
        children[i] = block("A", !i ? "padding-top:2px" : i == 3 ? "font-size:20px; line-height:24px" : nullptr, "div", cells[i]); ASSERT_NE(children[i], nullptr);
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    for (size_t i = 0; i < 3; i++) {
        LayoutViewNode* fragment = source_fragment(tree, children[i], VIEW_FRAGMENT_BODY, true); ASSERT_NE(fragment, nullptr);
        EXPECT_FLOAT_EQ(fragment->rect.y, !i ? 16.0f : i == 1 ? 18.0f : 12.0f);
        LayoutViewNode* text = source_fragment(tree, children[i]->first_child, VIEW_FRAGMENT_BODY, false); ASSERT_NE(text, nullptr);
        EXPECT_FLOAT_EQ(text->rect.y, i < 2 ? 18.0f : 12.0f);
    }
    LayoutViewNode* a = source_fragment(tree, children[3]->first_child, VIEW_FRAGMENT_BODY, false);
    LayoutViewNode* b = source_fragment(tree, children[4]->first_child, VIEW_FRAGMENT_BODY, false);
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(a->glyph_run, nullptr); ASSERT_NE(b->glyph_run, nullptr);
    EXPECT_FLOAT_EQ(a->glyph_run->baseline_y, b->glyph_run->baseline_y);
    ASSERT_TRUE(cells[0]->set_attribute("r:cell-alignment", "bad")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "cell alignment requires css or relative-before on a table cell"); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, RelativeCellBeforeReservesAlignmentAcrossCellContinuations) {
    stylesheet("@page { size:120px 80px; margin:10px } div { margin:0; font:10px/12px Arial } "
        "table { table-layout:fixed; width:100%; border-spacing:0 } td { padding:0; vertical-align:top }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* table = block(nullptr, nullptr, "table"); ASSERT_NE(table, nullptr);
    DomElement* row = block(nullptr, nullptr, "tr", table); ASSERT_NE(row, nullptr);
    DomElement* a = block(nullptr, nullptr, "td", row); ASSERT_NE(a, nullptr);
    DomElement* b = block(nullptr, "padding-top:8px", "td", row); ASSERT_NE(b, nullptr);
    for (DomElement* cell : {a, b}) {
        ASSERT_TRUE(cell->set_attribute("r:cell-alignment", "relative-before"));
        ASSERT_NE(block("A\nB\nC\nD\nE\nF\nG\nH", cell == a ? "white-space:pre; orphans:1; widows:1; padding-top:2px" :
            "white-space:pre; orphans:1; widows:1", "div", cell), nullptr);
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    for (DomElement* cell : {a, b}) {
        ViewNodeState* state = view_tree_node_state(tree, cell->first_child_element(), false); ASSERT_NE(state, nullptr);
        LayoutViewNode* first = nullptr; LayoutViewNode* last = nullptr; size_t boxes = 0;
        for (LayoutViewNode* node = state->first_occurrence; node; node = node->next_occurrence) if (node->paint_box) {
            if (!first) first = node;
            last = node; boxes++;
        }
        ASSERT_EQ(boxes, 2u); ASSERT_NE(first, nullptr); ASSERT_NE(last, nullptr);
        EXPECT_FLOAT_EQ(first->rect.y, cell == a ? 16.0f : 18.0f); EXPECT_FLOAT_EQ(last->rect.y, 10.0f);
        EXPECT_LE(first->rect.y + first->rect.height, 70.0f);
        StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(last, text);
        EXPECT_STREQ(text->str, "EFGH"); strbuf_free(text);
    }
}

TEST_F(SecondaryViewTest, BodyAlignmentMovesRepeatedTableFurnitureWithLateFloatsAndKeepsNotesFixed) {
    stylesheet("div, span { margin:0; font:10px/12px Arial; orphans:1; widows:1 } "
        "table { table-layout:fixed; width:100%; border-spacing:0 } td { padding:0; vertical-align:top }");
    DomElement* master = page_master("sheet", "size:200px 140px; margin:10px"); ASSERT_NE(master, nullptr);
    DomElement* region = page_region(master, "body", "main"); ASSERT_NE(region, nullptr);
    DomElement* sequence = page_sequence("sheet"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr); ASSERT_TRUE(flow->set_attribute("region-name", "main"));
    DomElement* table = block(nullptr, nullptr, "table", flow); ASSERT_NE(table, nullptr);
    DomElement* groups[2] = {};
    for (size_t i = 0; i < 2; i++) {
        groups[i] = block(nullptr, nullptr, i ? "tfoot" : "thead", table); ASSERT_NE(groups[i], nullptr);
        DomElement* row = block(nullptr, nullptr, "tr", groups[i]); ASSERT_NE(row, nullptr);
        DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr); ASSERT_NE(block(i ? "Foot" : "Head", nullptr, "div", cell), nullptr);
    }
    DomElement* body = block(nullptr, nullptr, "tbody", table); ASSERT_NE(body, nullptr);
    for (size_t i = 0; i < 10; i++) {
        DomElement* row = block(nullptr, nullptr, "tr", body); ASSERT_NE(row, nullptr);
        DomElement* cell = block(nullptr, nullptr, "td", row); ASSERT_NE(cell, nullptr); ASSERT_NE(block("Row", nullptr, "div", cell), nullptr);
    }
    DomElement* top = block("Top", "float:top; float-reference:page; height:12px", "div", flow); ASSERT_NE(top, nullptr);
    DomElement* bottom = block("Bottom", "float:bottom; float-reference:page; height:12px", "div", flow); ASSERT_NE(bottom, nullptr);
    DomElement* call = block("Call", nullptr, "div", flow); ASSERT_NE(call, nullptr);
    DomElement* note = block("Note", "float:footnote", "span", call); ASSERT_NE(note, nullptr);
    const char* alignments[] = {"before", "center", "after"};
    ViewTree* tree = secondary(); ViewTree* preview = nullptr;
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    for (size_t i = 0; i < 3; i++) {
        SCOPED_TRACE(alignments[i]); ASSERT_TRUE(region->set_attribute("display-align", alignments[i]));
        if (i) ASSERT_TRUE(view_tree_model_reset(tree));
        ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
        ASSERT_EQ(tree->model->page_count, 2u);
        float offset = static_cast<float>(i) * 12.0f;
        for (size_t group = 0; group < 2; group++) {
            ViewNodeState* state = view_tree_node_state(tree, groups[group], false); ASSERT_NE(state, nullptr);
            LayoutViewNode* repeated = state->last_occurrence; ASSERT_NE(repeated, nullptr);
            EXPECT_EQ(occurrence_page(repeated), 2u); EXPECT_EQ(repeated->role, VIEW_FRAGMENT_REPEATED_TABLE);
            EXPECT_FLOAT_EQ(repeated->rect.y, (group ? 58.0f : 22.0f) + offset);
            ASSERT_NE(repeated->first_child, nullptr); EXPECT_FLOAT_EQ(repeated->first_child->rect.y, repeated->rect.y);
        }
        LayoutViewNode* floating = source_fragment(tree, top, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(floating, nullptr);
        EXPECT_FLOAT_EQ(floating->rect.y, 10.0f);
        floating = source_fragment(tree, bottom, VIEW_FRAGMENT_FLOAT, true); ASSERT_NE(floating, nullptr); EXPECT_FLOAT_EQ(floating->rect.y, 106.0f);
        LayoutViewNode* inserted = source_fragment(tree, note, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(inserted, nullptr); EXPECT_FLOAT_EQ(inserted->rect.y, 118.0f);
        LayoutViewNode* paragraph = source_fragment(tree, call, VIEW_FRAGMENT_BODY, true); ASSERT_NE(paragraph, nullptr); EXPECT_FLOAT_EQ(paragraph->rect.y, 70.0f + offset);
        if (i == 1) {
            ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
            preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
        }
    }
    ASSERT_TRUE(region->set_attribute("display-align", "auto")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "invalid logical region display alignment");
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    const ViewPageBox* retained = view_tree_page_material(preview, preview->model->pages.get()[1]); ASSERT_NE(retained, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&retained->node, text);
    EXPECT_NE(strstr(text->str, "Head"), nullptr); EXPECT_NE(strstr(text->str, "Note"), nullptr); strbuf_free(text);
}

TEST_F(SecondaryViewTest, FoTableValidationKeepsOriginalSourcesAndNeverChangesTheInitialBorderModel) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format'><f:layout-master-set>"
        "<f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:table><f:table-column/><f:table-body><f:table-row><f:table-cell><f:block>Text</f:block></f:table-cell></f:table-row></f:table-body></f:table>"
        "</f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    DomElement* original_table = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original_table, nullptr);
    DomElement* column = original_table->first_child_element(); ASSERT_NE(column, nullptr);
    DomElement* cell = original_table->last_child_element()->first_child_element()->first_child_element(); ASSERT_NE(cell, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default();
    RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); ASSERT_NE(diagnostic.origin, nullptr);
    EXPECT_EQ(diagnostic.origin->source.address, original_table); EXPECT_STREQ(diagnostic.reason, "collapsed table borders require fragment conflict resolution");
    const struct { DomElement* source; const char* property; const char* value; } invalid[] = {
        {column, "number-columns-repeated", "1001"}, {column, "number-columns-repeated", "1e100"},
        {column, "number-columns-repeated", "2x"}, {cell, "number-columns-spanned", "1001"},
        {cell, "number-rows-spanned", "65535"}, {cell, "display-align", "baseline"},
        {original_table, "table-omit-header-at-break", "sometimes"}, {column, "column-number", "1001"}
    };
    for (const auto& test : invalid) {
        SCOPED_TRACE(test.property); ASSERT_TRUE(test.source->set_attribute(test.property, test.value));
        translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        EXPECT_EQ(translated->root, nullptr); EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID);
        EXPECT_EQ(translated->diagnostic.source.address, test.source); EXPECT_STREQ(translated->diagnostic.property, test.property);
        ASSERT_TRUE(test.source->remove_attribute(test.property));
    }
    ASSERT_NE(block(nullptr, nullptr, "f:table-header", original_table), nullptr);
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->root, nullptr); EXPECT_EQ(translated->diagnostic.source.address, original_table->last_child_element());
}

TEST_F(SecondaryViewTest, FoTranslationRejectsNamespaceRebindingUnsupportedTraitsAndBudgets) {
    DomElement* fo = block(nullptr, nullptr, "f:root"); ASSERT_NE(fo, nullptr);
    ASSERT_TRUE(fo->set_attribute("xmlns:f", RADIANT_FO_NAMESPACE));
    DomElement* masters = block(nullptr, nullptr, "f:layout-master-set", fo); ASSERT_NE(masters, nullptr);
    DomElement* master = block(nullptr, nullptr, "f:simple-page-master", masters); ASSERT_NE(master, nullptr);
    ASSERT_TRUE(master->set_attribute("master-name", "sheet")); ASSERT_TRUE(master->set_attribute("page-width", "150pt")); ASSERT_TRUE(master->set_attribute("page-height", "90pt"));
    DomElement* body = block(nullptr, nullptr, "f:region-body", master); ASSERT_NE(body, nullptr);
    DomElement* sequence = block(nullptr, nullptr, "f:page-sequence", fo); ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(sequence->set_attribute("master-reference", "sheet"));
    DomElement* flow = block(nullptr, nullptr, "f:flow", sequence); ASSERT_NE(flow, nullptr); ASSERT_TRUE(flow->set_attribute("flow-name", "xsl-region-body"));
    DomElement* content = block("Text", nullptr, "f:block", flow); ASSERT_NE(content, nullptr);
    RadiantFoOptions options = radiant_fo_options_default();
    ASSERT_TRUE(content->set_attribute("writing-mode", "tb-rl"));
    RadiantFoTranslation* result = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->root, nullptr); EXPECT_EQ(result->diagnostic.source.address, content); EXPECT_STREQ(result->diagnostic.property, "writing-mode");
    ASSERT_TRUE(content->remove_attribute("writing-mode")); ASSERT_TRUE(content->set_attribute("xmlns:f", "urn:other"));
    result = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->root, nullptr); EXPECT_EQ(result->diagnostic.source.address, content);
    ASSERT_TRUE(content->remove_attribute("xmlns:f")); options.max_nodes = 2;
    result = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->root, nullptr); EXPECT_EQ(result->diagnostic.status, TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(result->diagnostic.source.address, master);
    options.max_nodes = radiant_fo_options_default().max_nodes;
    ASSERT_TRUE(content->set_attribute("id", "duplicate")); ASSERT_TRUE(sequence->set_attribute("id", "duplicate"));
    result = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->root, nullptr); EXPECT_EQ(result->diagnostic.source.address, content); EXPECT_STREQ(result->diagnostic.property, "id");
    ASSERT_TRUE(content->set_attribute("id", "inherit"));
    result = radiant_fo_translate(&doc, fo, &options); ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->root, nullptr); EXPECT_EQ(result->diagnostic.source.address, content); EXPECT_STREQ(result->diagnostic.property, "id");
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

TEST_F(SecondaryViewTest, NativeSpaceRangesPackToFitAndRestoreAcrossPageTrials) {
    stylesheet("@page { size: 200px 100px; margin: 10px } p, div { margin: 0; font: 10px/20px Arial; white-space: pre-wrap; orphans: 1; widows: 1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* first = block("First"); DomElement* second = block("Second\nThird");
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    ASSERT_TRUE(first->set_attribute("r:space-after.minimum", "10px"));
    ASSERT_TRUE(first->set_attribute("r:space-after.optimum", "30px"));
    ASSERT_TRUE(first->set_attribute("r:space-after.maximum", "40px"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u);
    LayoutViewNode* a = source_fragment(tree, first, VIEW_FRAGMENT_BODY, true), *b = source_fragment(tree, second, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr);
    EXPECT_FLOAT_EQ(a->rect.y, 10.0f); EXPECT_FLOAT_EQ(a->rect.height, 20.0f);
    EXPECT_FLOAT_EQ(b->rect.y, 50.0f); EXPECT_FLOAT_EQ(b->rect.height, 40.0f);
    ViewCssStyle* style = view_css_resolve(tree, first); ASSERT_NE(style->flow_traits.get(), nullptr);
    EXPECT_FLOAT_EQ(style->flow_traits->after.minimum, 10.0f); EXPECT_FLOAT_EQ(style->flow_traits->after.optimum, 30.0f);
    ASSERT_TRUE(second->set_attribute("style", "break-before: page")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    b = source_fragment(tree, second, VIEW_FRAGMENT_BODY, true); ASSERT_NE(b, nullptr);
    EXPECT_EQ(occurrence_page(b), 2u); EXPECT_FLOAT_EQ(b->rect.y, 10.0f);
}

TEST_F(SecondaryViewTest, NativeSpaceStacksPreserveEmptyAncestorsAndNamespaceAliases) {
    stylesheet("@page { size: 200px 160px; margin: 10px } p, div { margin: 0; font: 10px/20px Arial; orphans: 1; widows: 1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:pub", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr), *child = block("First", nullptr, "div", parent);
    DomElement* empty = block(nullptr), *last = block("Last");
    ASSERT_NE(parent, nullptr); ASSERT_NE(child, nullptr); ASSERT_NE(empty, nullptr); ASSERT_NE(last, nullptr);
    ASSERT_TRUE(parent->set_attribute("pub:space-before", "14px")); ASSERT_TRUE(parent->set_attribute("pub:space-before.conditionality", "retain"));
    ASSERT_TRUE(child->set_attribute("pub:space-before", "16px")); ASSERT_TRUE(child->set_attribute("pub:space-after", "5px"));
    ASSERT_TRUE(child->set_attribute("pub:space-after.precedence", "force")); ASSERT_TRUE(empty->set_attribute("pub:space-before", "7px"));
    ASSERT_TRUE(empty->set_attribute("pub:space-before.precedence", "force")); ASSERT_TRUE(last->set_attribute("pub:space-before", "30px"));
    ASSERT_TRUE(last->set_attribute("xmlns:pub", "urn:unrelated"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* p = source_fragment(tree, parent, VIEW_FRAGMENT_BODY, true), *c = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true);
    LayoutViewNode* e = source_fragment(tree, empty, VIEW_FRAGMENT_BODY, true), *l = source_fragment(tree, last, VIEW_FRAGMENT_BODY, true);
    ASSERT_NE(p, nullptr); ASSERT_NE(c, nullptr); ASSERT_NE(e, nullptr); ASSERT_NE(l, nullptr);
    EXPECT_FLOAT_EQ(p->rect.y, 26.0f); EXPECT_FLOAT_EQ(c->rect.y, 26.0f); EXPECT_FLOAT_EQ(p->rect.height, 20.0f);
    EXPECT_FLOAT_EQ(e->rect.y, 58.0f); EXPECT_FLOAT_EQ(l->rect.y, 58.0f);
    EXPECT_EQ(view_css_resolve(tree, last)->flow_traits.get(), nullptr);
}

TEST_F(SecondaryViewTest, NativeKeepStrengthsChooseTheWeakestBoundaryAndForcedBreaksOverrideAlways) {
    stylesheet("@page { size: 200px 80px; margin: 10px } p, div { margin: 0; font: 10px/20px Arial; orphans: 1; widows: 1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* a = block("A"), *b = block("B"), *c = block("C"), *d = block("D");
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(c, nullptr); ASSERT_NE(d, nullptr);
    ASSERT_TRUE(a->set_attribute("r:keep-with-next.within-page", "90"));
    ASSERT_TRUE(b->set_attribute("r:keep-with-next.within-page", "10"));
    ASSERT_TRUE(c->set_attribute("r:keep-with-next.within-column", "2147483647"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, b, VIEW_FRAGMENT_BODY, true)), 1u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, c, VIEW_FRAGMENT_BODY, true)), 2u);
    EXPECT_EQ(view_css_resolve(tree, c)->flow_traits->next.scope[1].value, INT32_MAX);
    ASSERT_TRUE(b->set_attribute("r:keep-with-next.within-page", "always")); ASSERT_TRUE(c->set_attribute("style", "break-before: page"));
    ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(occurrence_page(source_fragment(tree, b, VIEW_FRAGMENT_BODY, true)), 1u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, c, VIEW_FRAGMENT_BODY, true)), 2u);
}

TEST_F(SecondaryViewTest, NativeLineKeepsMoveTheCompleteInlineAndPreserveForcedLineBreaks) {
    stylesheet("@page { size: 100px 100px; margin: 10px } p, div, span { margin: 0; font: 10px/20px Arial; orphans: 1; widows: 1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* paragraph = block("X X X X X "); DomElement* span = block("Alpha Beta", nullptr, "span", paragraph);
    ASSERT_NE(paragraph, nullptr); ASSERT_NE(span, nullptr);
    ASSERT_TRUE(span->set_attribute("r:keep-together.within-line", "always"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewNodeState* text = view_tree_node_state(tree, span->first_child, false); ASSERT_NE(text, nullptr);
    float y = text->first_occurrence->rect.y;
    for (LayoutViewNode* part = text->first_occurrence; part; part = part->next_occurrence) EXPECT_FLOAT_EQ(part->rect.y, y);
    ViewNodeState* prefix = view_tree_node_state(tree, paragraph->first_child, false); ASSERT_NE(prefix, nullptr);
    EXPECT_LT(prefix->first_occurrence->rect.y, y);
    ASSERT_NE(block(nullptr, nullptr, "br", span), nullptr); ASSERT_NE(block("After", nullptr, "span", span), nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
}

TEST_F(SecondaryViewTest, NativeFlowTraitsInheritComputedValuesAndRejectInvalidComponents) {
    stylesheet("@page { size: 200px 160px; margin: 10px } p, div { margin: 0; font: 10px/20px Arial }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* parent = block(nullptr), *child = block("Text", "font-size: 2em", "div", parent);
    ASSERT_NE(parent, nullptr); ASSERT_NE(child, nullptr);
    ASSERT_TRUE(parent->set_attribute("r:space-before", "2em")); ASSERT_TRUE(parent->set_attribute("r:space-before.conditionality", "retain"));
    ASSERT_TRUE(parent->set_attribute("r:keep-together.within-page", "-2147483648"));
    ASSERT_TRUE(child->set_attribute("r:space-before", "inherit")); ASSERT_TRUE(child->set_attribute("r:keep-together.within-page", "inherit"));
    ViewTree* tree = secondary(); ViewCssStyle* style = view_css_resolve(tree, child); ASSERT_NE(style, nullptr);
    ASSERT_NE(style->flow_traits.get(), nullptr); EXPECT_FLOAT_EQ(style->font.font_size, 20.0f);
    EXPECT_FLOAT_EQ(style->flow_traits->before.optimum, 20.0f);
    EXPECT_EQ(style->flow_traits->together.scope[2].value, INT32_MIN);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* fragment = source_fragment(tree, child, VIEW_FRAGMENT_BODY, true); ASSERT_NE(fragment, nullptr);
    EXPECT_FLOAT_EQ(fragment->rect.y, 30.0f);
    ASSERT_TRUE(child->set_attribute("r:space-before", "10%")); ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, child); EXPECT_NE(strstr(diagnostic.reason, "length"), nullptr);
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, NativeAlwaysKeepsRejectImpossibleGroupsWithoutPublishingPartialPages) {
    stylesheet("@page { size: 200px 80px; margin: 10px } p, div { margin: 0; font: 10px/20px Arial; orphans: 1; widows: 1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* group = block(nullptr); ASSERT_NE(group, nullptr);
    ASSERT_TRUE(group->set_attribute("r:keep-together.within-page", "always"));
    for (size_t i = 0; i < 4; i++) ASSERT_NE(block("Line", nullptr, "div", group), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_EQ(diagnostic.relaxed_avoidance, 0u);
    ASSERT_TRUE(group->set_attribute("r:keep-together.within-page", "7")); ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u);
}

TEST_F(SecondaryViewTest, NativePageMastersUseTheCommonCssCascadeAndMarginContexts) {
    stylesheet("p, div { margin: 0; font: 10px/12px Arial, sans-serif } "
        "@page { size: 90px 90px; margin: 0 } @page chapter:left { margin-left: 15px }");
    DomElement* set = block(nullptr, nullptr, "page:master-set"); ASSERT_NE(set, nullptr);
    ASSERT_TRUE(set->set_attribute("xmlns:page", RADIANT_PAGE_NAMESPACE));
    DomElement* master = block(nullptr, "size: 180pt 120pt; margin: 10%; --ink: blue; color: var(--ink)", "page:page-master", set);
    ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* region = block(nullptr, "content: counter(page); font-size: 8px", "page:region", master);
    ASSERT_NE(region, nullptr); ASSERT_TRUE(region->set_attribute("box", "bottom-center"));
    DomElement* seq = block(nullptr, nullptr, "page:page-sequence"); ASSERT_NE(seq, nullptr);
    ASSERT_TRUE(seq->set_attribute("xmlns:page", RADIANT_PAGE_NAMESPACE));
    ASSERT_TRUE(seq->set_attribute("master-reference", "chapter"));
    ASSERT_NE(block("First sheet", "height: 20px", "div", seq), nullptr);
    ASSERT_NE(block("Second sheet", "height: 20px; break-before: page", "div", seq), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    EXPECT_EQ(tree->model->css->page_document->rule_count, 3u);
    EXPECT_EQ(view_tree_node_state(tree, master, false), nullptr);
    for (size_t i = 0; i < 2; i++) {
        ViewPageBox* page = tree->model->pages.get()[i];
        EXPECT_FLOAT_EQ(page->node.rect.width, 240.0f); EXPECT_FLOAT_EQ(page->node.rect.height, 160.0f);
        EXPECT_FLOAT_EQ(page->content_rect.x, i ? 15.0f : 24.0f);
        EXPECT_FLOAT_EQ(page->content_rect.y, 16.0f);
        ASSERT_NE(page->style->computed_style, nullptr);
        EXPECT_EQ(page->style->computed_style->color.b, 255);
        EXPECT_NE(page->margin_boxes[CSS_PAGE_BOTTOM_CENTER], nullptr);
    }
    EXPECT_FLOAT_EQ(source->width, 640.0f); EXPECT_EQ(master->parent.get(), set);
}

TEST_F(SecondaryViewTest, NativeBodyRegionsKeepGeometryAndBackgroundDistinctFromCssMarginBoxes) {
    stylesheet("p, div { margin: 0; font: 10px/12px Arial, sans-serif }");
    DomElement* set = block(nullptr, nullptr, "master-set"); ASSERT_NE(set, nullptr);
    ASSERT_TRUE(set->set_attribute("xmlns", RADIANT_PAGE_NAMESPACE));
    DomElement* master = block(nullptr, "size: 200px 160px; margin: 10px", "page-master", set);
    ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "doc:Sheet"));
    DomElement* region = block(nullptr, "margin: 7px 5px; background-color: blue", "region", master);
    ASSERT_NE(region, nullptr); ASSERT_TRUE(region->set_attribute("role", "body")); ASSERT_TRUE(region->set_attribute("name", "Main"));
    DomElement* seq = block(nullptr, nullptr, "r:page-sequence"); ASSERT_NE(seq, nullptr);
    ASSERT_TRUE(seq->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE)); ASSERT_TRUE(seq->set_attribute("master-reference", "doc:Sheet"));
    DomElement* flow = block(nullptr, nullptr, "r:flow", seq); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "Main"));
    DomElement* content = block("Body", nullptr, "div", flow); ASSERT_NE(content, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u); ViewPageBox* page = tree->model->pages.get()[0];
    EXPECT_FLOAT_EQ(page->style->margin[0], 10.0f);
    EXPECT_FLOAT_EQ(page->content_rect.x, 15.0f); EXPECT_FLOAT_EQ(page->content_rect.y, 17.0f);
    EXPECT_FLOAT_EQ(page->content_rect.width, 170.0f); EXPECT_FLOAT_EQ(page->content_rect.height, 126.0f);
    ASSERT_NE(page->style->body_region, nullptr); EXPECT_STREQ(page->style->body_region->name, "Main");
    LayoutViewNode* box = source_fragment(tree, content, VIEW_FRAGMENT_BODY, true); ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->rect.x, 15.0f); EXPECT_FLOAT_EQ(box->rect.y, 17.0f);
    init_vector_engine();
    ImageSurface* image = render_secondary_page_snapshot(tree, 1, 1.0f); ASSERT_NE(image, nullptr);
    EXPECT_EQ(snapshot_pixel(image, 11, 11), 0xffffffffu); EXPECT_EQ(snapshot_pixel(image, 20, 50), 0xffff0000u);
    image_surface_destroy(image);
    ASSERT_TRUE(flow->set_attribute("region-name", "missing")); ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, flow); EXPECT_STREQ(diagnostic.reason, "flow references an unknown body region");
}

TEST_F(SecondaryViewTest, NativePageMasterSnapshotsSurviveSourceRestyleAndGenerationReset) {
    stylesheet("p, div { margin: 0; font: 10px/12px Arial, sans-serif }");
    DomElement* master = block(nullptr, "size: 140px 100px; margin: 10px; background-color: blue", "r:page-master");
    ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    ASSERT_TRUE(master->set_attribute("name", "sheet"));
    DomElement* seq = block(nullptr, nullptr, "r:page-sequence"); ASSERT_NE(seq, nullptr);
    ASSERT_TRUE(seq->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE)); ASSERT_TRUE(seq->set_attribute("master-reference", "sheet"));
    ASSERT_NE(block("Retained sheet", nullptr, "div", seq), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(master->set_attribute("style", "size: 180px 120px; margin: 15px; background-color: red"));
    ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->node.rect.width, 180.0f);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    init_vector_engine();
    ImageSurface* image = render_secondary_page_snapshot(preview, 1, 1.0f); ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->width, 140); EXPECT_EQ(image->height, 100);
    EXPECT_EQ(snapshot_pixel(image, 1, 1), 0xffff0000u);
    image_surface_destroy(image);
}

TEST_F(SecondaryViewTest, NativePageControlsRejectDuplicateMastersAndDanglingReferences) {
    stylesheet("p, div { margin: 0 }");
    DomElement* master = block(nullptr, "size: 100px 100px", "r:page-master"); ASSERT_NE(master, nullptr);
    ASSERT_TRUE(master->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE)); ASSERT_TRUE(master->set_attribute("name", "sheet"));
    DomElement* duplicate = block(nullptr, "size: 120px 100px", "r:page-master"); ASSERT_NE(duplicate, nullptr);
    ASSERT_TRUE(duplicate->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE)); ASSERT_TRUE(duplicate->set_attribute("name", "sheet"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, duplicate); EXPECT_STREQ(diagnostic.reason, "duplicate native page master name");
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(duplicate->set_attribute("name", "other"));
    DomElement* seq = block("Unbound flow", nullptr, "r:page-sequence"); ASSERT_NE(seq, nullptr);
    ASSERT_TRUE(seq->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE)); ASSERT_TRUE(seq->set_attribute("master-reference", "missing"));
    ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, seq); EXPECT_STREQ(diagnostic.reason, "page sequence references an unknown master");
    EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, PageCascadeResolvesNamedSidedAndPercentageMargins) {
    stylesheet("@page { size: 320px 480px; margin: 10%; @bottom-center { content: counter(page) } } "
        "@page :left { margin-left: 24px; } @page chapter { size: A4 landscape; margin: 12mm; } "
        "@page chapter:first { margin-top: 24mm; } @page :blank { @bottom-center { content: none } }");
    ViewTree* tree = secondary();
    ViewPageStyle style = {};
    ASSERT_EQ(view_css_page_style(tree, nullptr, 2, VIEW_PAGE_LEFT, false, &style), VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(style.width, 320.0f);
    EXPECT_FLOAT_EQ(style.height, 480.0f);
    EXPECT_FLOAT_EQ(style.margin[0], 48.0f);
    EXPECT_FLOAT_EQ(style.margin[1], 32.0f);
    EXPECT_FLOAT_EQ(style.margin[3], 24.0f);
    EXPECT_FLOAT_EQ(style.content_rect.width, 264.0f);
    EXPECT_FLOAT_EQ(style.content_rect.height, 384.0f);
    ASSERT_NE(style.margin_content[CSS_PAGE_BOTTOM_CENTER], nullptr);
    ASSERT_EQ(view_css_page_style(tree, "chapter", 1, VIEW_PAGE_RIGHT, false, &style), VIEW_MODEL_OK);
    EXPECT_NEAR(style.width, 297.0f * 96.0f / 25.4f, 0.001f);
    EXPECT_NEAR(style.height, 210.0f * 96.0f / 25.4f, 0.001f);
    EXPECT_NEAR(style.margin[0], 24.0f * 96.0f / 25.4f, 0.001f);
    ASSERT_EQ(view_css_page_style(tree, nullptr, 3, VIEW_PAGE_RIGHT, true, &style), VIEW_MODEL_OK);
    ASSERT_NE(style.margin_content[CSS_PAGE_BOTTOM_CENTER], nullptr);
    EXPECT_TRUE(css_value_is_none(style.margin_content[CSS_PAGE_BOTTOM_CENTER]->value));
}

TEST_F(SecondaryViewTest, PageGeometryUsesPageVariablesWithoutChangingSemanticInheritance) {
    stylesheet("p { --paper: 500px 500px; --edge: 3px; --tint: red; font-size: 10px; margin: 0 } "
        "@page { --paper: 240px 120px; --edge: 20px; --tint: blue; size: var(--paper); "
        "margin: var(--edge); background-color: var(--tint) } "
        "@page :left { --paper: 260px 140px; --edge: 10px; --tint: green }");
    ViewTree* tree = secondary(); ViewPageStyle first = {}, second = {};
    ASSERT_EQ(view_css_page_style(tree, nullptr, 1, VIEW_PAGE_RIGHT, false, &first), VIEW_MODEL_OK);
    ASSERT_EQ(view_css_page_style(tree, nullptr, 2, VIEW_PAGE_LEFT, false, &second), VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(first.width, 240.0f); EXPECT_FLOAT_EQ(first.height, 120.0f);
    EXPECT_FLOAT_EQ(first.content_rect.x, 20.0f); EXPECT_FLOAT_EQ(first.content_rect.width, 200.0f);
    EXPECT_EQ(first.background.b, 255); EXPECT_EQ(first.background.r, 0);
    EXPECT_FLOAT_EQ(second.width, 260.0f); EXPECT_FLOAT_EQ(second.height, 140.0f);
    EXPECT_FLOAT_EQ(second.content_rect.x, 10.0f); EXPECT_FLOAT_EQ(second.content_rect.width, 240.0f);
    EXPECT_EQ(second.background.g, 128); EXPECT_EQ(second.background.r, 0);
    ViewCssStyle* semantic = view_css_resolve(tree, source); ASSERT_NE(semantic, nullptr);
    EXPECT_FLOAT_EQ(view_css_length(tree, semantic, view_css_property(tree, semantic, "--edge"),
        CSS_PROPERTY_MARGIN_LEFT, 100.0f, 100.0f), 3.0f);
    EXPECT_FLOAT_EQ(source->width, 640.0f); EXPECT_FLOAT_EQ(source->x, 11.25f);
    ASSERT_NE(block("First sheet"), nullptr);
    ASSERT_NE(block("Second sheet", "break-before: page"), nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ASSERT_TRUE(create_dir("temp/paged-media-area-text"));
    EXPECT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-area-text/page-geometry.pdf"));
    init_vector_engine();
    for (uint32_t page_number = 1; page_number <= 2; page_number++) {
        ImageSurface* surface = render_secondary_page_snapshot(tree, page_number, 1.0f); ASSERT_NE(surface, nullptr);
        EXPECT_EQ(surface->width, page_number == 1 ? 240 : 260);
        EXPECT_EQ(surface->height, page_number == 1 ? 120 : 140);
        EXPECT_EQ(snapshot_pixel(surface, 1, 1), page_number == 1 ? 0xffff0000u : 0xff008000u);
        save_surface_to_png(surface, page_number == 1 ? "temp/paged-media-area-text/page-geometry-native-1.png" :
            "temp/paged-media-area-text/page-geometry-native-2.png"); image_surface_destroy(surface);
    }
}

TEST_F(SecondaryViewTest, PageSizeAndEdgesUseThePageFontAcrossNamedAndSidedContexts) {
    stylesheet("p { font-size: 10px } @page { size: 24em 12em; margin: 1em; font-size: 10px } "
        "@page :left { font-size: 20px } @page square { size: 12em; font-size: 15px }");
    ViewTree* tree = secondary(); ViewPageStyle first = {}, second = {}, square = {};
    ASSERT_EQ(view_css_page_style(tree, nullptr, 1, VIEW_PAGE_RIGHT, false, &first), VIEW_MODEL_OK);
    ASSERT_EQ(view_css_page_style(tree, nullptr, 2, VIEW_PAGE_LEFT, false, &second), VIEW_MODEL_OK);
    ASSERT_EQ(view_css_page_style(tree, "square", 3, VIEW_PAGE_RIGHT, false, &square), VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(first.width, 240.0f); EXPECT_FLOAT_EQ(first.height, 120.0f);
    EXPECT_FLOAT_EQ(first.margin[0], 10.0f);
    EXPECT_FLOAT_EQ(second.width, 480.0f); EXPECT_FLOAT_EQ(second.height, 240.0f);
    EXPECT_FLOAT_EQ(second.margin[0], 20.0f);
    EXPECT_FLOAT_EQ(square.width, 180.0f); EXPECT_FLOAT_EQ(square.height, 180.0f);
    EXPECT_FLOAT_EQ(square.margin[0], 15.0f);
    EXPECT_FLOAT_EQ(view_css_resolve(tree, source)->font.font_size, 10.0f);
}

TEST_F(SecondaryViewTest, ForcedBreaksProducePhysicalPagesAndNoTrailingBlank) {
    stylesheet("@page { size: 320px 480px; margin: 40px } p { margin: 0; font-size: 12px; line-height: 16px } "
        "div { height: 120px } div + div { break-before: page } div:last-child { break-after: page }");
    for (const char* text : {"First page.", "Second page.", "Third page."}) {
        DomElement* section = DomElement::create(&doc, "div", elmt_arena(doc.node_arena));
        ASSERT_NE(section, nullptr);
        ASSERT_TRUE(source->DomNode::append_child(section));
        DomText* content = DomText::create_copy(text, strlen(text), section);
        ASSERT_NE(content, nullptr);
        ASSERT_TRUE(section->DomNode::append_child(content));
    }
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    for (size_t i = 0; i < tree->model->page_count; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        EXPECT_FLOAT_EQ(page->node.rect.width, 320.0f);
        EXPECT_FLOAT_EQ(page->node.rect.height, 480.0f);
        EXPECT_FLOAT_EQ(page->content_rect.width, 240.0f);
        EXPECT_EQ(page->side, i % 2 ? VIEW_PAGE_LEFT : VIEW_PAGE_RIGHT);
        EXPECT_FALSE(page->blank);
    }
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    EXPECT_FLOAT_EQ(source->x, 11.25f);
    PaintList paint = {}; paint_list_init(&paint, nullptr);
    ASSERT_TRUE(layout_secondary_paint_page(tree, tree->model->pages.get()[0], &paint));
    EXPECT_TRUE(paint_ir_validate_or_log(&paint, "paged test"));
    EXPECT_GT(paint.item_count(), 3);
    paint_list_destroy(&paint);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    ViewPageRange range = {2, 2};
    ViewPageSelection selection = {false, &range, 1};
    ViewPreviewOptions preview = view_preview_options_default();
    preview.rows = preview.columns = 2; preview.scale = 0.25f;
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &preview), VIEW_MODEL_OK);
    ASSERT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/three-pages.pdf"));
    char* pdf = read_text_file("temp/paged-media-impl/three-pages.pdf");
    ASSERT_NE(pdf, nullptr);
    EXPECT_NE(strstr(pdf, "/Count 3"), nullptr);
    EXPECT_NE(strstr(pdf, "/MediaBox [0 0 240.00 360.00]"), nullptr);
    free(pdf);
    ASSERT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/selected-page.pdf", &selection));
    pdf = read_text_file("temp/paged-media-impl/selected-page.pdf");
    ASSERT_NE(pdf, nullptr);
    EXPECT_NE(strstr(pdf, "/Count 1"), nullptr);
    free(pdf);
}

enum NativeAssemblyMode { NATIVE_ASSEMBLY_NONE, NATIVE_ASSEMBLY_HOLD_REINSERT, NATIVE_ASSEMBLY_STUCK,
    NATIVE_ASSEMBLY_CYCLE, NATIVE_ASSEMBLY_ENDLESS, NATIVE_ASSEMBLY_FAIL, NATIVE_ASSEMBLY_PER_PAGE };
struct NativeFragmentFixture {
    RefCount references;
    bool admit;
    size_t retained, released;
    NativeRegionFixture exact;
    FontContext* fonts;
    FontProp font;
    FontBox box;
    PaintGlyphRun run;
    ImageSurface* image;
    PaintImageBox image_box;
    uint32_t glyphs[2];
    float xs[2], ys[2];
    TypesetContribution contributions[16];
    TypesetItem items[4];
    TypesetParagraph paragraph;
    TypesetMark marks[3];
    TypesetTarget targets[2];
    NativeRegionFixture region_records[2];
    TypesetRegionMaterial regions[2];
    NativeRegionFixture solutions[20];
    TypesetResume active;
    size_t contribution_count, restore_count, committed_pages;
    size_t assembly_phase, assembly_calls;
    uint32_t assembly_page, fail_page;
    TypesetPageKind page_kinds[16];
    uint32_t page_numbers[16];
    NativeAssemblyMode assembly_mode;
    bool assembly_seen_exact;
    bool first_candidate, fail_commit, fail_region_paint;
};
static bool native_fragment_retain(void* context) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    if (!fixture->admit || !ref_count_retain(&fixture->references)) return false;
    fixture->retained++; return true;
}
static void native_fragment_release(void* context) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    fixture->released++;
    if (ref_count_release(&fixture->references) != REF_COUNT_LAST) return;
    if (fixture->font.font_handle) font_handle_release(fixture->font.font_handle);
    if (fixture->fonts) font_context_destroy(fixture->fonts);
    if (fixture->image) image_surface_destroy(fixture->image);
    lam::free_owned(fixture->run.owned_text);
    free(fixture);
}
static NativeFragmentFixture* native_fragment_fixture() {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)calloc(1, sizeof(NativeFragmentFixture));
    if (!fixture) return nullptr;
    ref_count_init(&fixture->references); fixture->admit = true;
    fixture->exact.provider = 917; fixture->exact.scaled_points = UINT64_C(9007199254740993);
    return fixture;
}
static ViewNativeMaterial native_fragment_material(NativeFragmentFixture* fixture) {
    ViewNativeMaterial material = {};
    material.source = {fixture->exact.provider, 5, 81, TYPESET_PROVIDER_OFFSETS, lam::up((const TypesetRecord*)&fixture->exact)};
    material.metrics = {80, 24, 7, 24, {0, -24, 80, 31}, lam::up((const TypesetRecord*)&fixture->exact)};
    material.start = 13; material.length = 29;
    material.owner = {fixture, native_fragment_retain, native_fragment_release};
    return material;
}

static void native_fragment_paint_fixture(NativeFragmentFixture* fixture) {
    FontContextConfig config = {}; config.pixel_ratio = 1;
    fixture->fonts = font_context_create(&config); ASSERT_NE(fixture->fonts, nullptr);
    FontStyleDesc description = {"Arial", 18, FONT_WEIGHT_NORMAL, FONT_SLANT_NORMAL, nullptr};
    fixture->font.font_handle = lam::counted(font_resolve(fixture->fonts, &description));
    ASSERT_NE(fixture->font.font_handle, nullptr);
    fixture->font.font_size = 18; fixture->box.style = lam::up(&fixture->font); fixture->box.current_font_size = 18;
    fixture->glyphs[0] = font_get_glyph_index(fixture->font.font_handle, 'A'); fixture->glyphs[1] = fixture->glyphs[0];
    ASSERT_NE(fixture->glyphs[0], 0u); fixture->xs[1] = 40; fixture->ys[1] = 12;
    fixture->run.font = lam::up(&fixture->box); fixture->run.color = {.r = 25, .g = 80, .b = 180, .a = 255};
    fixture->run.font_size = 18; fixture->run.x = 20; fixture->run.baseline_y = 40;
    fixture->run.glyph_ids = lam::up(fixture->glyphs); fixture->run.xs = lam::up(fixture->xs);
    fixture->run.ys = lam::up(fixture->ys); fixture->run.count = 2;
    fixture->run.owned_text = lam::own((const char*)mem_strdup("AA", MEM_CAT_RENDER)); ASSERT_NE(fixture->run.owned_text, nullptr);
    fixture->run.text = fixture->run.owned_text.borrow(); fixture->run.text_len = 2;
    fixture->image = render_surface_create_budgeted(nullptr, 4, 2); ASSERT_NE(fixture->image, nullptr);
    for (size_t y = 0; y < 2; y++) for (size_t x = 0; x < 4; x++)
        ((uint32_t*)((uint8_t*)fixture->image->pixels + y * fixture->image->pitch))[x] = UINT32_C(0xff2588dc);
    fixture->image_box.image = lam::up(fixture->image); fixture->image_box.raster_scale = 1;
    fixture->image_box.opacity = 255; fixture->image_box.content_rect = fixture->image_box.image_rect = {120, 20, 40, 30};
}

static void native_fragment_export_preview(ViewTree* preview, const ImageSurface* expected,
        const char* png, const char* svg, const char* pdf) {
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    bool glyph_pixels = false;
    for (size_t y = 0; y < (size_t)expected->height; y++) for (size_t x = 0; x < (size_t)expected->width; x++) {
        const uint8_t* pixel = (const uint8_t*)expected->pixels + y * expected->pitch + x * 4;
        glyph_pixels |= pixel[3] && pixel[0] < 80 && pixel[1] > 20 && pixel[1] < 130 && pixel[2] > 100;
    }
    ASSERT_TRUE(glyph_pixels);
    ImageSurface* retained = render_secondary_page_snapshot(preview, 1); ASSERT_NE(retained, nullptr);
    expect_same_surface_pixels(expected, retained); save_surface_to_png(retained, png); image_surface_destroy(retained);
    ASSERT_TRUE(render_secondary_view_to_svg(preview, svg, 1, 1));
    ASSERT_TRUE(render_secondary_view_to_pdf(preview, pdf));
}

static TypesetStatus native_bound_next(void* context, const TypesetResume* cursor,
        TypesetContribution* contribution, TypesetResume* next) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    if (memcmp(cursor, &fixture->active, sizeof(*cursor))) return TYPESET_STALE;
    if (cursor->state[0] >= fixture->contribution_count) return TYPESET_DONE;
    *contribution = fixture->contributions[cursor->state[0]];
    *next = *cursor; next->serial++; next->state[0]++; next->state[1]++;
    fixture->active = *next;
    return TYPESET_OK;
}
static TypesetStatus native_bound_checkpoint(void*, const TypesetResume* cursor, TypesetResume* saved) {
    *saved = *cursor; saved->state[2] = 17;
    return TYPESET_OK;
}
static TypesetStatus native_bound_restore(void* context, const TypesetResume* saved, TypesetResume* restored) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    fixture->active = *saved; fixture->restore_count++; *restored = *saved;
    return TYPESET_OK;
}
static TypesetStatus native_bound_material(void* context, const TypesetContribution*, const TypesetItem*,
        const TypesetLineCandidate*, ViewNativeMaterial* material) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    *material = native_fragment_material(fixture);
    if (fixture->run.font) material->glyph_run = lam::up((const PaintGlyphRun*)&fixture->run);
    if (fixture->image) material->image_box = lam::up((const PaintImageBox*)&fixture->image_box);
    return TYPESET_OK;
}
static size_t native_bound_choose(void* context, const TypesetPageCandidate* candidates, size_t count) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    for (size_t i = 0; i < count; i++) if (candidates[i].boundary.legality == TYPESET_BREAK_FORCED) return i;
    if (fixture->first_candidate) {
        size_t selected = SIZE_MAX;
        for (size_t i = 0; i < count; i++) if (candidates[i].body_height > 0.0f &&
            (selected == SIZE_MAX || candidates[i].body_height <= candidates[selected].body_height)) selected = i;
        if (selected != SIZE_MAX) return selected;
    }
    return count - 1;
}
static TypesetAssemblyAction native_bound_assemble(void* context, const TypesetPageCandidate* candidate) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    if (fixture->assembly_mode == NATIVE_ASSEMBLY_NONE ||
        (fixture->assembly_page && fixture->assembly_page != candidate->page_number)) return TYPESET_ASSEMBLY_FINALIZE;
    if (fixture->assembly_mode == NATIVE_ASSEMBLY_PER_PAGE)
        return fixture->assembly_phase < 2 * candidate->page_number ? TYPESET_ASSEMBLY_HOLD : TYPESET_ASSEMBLY_FINALIZE;
    if (fixture->assembly_mode != NATIVE_ASSEMBLY_HOLD_REINSERT || !fixture->assembly_phase) return TYPESET_ASSEMBLY_HOLD;
    return fixture->assembly_phase == 1 ? TYPESET_ASSEMBLY_REINSERT : TYPESET_ASSEMBLY_FINALIZE;
}
static TypesetStatus native_bound_transition(void* context, TypesetAssemblyAction action, const TypesetPagePlan* plan) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    fixture->assembly_calls++;
    if (fixture->assembly_mode == NATIVE_ASSEMBLY_STUCK) return TYPESET_OK;
    if (fixture->assembly_mode == NATIVE_ASSEMBLY_CYCLE) { fixture->assembly_phase ^= 1; return TYPESET_OK; }
    fixture->assembly_phase++;
    if (fixture->assembly_mode == NATIVE_ASSEMBLY_PER_PAGE) return TYPESET_OK;
    if (fixture->assembly_mode == NATIVE_ASSEMBLY_FAIL) return TYPESET_INVALID;
    if (fixture->assembly_mode == NATIVE_ASSEMBLY_ENDLESS) return TYPESET_OK;
    if ((fixture->assembly_page ? fixture->committed_pages + 1 != fixture->assembly_page : fixture->committed_pages != 0) ||
        (action == TYPESET_ASSEMBLY_HOLD ? fixture->assembly_phase != 1 : fixture->assembly_phase != 2))
        return TYPESET_INVALID;
    for (size_t i = 0; i < plan->count; i++) if (plan->contributions[i].metrics.exact) {
        const NativeRegionFixture* exact = (const NativeRegionFixture*)plan->contributions[i].metrics.exact.get();
        if (exact->provider != 917 || exact->scaled_points < UINT64_C(9007199254740993)) return TYPESET_INVALID;
        fixture->assembly_seen_exact = true;
    }
    if (action == TYPESET_ASSEMBLY_REINSERT) fixture->first_candidate = true;
    return TYPESET_OK;
}
static TypesetStatus native_bound_committed(void* context, const TypesetPageCandidate* candidate) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    if (fixture->fail_commit && (!fixture->fail_page || fixture->fail_page == candidate->page_number)) return TYPESET_INVALID;
    if (fixture->committed_pages >= sizeof(fixture->page_kinds) / sizeof(fixture->page_kinds[0])) return TYPESET_BUDGET_EXHAUSTED;
    fixture->page_kinds[fixture->committed_pages] = candidate->kind;
    fixture->page_numbers[fixture->committed_pages] = candidate->page_number;
    fixture->committed_pages++;
    return TYPESET_OK;
}
static TypesetStatus native_bound_policy_checkpoint(void* context, TypesetPolicyCheckpoint* saved) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    saved->state[0] = fixture->committed_pages; saved->state[1] = fixture->assembly_phase;
    saved->state[2] = fixture->first_candidate;
    return TYPESET_OK;
}
static TypesetStatus native_bound_policy_restore(void* context, const TypesetPolicyCheckpoint* saved) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    fixture->committed_pages = saved->state[0]; fixture->assembly_phase = saved->state[1];
    fixture->first_candidate = saved->state[2] != 0;
    return TYPESET_OK;
}
static TypesetPagePolicy native_bound_policy(NativeFragmentFixture* fixture) {
    return {fixture, native_bound_choose, native_bound_assemble, native_bound_committed,
        native_bound_policy_checkpoint, native_bound_policy_restore, native_bound_transition};
}
static PagedNativeFlowBinding native_bound_binding(NativeFragmentFixture* fixture, DomElement* flow) {
    PagedNativeFlowBinding binding = {};
    binding.control = dom_node_ref(flow);
    binding.provider = {fixture->exact.provider, 5, fixture, native_bound_next, native_bound_checkpoint, native_bound_restore};
    binding.start = {fixture->exact.provider, 5, 0, {}}; binding.owner = {fixture, native_fragment_retain, native_fragment_release};
    binding.context = fixture; binding.material = native_bound_material;
    return binding;
}

static TypesetStatus native_bound_region_measure(void* context, const TypesetResume* start,
        const TypesetRegionConstraints* constraints, bool split, Pool* scratch, TypesetRegionSlice* slice) {
    TypesetStatus status = native_region_measure(context, start, constraints, split, scratch, slice);
    if (status != TYPESET_OK) return status;
    NativeRegionFixture* exact = (NativeRegionFixture*)pool_alloc(scratch, sizeof(NativeRegionFixture));
    if (!exact) return TYPESET_OUT_OF_MEMORY;
    *exact = *(const NativeRegionFixture*)context;
    slice->metrics.exact = slice->paint = lam::up((const TypesetRecord*)exact);
    return TYPESET_OK;
}
static TypesetStatus native_bound_region_item(void* context, const TypesetRegionPlacement* placement, size_t index,
        ViewNativeMaterial* material, RdtLogicalRect* rect) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)context;
    if (fixture->fail_region_paint) return TYPESET_INVALID;
    size_t count = placement->slice.end.state[0] - placement->start.state[0];
    if (index >= count) return TYPESET_DONE;
    const NativeRegionFixture* record = (const NativeRegionFixture*)placement->material->context;
    *material = native_fragment_material(fixture); material->owner = {};
    material->source = placement->material->source;
    material->source.node += 10000 + placement->start.state[0] + index;
    material->start = placement->start.state[0] + index; material->length = 1;
    material->metrics = {80, record->line_height - 7, 7, record->line_height - 7,
        {0, 0, 80, record->line_height}, placement->slice.metrics.exact};
    material->solution = placement->slice.paint;
    if (fixture->run.font) material->glyph_run = lam::up((const PaintGlyphRun*)&fixture->run);
    if (fixture->image) material->image_box = lam::up((const PaintImageBox*)&fixture->image_box);
    *rect = {0, index * record->line_height, 80, record->line_height};
    return TYPESET_OK;
}

static TypesetContribution native_bound_box(NativeFragmentFixture* fixture, uint64_t node) {
    ViewNativeMaterial material = native_fragment_material(fixture);
    TypesetContribution value = {};
    value.kind = TYPESET_CONTRIBUTION_BOX; value.source = material.source; value.source.node = node;
    value.metrics = material.metrics; value.boundary = {TYPESET_BREAK_ALLOWED, TYPESET_BREAK_PAGE, 0, 0};
    return value;
}
static size_t native_bound_alternatives(const TypesetParagraph* paragraph, size_t first, float width,
        TypesetLineCandidate* candidates, size_t capacity, void*) {
    size_t count = 0;
    for (size_t length = 1; length <= 2 && first + length <= paragraph->count; length++) {
        if (count >= capacity) return count + 1;
        TypesetLineCandidate line = {};
        line.first = line.paint_first = first; line.next = line.paint_end = first + length;
        for (size_t i = first; i < first + length; i++) line.width += paragraph->items[i].metrics.advance;
        line.height = 20.0f; line.depth = 5.0f; line.baseline = 15.0f;
        line.cost = 2 - length; line.overflow = line.width > width;
        line.solution = lam::up((const TypesetRecord*)&((NativeFragmentFixture*)paragraph->context)->exact);
        candidates[count++] = line;
    }
    return count;
}

static TypesetContribution native_bound_nested(NativeFragmentFixture* fixture, uint64_t node,
        const TypesetFlowProvider* child, TypesetBreakLegality boundary = TYPESET_BREAK_ALLOWED) {
    TypesetContribution value = native_bound_box(fixture, node);
    value.kind = TYPESET_CONTRIBUTION_NESTED; value.metrics = {}; value.nested = child;
    value.boundary.legality = boundary; return value;
}

TEST_F(SecondaryViewTest, NativeNestedStreamsResumeParentsAfterChildBreaksAndRetainEveryOwner) {
    init_vector_engine(); ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixtures[3] = {};
    PagedNativeFlowBinding bindings[3] = {};
    for (size_t i = 0; i < 3; i++) {
        fixtures[i] = native_fragment_fixture(); ASSERT_NE(fixtures[i], nullptr);
        fixtures[i]->exact.provider += i; native_fragment_paint_fixture(fixtures[i]);
        bindings[i] = native_bound_binding(fixtures[i], i ? nullptr : flow);
    }
    fixtures[0]->contribution_count = fixtures[1]->contribution_count = 3; fixtures[2]->contribution_count = 2;
    for (size_t i = 0; i < 3; i++) for (size_t j = 0; j < fixtures[i]->contribution_count; j++)
        fixtures[i]->contributions[j] = native_bound_box(fixtures[i], 5000 + i * 10 + j);
    fixtures[0]->contributions[1] = native_bound_nested(fixtures[0], 5001, &bindings[1].provider);
    fixtures[1]->contributions[1] = native_bound_nested(fixtures[1], 5011, &bindings[2].provider);
    fixtures[1]->contributions[2].boundary.legality = TYPESET_BREAK_FORCED;
    fixtures[2]->contributions[0].boundary.legality = TYPESET_BREAK_FORCED;
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = bindings; options.native_flow_count = 3;
    PagedLayoutDiagnostic diagnostic = {}; ViewTree* tree = secondary();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 4u);
    const uint32_t pages[3][3] = {{1,0,4},{1,0,3},{2,3,0}};
    for (size_t i = 0; i < 3; i++) for (size_t j = 0; j < fixtures[i]->contribution_count; j++) {
        if (!pages[i][j]) continue;
        const ViewNodeState* state = view_tree_native_state(tree, &fixtures[i]->contributions[j].source); ASSERT_NE(state, nullptr);
        ASSERT_EQ(state->occurrence_count, 1u);
        EXPECT_EQ(occurrence_page(state->first_occurrence), pages[i][j]);
        EXPECT_EQ(state->first_occurrence->native_material->metrics.exact.get(), &fixtures[i]->exact);
        EXPECT_EQ(state->first_occurrence->native_material->owner.context, fixtures[i]);
    }
    ImageSurface* expected[4] = {};
    for (uint32_t page = 1; page <= 4; page++) {
        expected[page - 1] = render_secondary_page_snapshot(tree, page); ASSERT_NE(expected[page - 1], nullptr);
    }
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u);
    for (auto* fixture : fixtures) EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    for (auto* fixture : fixtures) native_fragment_release(fixture);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    EXPECT_TRUE(render_secondary_view_to_pdf(preview, "temp/paged-media-impl/native-nested.pdf"));
    EXPECT_TRUE(render_secondary_view_to_svg(preview, "temp/paged-media-impl/native-nested.svg"));
    for (uint32_t page = 1; page <= 4; page++) {
        ImageSurface* retained = render_secondary_page_snapshot(preview, page); ASSERT_NE(retained, nullptr);
        expect_same_surface_pixels(expected[page - 1], retained);
        image_surface_destroy(expected[page - 1]); image_surface_destroy(retained);
    }
    ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
}

TEST_F(SecondaryViewTest, NativeNestedParagraphsKeepPerInvocationCursorsAndRemeasureSelectedWidths) {
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr); ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* parent = native_fragment_fixture(), *child = native_fragment_fixture();
    ASSERT_NE(parent, nullptr); ASSERT_NE(child, nullptr); child->exact.provider++;
    PagedNativeFlowBinding bindings[] = {native_bound_binding(parent, flow), native_bound_binding(child, nullptr)};
    parent->contribution_count = 2; child->contribution_count = 1;
    for (size_t i = 0; i < 2; i++) parent->contributions[i] = native_bound_nested(parent, 5100 + i, &bindings[1].provider);
    child->contributions[0] = native_bound_box(child, 5200);
    for (size_t i = 0; i < 4; i++) {
        child->items[i].kind = TYPESET_BOX; child->items[i].source = child->contributions[0].source;
        child->items[i].start = i; child->items[i].length = 1;
        child->items[i].metrics = child->contributions[0].metrics;
    }
    child->paragraph = {child->items, 4, 0, native_bound_alternatives, typeset_choose_lowest_cost, child};
    child->contributions[0].kind = TYPESET_CONTRIBUTION_PARAGRAPH; child->contributions[0].paragraph = &child->paragraph;
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = bindings; options.native_flow_count = 2;
    PagedLayoutDiagnostic diagnostic = {}; ViewTree* tree = secondary();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 1u); EXPECT_EQ(parent->active.state[0], 2u); EXPECT_EQ(child->active.state[0], 1u);
    const ViewNodeState* state = view_tree_native_state(tree, &child->contributions[0].source); ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->occurrence_count, 8u);
    size_t uses[4] = {};
    for (auto* node = state->first_occurrence.get(); node; node = node->next_occurrence) {
        ASSERT_LT(node->text_start, 4u); uses[node->text_start]++;
        EXPECT_EQ(node->native_material->solution.get(), &child->exact);
    }
    for (size_t count : uses) EXPECT_EQ(count, 2u);
    ASSERT_TRUE(view_tree_model_reset(tree));
    DomElement* master = source->first_child_element(); ASSERT_NE(master, nullptr);
    ASSERT_TRUE(master->set_attribute("style", "size:100px 100px; margin:10px"));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u);
    state = view_tree_native_state(tree, &child->contributions[0].source); ASSERT_NE(state, nullptr); EXPECT_EQ(state->occurrence_count, 8u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&parent->references), 1); EXPECT_EQ(ref_count_get(&child->references), 1);
    native_fragment_release(parent); native_fragment_release(child);
}

TEST_F(SecondaryViewTest, NativeNestedRegistrationsRejectCyclesDescriptorsCursorBudgetsAndLateCommitAtomically) {
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr); ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* parent = native_fragment_fixture(), *child = native_fragment_fixture();
    ASSERT_NE(parent, nullptr); ASSERT_NE(child, nullptr); child->exact.provider++;
    PagedNativeFlowBinding bindings[] = {native_bound_binding(parent, flow), native_bound_binding(child, nullptr)};
    parent->contribution_count = 1; child->contribution_count = 1;
    parent->contributions[0] = native_bound_nested(parent, 5300, &bindings[1].provider);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = bindings; options.native_flow_count = 2;
    TypesetPagePolicy policy = native_bound_policy(parent); options.page_policy = &policy;
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
    for (size_t mode = 0; mode < 5; mode++) {
        SCOPED_TRACE(mode); ASSERT_TRUE(view_tree_model_reset(tree));
        child->contributions[0] = mode ? native_bound_box(child, 5400) : native_bound_nested(child, 5400, &bindings[1].provider);
        TypesetFlowProvider invalid = bindings[1].provider; invalid.context = parent;
        parent->contributions[0].nested = mode == 1 ? &invalid : &bindings[1].provider;
        options.max_items = mode == 2 ? 1 : paged_layout_options_default().max_items;
        options.native_flow_count = mode == 3 ? 1 : 2;
        parent->fail_commit = mode == 4;
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), mode == 2 ? TYPESET_BUDGET_EXHAUSTED : TYPESET_INVALID);
        EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(parent->active.state[0], 0u);
        EXPECT_EQ(child->active.state[0], 0u); EXPECT_EQ(parent->committed_pages, 0u);
    }
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&parent->references), 1); EXPECT_EQ(ref_count_get(&child->references), 1);
    native_fragment_release(parent); native_fragment_release(child);
}

TEST_F(SecondaryViewTest, NativeNestedDepthAndPersistentCursorBudgetsDiagnoseTheirOwnLimits) {
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr); ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixtures[4] = {}; PagedNativeFlowBinding bindings[4] = {};
    for (size_t i = 0; i < 4; i++) {
        fixtures[i] = native_fragment_fixture(); ASSERT_NE(fixtures[i], nullptr); fixtures[i]->exact.provider += i;
        bindings[i] = native_bound_binding(fixtures[i], i ? nullptr : flow);
        fixtures[i]->contribution_count = i ? 1 : 16;
    }
    for (size_t i = 0; i < 4; i++) for (size_t j = 0; j < fixtures[i]->contribution_count; j++)
        fixtures[i]->contributions[j] = i < 3 ? native_bound_nested(fixtures[i], 5600 + i * 20 + j, &bindings[i + 1].provider) :
            native_bound_box(fixtures[i], 5680);
    fixtures[3]->contributions[0].boundary.legality = TYPESET_BREAK_FORCED;
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = bindings; options.native_flow_count = 4;
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
    options.max_depth = 3;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "native nested provider depth exceeds the common context budget");
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_depth = 4; options.max_items = 32;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_STREQ(diagnostic.reason, "native continuation cursor count exceeds the common item budget");
    EXPECT_EQ(tree->model->page_count, 0u);
    for (auto* fixture : fixtures) EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 256;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 16u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    for (auto* fixture : fixtures) {
        EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
        native_fragment_release(fixture);
    }
}

TEST_F(SecondaryViewTest, NativeBodyBindingsUseCommonPagePlanningExactMetricsAndSelectablePolicies) {
    stylesheet("p { margin: 0 } #native-reference::before { content: target-counter(url('#native-body'), page) }");
    DomElement* master = page_master("native", "size: 200px 100px; margin: 10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "before", "header", "5px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* header = static_content(sequence, "header"); ASSERT_NE(header, nullptr);
    DomElement* reference = block(nullptr, nullptr, "span", header); ASSERT_NE(reference, nullptr);
    ASSERT_TRUE(reference->set_attribute("id", "native-reference"));
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body")); ASSERT_TRUE(flow->set_attribute("id", "native-body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->contribution_count = 3;
    for (size_t i = 0; i < fixture->contribution_count; i++) fixture->contributions[i] = native_bound_box(fixture, 100 + i);
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    TypesetPagePolicy policy = native_bound_policy(fixture);
    options.page_policy = &policy;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 2u); EXPECT_EQ(fixture->committed_pages, 2u);
    EXPECT_GT(diagnostic.reference_passes, 1u);
    const TypesetTarget* target = layout_secondary_target(tree, "native-body"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->page_number, 1u); EXPECT_EQ(target->last_page_number, 2u);
    EXPECT_EQ(fixture->active.state[0], 3u); EXPECT_EQ(fixture->active.state[1], 3u); EXPECT_EQ(fixture->active.state[2], 17u); EXPECT_GE(fixture->restore_count, 3u);
    for (size_t i = 0; i < fixture->contribution_count; i++) {
        const ViewNodeState* state = view_tree_native_state(tree, &fixture->contributions[i].source); ASSERT_NE(state, nullptr);
        ASSERT_EQ(state->occurrence_count, 1u); const LayoutViewNode* node = state->first_occurrence;
        EXPECT_EQ(node->source.address, nullptr); EXPECT_EQ(node->computed_style, nullptr);
        EXPECT_EQ(node->native_material->metrics.exact.get(), &fixture->exact);
        EXPECT_FLOAT_EQ(node->native_material->metrics.depth, 7.0f); EXPECT_FLOAT_EQ(node->rect.height, 31.0f);
        EXPECT_FLOAT_EQ(node->rect.x, 10.0f); EXPECT_FLOAT_EQ(node->rect.y, i == 1 ? 41.0f : 10.0f);
    }
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    fixture->first_candidate = true; fixture->committed_pages = 0; ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 3u); EXPECT_EQ(fixture->committed_pages, 3u);
    EXPECT_EQ(preview->model->page_count, 2u);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->committed_pages = 0; options.max_pages = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->committed_pages, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 10; fixture->fail_commit = true;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->committed_pages, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ASSERT_NE(view_tree_native_state(preview, &fixture->contributions[0].source), nullptr);
    ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
    EXPECT_EQ(fixture->exact.scaled_points, UINT64_C(9007199254740993));
    EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeHeldPagesReinsertImmutableInputAndConvergeBeforePublication) {
    init_vector_engine();
    stylesheet("#native-reference::before { content: target-counter(url('#native-body'), page) }");
    DomElement* master = page_master("native", "size:200px 100px; margin:10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "before", "header", "5px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* header = static_content(sequence, "header"); ASSERT_NE(header, nullptr);
    DomElement* reference = block(nullptr, nullptr, "span", header); ASSERT_NE(reference, nullptr);
    ASSERT_TRUE(reference->set_attribute("id", "native-reference"));
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body")); ASSERT_TRUE(flow->set_attribute("id", "native-body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    native_fragment_paint_fixture(fixture);
    fixture->contribution_count = 3; fixture->assembly_mode = NATIVE_ASSEMBLY_HOLD_REINSERT;
    for (size_t i = 0; i < fixture->contribution_count; i++) fixture->contributions[i] = native_bound_box(fixture, 2100 + i);
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    TypesetPagePolicy policy = native_bound_policy(fixture);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding;
    options.native_flow_count = 1; options.page_policy = &policy;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_EQ(fixture->committed_pages, 3u);
    EXPECT_EQ(fixture->assembly_phase, 2u); EXPECT_TRUE(fixture->first_candidate); EXPECT_TRUE(fixture->assembly_seen_exact);
    EXPECT_GT(diagnostic.reference_passes, 1u); EXPECT_GE(fixture->assembly_calls, 4u);
    EXPECT_EQ(layout_secondary_target(tree, "native-body")->last_page_number, 3u);
    for (size_t i = 0; i < fixture->contribution_count; i++) {
        const ViewNodeState* state = view_tree_native_state(tree, &fixture->contributions[i].source); ASSERT_NE(state, nullptr);
        ASSERT_EQ(state->occurrence_count, 1u); EXPECT_EQ(occurrence_page(state->first_occurrence), i + 1);
        EXPECT_FLOAT_EQ(state->first_occurrence->rect.y, 10);
        EXPECT_EQ(state->first_occurrence->native_material->metrics.exact.get(), &fixture->exact);
    }
    ImageSurface* first = render_secondary_page_snapshot(tree, 1); ASSERT_NE(first, nullptr);
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->assembly_phase = fixture->committed_pages = 0;
    fixture->first_candidate = false; fixture->fail_commit = true;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->committed_pages, 0u);
    EXPECT_EQ(fixture->assembly_phase, 0u); EXPECT_FALSE(fixture->first_candidate); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
    ASSERT_EQ(preview->model->page_count, 3u);
    native_fragment_export_preview(preview, first, "temp/paged-media-impl/native-assembly.png",
        "temp/paged-media-impl/native-assembly.svg", "temp/paged-media-impl/native-assembly.pdf");
    image_surface_destroy(first); ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
}

TEST_F(SecondaryViewTest, NativeAssemblyCyclesBudgetsAndTransitionFailuresRestoreTheWholeEdition) {
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->contribution_count = 3;
    for (size_t i = 0; i < fixture->contribution_count; i++) fixture->contributions[i] = native_bound_box(fixture, 2200 + i);
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    TypesetPagePolicy policy = native_bound_policy(fixture);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding;
    options.native_flow_count = 1; options.page_policy = &policy;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    const NativeAssemblyMode modes[] = {NATIVE_ASSEMBLY_STUCK, NATIVE_ASSEMBLY_CYCLE, NATIVE_ASSEMBLY_ENDLESS,
        NATIVE_ASSEMBLY_FAIL, NATIVE_ASSEMBLY_HOLD_REINSERT, NATIVE_ASSEMBLY_HOLD_REINSERT,
        NATIVE_ASSEMBLY_HOLD_REINSERT, NATIVE_ASSEMBLY_HOLD_REINSERT};
    for (size_t invalid = 0; invalid < sizeof(modes) / sizeof(modes[0]); invalid++) {
        SCOPED_TRACE(invalid); fixture->assembly_mode = modes[invalid]; fixture->assembly_calls = 0;
        policy.transition = invalid == 4 ? nullptr : native_bound_transition;
        policy.checkpoint = invalid == 6 ? nullptr : native_bound_policy_checkpoint;
        policy.restore = invalid == 6 ? nullptr : native_bound_policy_restore;
        options.max_page_transitions = invalid == 5 ? 0 : 2; fixture->fail_commit = invalid == 7;
        TypesetStatus expected = invalid < 2 ? TYPESET_NO_PROGRESS : invalid == 2 || invalid == 5 ?
            TYPESET_BUDGET_EXHAUSTED : TYPESET_INVALID;
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), expected) << diagnostic.reason;
        EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
        EXPECT_EQ(fixture->committed_pages, 0u); EXPECT_EQ(fixture->assembly_phase, 0u); EXPECT_FALSE(fixture->first_candidate);
        EXPECT_LE(fixture->assembly_calls, 2u); EXPECT_EQ(view_tree_native_state(tree, &fixture->contributions[0].source), nullptr);
        ASSERT_TRUE(view_tree_model_reset(tree));
    }
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

void SecondaryViewTest::native_publishing_session(bool nested) {
    init_vector_engine();
    stylesheet("#native-reference::before { content: target-counter(url('#native-anchor'), page) }");
    DomElement* master = page_master("native", "size:200px 140px; margin:10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "before", "header", "5px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* header = static_content(sequence, "header"); ASSERT_NE(header, nullptr);
    DomElement* reference = block(nullptr, nullptr, "span", header); ASSERT_NE(reference, nullptr);
    ASSERT_TRUE(reference->set_attribute("id", "native-reference"));
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    native_fragment_paint_fixture(fixture); fixture->image_box.opacity = 0;
    fixture->assembly_mode = NATIVE_ASSEMBLY_HOLD_REINSERT; fixture->contribution_count = 12;
    for (size_t i = 0; i < fixture->contribution_count; i++) fixture->contributions[i] = native_bound_box(fixture, 2300 + i);
    const size_t mark_indices[] = {0, 7, 10}; const char* texts[] = {"Alpha", "Beta", "Gamma"};
    for (size_t i = 0; i < 3; i++) {
        TypesetContribution& value = fixture->contributions[mark_indices[i]]; value.kind = TYPESET_CONTRIBUTION_MARK;
        fixture->marks[i] = {TYPESET_MARK_NATIVE, "chapter", value.source, texts[i],
            lam::up((const TypesetRecord*)&fixture->exact), 0, false}; value.mark = &fixture->marks[i];
    }
    fixture->contributions[1].kind = TYPESET_CONTRIBUTION_PARAGRAPH;
    fixture->paragraph = {fixture->items, 4, 20, native_bound_alternatives, typeset_choose_lowest_cost, fixture};
    fixture->paragraph.baseline_aware = true; fixture->contributions[1].paragraph = &fixture->paragraph;
    for (size_t i = 0; i < 4; i++) {
        fixture->items[i].kind = TYPESET_BOX; fixture->items[i].source = fixture->contributions[1].source;
        fixture->items[i].start = i * 9; fixture->items[i].length = 9;
        fixture->items[i].metrics = {65, 15, 5, 15, {0, 0, 65, 20}, lam::up((const TypesetRecord*)&fixture->exact)};
    }
    fixture->contributions[2].kind = TYPESET_CONTRIBUTION_GLUE;
    fixture->contributions[2].glue = {5, 1, 0, 2, 0, false, true};
    fixture->contributions[2].boundary = {TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_PAGE, -30, 0x40};
    for (size_t i = 0; i < 2; i++) {
        fixture->region_records[i] = {{917}, UINT64_C(9007199254740993) + i, i ? 1u : 2u, 20};
        fixture->regions[i] = native_region_material(&fixture->region_records[i], 5, 2400 + i);
        fixture->regions[i].measure = native_bound_region_measure; fixture->regions[i].split = !i;
        fixture->regions[i].delay_pages = i ? 2 : 0;
        TypesetContribution& value = fixture->contributions[3 + i]; value.source = fixture->regions[i].source;
        value.kind = i ? TYPESET_CONTRIBUTION_FLOAT : TYPESET_CONTRIBUTION_INSERTION;
        value.region = i ? TYPESET_REGION_FLOAT : TYPESET_REGION_NOTE; value.region_material = &fixture->regions[i];
    }
    fixture->targets[0] = {"native-anchor", fixture->contributions[6].source, 0,
        lam::up((const TypesetRecord*)&fixture->exact)};
    fixture->targets[1] = {"native-note", fixture->regions[0].source, 0, fixture->regions[0].source.native};
    fixture->contributions[5].kind = TYPESET_CONTRIBUTION_TARGET; fixture->contributions[5].source = fixture->targets[0].source;
    fixture->contributions[5].target = &fixture->targets[0];
    fixture->contributions[8].kind = TYPESET_CONTRIBUTION_FLUSH_DEFERRED;
    fixture->contributions[11].boundary = {TYPESET_BREAK_FORCED, TYPESET_BREAK_PAGE, -10000, 0x80};
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow); binding.region_item = native_bound_region_item;
    binding.targets = fixture->targets; binding.target_count = 2;
    TypesetPagePolicy policy = native_bound_policy(fixture);
    NativeFragmentFixture* parent = nullptr;
    PagedNativeFlowBinding bindings[2] = {binding, {}};
    if (nested) {
        parent = native_fragment_fixture(); ASSERT_NE(parent, nullptr); parent->exact.provider++;
        bindings[1] = binding; bindings[1].control = {};
        bindings[0] = native_bound_binding(parent, flow); parent->contribution_count = 1;
        parent->contributions[0] = native_bound_nested(parent, 5500, &bindings[1].provider);
    }
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = bindings;
    options.native_flow_count = nested ? 2 : 1; options.page_policy = &policy;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    size_t old_pages = tree->model->page_count; EXPECT_GT(old_pages, 2u); EXPECT_GT(diagnostic.reference_passes, 1u);
    EXPECT_TRUE(fixture->assembly_seen_exact); EXPECT_EQ(fixture->assembly_phase, 2u);
    const ViewNodeState* paragraph = view_tree_native_state(tree, &fixture->contributions[1].source); ASSERT_NE(paragraph, nullptr);
    ASSERT_EQ(paragraph->occurrence_count, 4u);
    size_t index = 0;
    for (const LayoutViewNode* node = paragraph->first_occurrence; node; node = node->next_occurrence, index++) {
        EXPECT_EQ(node->text_start, index * 9); EXPECT_EQ(node->text_length, 9u);
        EXPECT_FLOAT_EQ(node->rect.x, index % 2 ? 75 : 10);
        EXPECT_EQ(node->native_material->metrics.exact.get(), &fixture->exact);
        EXPECT_EQ(node->native_material->solution.get(), &fixture->exact);
    }
    const TypesetTarget* anchor = layout_secondary_target(tree, "native-anchor"); ASSERT_NE(anchor, nullptr);
    const ViewNodeState* body = view_tree_native_state(tree, &fixture->contributions[6].source); ASSERT_NE(body, nullptr);
    EXPECT_EQ(anchor->page_number, occurrence_page(body->first_occurrence));
    const TypesetTarget* note = layout_secondary_target(tree, "native-note"); ASSERT_NE(note, nullptr);
    const ViewNodeState* notes = view_tree_native_state(tree, &fixture->regions[0].source); ASSERT_NE(notes, nullptr);
    EXPECT_EQ(note->page_number, occurrence_page(notes->first_occurrence));
    const ViewNodeState* floating = view_tree_native_state(tree, &fixture->regions[1].source); ASSERT_NE(floating, nullptr);
    const ViewNodeState* following = view_tree_native_state(tree, &fixture->contributions[9].source); ASSERT_NE(following, nullptr);
    EXPECT_GT(occurrence_page(following->first_occurrence), occurrence_page(floating->first_occurrence));
    EXPECT_STREQ(layout_secondary_mark(tree, TYPESET_MARK_NATIVE, "chapter", 1, TYPESET_MARK_FIRST)->text, "Alpha");
    EXPECT_STREQ(layout_secondary_mark(tree, TYPESET_MARK_NATIVE, "chapter", static_cast<uint32_t>(old_pages), TYPESET_MARK_LAST)->text, "Gamma");
    ImageSurface* first = render_secondary_page_snapshot(tree, 1); ASSERT_NE(first, nullptr);
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(master->set_attribute("style", "size:120px 100px; margin:10px")); ASSERT_TRUE(view_tree_model_reset(tree));
    fixture->assembly_phase = fixture->committed_pages = 0; fixture->first_candidate = false;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_GE(tree->model->page_count, old_pages + 2); EXPECT_GT(diagnostic.reference_passes, 1u);
    paragraph = view_tree_native_state(tree, &fixture->contributions[1].source); ASSERT_NE(paragraph, nullptr);
    ASSERT_EQ(paragraph->occurrence_count, 4u);
    for (const LayoutViewNode* node = paragraph->first_occurrence; node; node = node->next_occurrence) EXPECT_FLOAT_EQ(node->rect.x, 10);
    EXPECT_EQ(preview->model->page_count, old_pages); ASSERT_NE(layout_secondary_target(preview, "native-note"), nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->assembly_phase = fixture->committed_pages = 0;
    fixture->first_candidate = false; options.max_pages = 1;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->assembly_phase, 0u); EXPECT_EQ(fixture->committed_pages, 0u);
    EXPECT_EQ(fixture->active.state[0], 0u); EXPECT_FALSE(fixture->first_candidate);
    if (parent) EXPECT_EQ(parent->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
    if (parent) native_fragment_release(parent);
    native_fragment_export_preview(preview, first,
        nested ? "temp/paged-media-impl/native-nested-combined.png" : "temp/paged-media-impl/native-combined.png",
        nested ? "temp/paged-media-impl/native-nested-combined.svg" : "temp/paged-media-impl/native-combined.svg",
        nested ? "temp/paged-media-impl/native-nested-combined.pdf" : "temp/paged-media-impl/native-combined.pdf");
    image_surface_destroy(first); ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
}

TEST_F(SecondaryViewTest, NativePublishingSessionCombinesAssemblyParagraphsRegionsReferencesAndRetainedEditions) {
    native_publishing_session(false);
}
TEST_F(SecondaryViewTest, NativeNestedPublishingSessionCombinesAssemblyParagraphsRegionsReferencesAndRetainedEditions) {
    native_publishing_session(true);
}

TEST_F(SecondaryViewTest, NativeRegionOnlyPagesUseJournaledAssemblyAndRetainPlacedTargets) {
    init_vector_engine();
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    ASSERT_NO_FATAL_FAILURE(native_fragment_paint_fixture(fixture));
    fixture->assembly_mode = NATIVE_ASSEMBLY_HOLD_REINSERT; fixture->assembly_page = 2;
    fixture->region_records[0] = {{917}, UINT64_C(9007199254740993), 1, 31};
    fixture->regions[0] = native_region_material(&fixture->region_records[0], 5, 2500);
    fixture->regions[0].measure = native_bound_region_measure; fixture->regions[0].delay_pages = 1;
    fixture->contributions[0] = native_bound_box(fixture, 2501); fixture->contributions[1] = native_bound_box(fixture, 2500);
    fixture->contributions[1].source = fixture->regions[0].source;
    fixture->contributions[1].kind = TYPESET_CONTRIBUTION_FLOAT; fixture->contributions[1].region = TYPESET_REGION_FLOAT;
    fixture->contributions[1].region_material = &fixture->regions[0]; fixture->contribution_count = 2;
    fixture->targets[0] = {"figure", fixture->regions[0].source, 0, fixture->regions[0].source.native};
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow); binding.region_item = native_bound_region_item;
    binding.targets = fixture->targets; binding.target_count = 1; TypesetPagePolicy policy = native_bound_policy(fixture);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding;
    options.native_flow_count = 1; options.page_policy = &policy;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_EQ(fixture->committed_pages, 2u);
    EXPECT_EQ(fixture->page_kinds[0], TYPESET_PAGE_FLOW); EXPECT_EQ(fixture->page_kinds[1], TYPESET_PAGE_REGION);
    EXPECT_EQ(fixture->page_numbers[0], 1u); EXPECT_EQ(fixture->page_numbers[1], 2u);
    EXPECT_EQ(fixture->assembly_phase, 2u); EXPECT_TRUE(fixture->assembly_seen_exact);
    EXPECT_EQ(layout_secondary_target(tree, "figure")->page_number, 2u);
    const ViewNodeState* floating = view_tree_native_state(tree, &fixture->regions[0].source); ASSERT_NE(floating, nullptr);
    EXPECT_FLOAT_EQ(floating->first_occurrence->rect.y, 10); EXPECT_EQ(occurrence_page(floating->first_occurrence), 2u);
    ImageSurface* first = render_secondary_page_snapshot(tree, 1); ASSERT_NE(first, nullptr);
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    const NativeAssemblyMode modes[] = {NATIVE_ASSEMBLY_STUCK, NATIVE_ASSEMBLY_CYCLE, NATIVE_ASSEMBLY_ENDLESS,
        NATIVE_ASSEMBLY_FAIL, NATIVE_ASSEMBLY_HOLD_REINSERT};
    for (size_t invalid = 0; invalid < sizeof(modes) / sizeof(modes[0]); invalid++) {
        SCOPED_TRACE(invalid); ASSERT_TRUE(view_tree_model_reset(tree));
        fixture->assembly_phase = fixture->committed_pages = 0; fixture->first_candidate = false;
        fixture->assembly_mode = modes[invalid]; fixture->fail_commit = invalid == 4; fixture->fail_page = 2;
        options.max_page_transitions = 2;
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), invalid < 2 ? TYPESET_NO_PROGRESS :
            invalid == 2 ? TYPESET_BUDGET_EXHAUSTED : TYPESET_INVALID) << diagnostic.reason;
        EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->committed_pages, 0u); EXPECT_EQ(fixture->assembly_phase, 0u);
        EXPECT_FALSE(fixture->first_candidate); EXPECT_EQ(fixture->active.state[0], 0u);
    }
    for (size_t budget : {3u, 4u}) {
        SCOPED_TRACE(budget); ASSERT_TRUE(view_tree_model_reset(tree));
        fixture->assembly_phase = fixture->committed_pages = 0; fixture->first_candidate = false;
        fixture->assembly_mode = NATIVE_ASSEMBLY_PER_PAGE; fixture->assembly_page = 0; fixture->fail_commit = false;
        options.max_page_transitions = budget;
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), budget == 3 ? TYPESET_BUDGET_EXHAUSTED : TYPESET_OK);
        EXPECT_EQ(tree->model->page_count, budget == 3 ? 0u : 2u);
        EXPECT_EQ(fixture->committed_pages, budget == 3 ? 0u : 2u);
        EXPECT_EQ(fixture->assembly_phase, budget == 3 ? 0u : 4u);
    }
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
    EXPECT_EQ(layout_secondary_target(preview, "figure")->page_number, 2u);
    native_fragment_export_preview(preview, first, "temp/paged-media-impl/native-physical.png",
        "temp/paged-media-impl/native-physical.svg", "temp/paged-media-impl/native-physical.pdf");
    image_surface_destroy(first); ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
}

TEST_F(SecondaryViewTest, BlankSidednessPagesParticipateInAssemblyOrderAndFailureRollback) {
    stylesheet("p,div{margin:0}"); ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    ASSERT_NE(block("A", "height:31px", "div", flow), nullptr);
    DomElement* tail = block("B", "height:31px;break-before:right", "div", flow); ASSERT_NE(tail, nullptr);
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->assembly_mode = NATIVE_ASSEMBLY_HOLD_REINSERT; fixture->assembly_page = 2;
    TypesetPagePolicy policy = native_bound_policy(fixture); PagedLayoutOptions options = paged_layout_options_default();
    options.page_policy = &policy; ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    // reinsertion selects the earliest body break, before the remainder of the declared block height.
    ASSERT_EQ(tree->model->page_count, 4u); EXPECT_TRUE(tree->model->pages.get()[1]->blank);
    ASSERT_EQ(fixture->committed_pages, 4u); EXPECT_EQ(fixture->assembly_phase, 2u);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(fixture->page_numbers[i], i + 1);
        EXPECT_EQ(fixture->page_kinds[i], i == 1 ? TYPESET_PAGE_BLANK : TYPESET_PAGE_FLOW);
    }
    const ViewNodeState* tail_state = view_tree_node_state(tree, tail, false); ASSERT_NE(tail_state, nullptr);
    size_t boxes = 0; float height = 0.0f;
    // paragraph occurrences share this source; only block occurrences own its declared height.
    for (const LayoutViewNode* node = tail_state->first_occurrence; node; node = node->next_occurrence) if (node->paint_box) {
        EXPECT_EQ(occurrence_page(node), 3 + boxes); boxes++; height += node->rect.height;
    }
    EXPECT_EQ(boxes, 2u); EXPECT_FLOAT_EQ(height, 31);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->assembly_phase = fixture->committed_pages = 0;
    fixture->first_candidate = false; options.max_page_transitions = 0;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->committed_pages, 0u); EXPECT_EQ(fixture->assembly_phase, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, SequenceEndPaddingUsesTheSameAssemblyPolicyAsFlowPages) {
    stylesheet("p,div{margin:0}"); ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native", "auto", "even"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body")); ASSERT_NE(block("A", "height:31px", "div", flow), nullptr);
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->assembly_mode = NATIVE_ASSEMBLY_HOLD_REINSERT; fixture->assembly_page = 2;
    TypesetPagePolicy policy = native_bound_policy(fixture); PagedLayoutOptions options = paged_layout_options_default();
    options.page_policy = &policy; ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_TRUE(tree->model->pages.get()[1]->blank);
    EXPECT_EQ(fixture->committed_pages, 2u); EXPECT_EQ(fixture->page_kinds[1], TYPESET_PAGE_BLANK);
    EXPECT_EQ(fixture->page_numbers[1], 2u); EXPECT_EQ(fixture->assembly_phase, 2u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, LeadingBlankPageAssemblyReplaysBeforeTheFirstBodyCommit) {
    stylesheet("p,div{margin:0}"); ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    ASSERT_NE(block("A", "height:31px;break-before:left", "div", flow), nullptr);
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->assembly_mode = NATIVE_ASSEMBLY_HOLD_REINSERT; fixture->assembly_page = 1;
    TypesetPagePolicy policy = native_bound_policy(fixture); PagedLayoutOptions options = paged_layout_options_default();
    options.page_policy = &policy; ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_TRUE(tree->model->pages.get()[0]->blank);
    EXPECT_EQ(fixture->committed_pages, 2u); EXPECT_EQ(fixture->assembly_phase, 2u);
    EXPECT_EQ(fixture->page_kinds[0], TYPESET_PAGE_BLANK); EXPECT_EQ(fixture->page_numbers[0], 1u);
    EXPECT_EQ(fixture->page_kinds[1], TYPESET_PAGE_FLOW); EXPECT_EQ(fixture->page_numbers[1], 2u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, FixedPagesHoldReinsertAndRollbackWithoutChangingTheirGeometry) {
    stylesheet("p,div{margin:0;font:10px/12px Arial}");
    DomElement* container = page_control("r:fixed-pages"); ASSERT_NE(container, nullptr);
    for (size_t i = 0; i < 2; i++) {
        DomElement* page = fixed_page(container, i ? "120px" : "200px", "100px"); ASSERT_NE(page, nullptr);
        ASSERT_NE(block("Fixed", "height:31px", "div", page), nullptr);
    }
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->assembly_mode = NATIVE_ASSEMBLY_HOLD_REINSERT; fixture->assembly_page = 2;
    TypesetPagePolicy policy = native_bound_policy(fixture); PagedLayoutOptions options = paged_layout_options_default();
    options.page_policy = &policy; ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); ASSERT_EQ(fixture->committed_pages, 2u); EXPECT_EQ(fixture->assembly_phase, 2u);
    for (size_t i = 0; i < 2; i++) {
        EXPECT_EQ(fixture->page_numbers[i], i + 1); EXPECT_EQ(fixture->page_kinds[i], TYPESET_PAGE_FIXED);
        EXPECT_FLOAT_EQ(tree->model->pages.get()[i]->node.rect.width, i ? 120 : 200);
        EXPECT_FLOAT_EQ(tree->model->pages.get()[i]->node.rect.height, 100);
    }
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->assembly_phase = fixture->committed_pages = 0; fixture->first_candidate = false;
    fixture->fail_commit = true; fixture->fail_page = 2;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->committed_pages, 0u); EXPECT_EQ(fixture->assembly_phase, 0u);
    EXPECT_FALSE(fixture->first_candidate); ASSERT_EQ(preview->model->page_count, 2u);
    EXPECT_FLOAT_EQ(preview->model->pages.get()[1]->node.rect.width, 120);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeMarksAndDeclaredTargetsShareConvergenceRollbackAndRetainedQueries) {
    stylesheet("p { margin:0 } #native-query::before { content: target-counter(url('#native-anchor'), page) }");
    DomElement* master = page_master("native", "size:200px 100px; margin:10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "before", "header", "5px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* header = static_content(sequence, "header"); ASSERT_NE(header, nullptr);
    DomElement* query = block(nullptr, nullptr, "span", header); ASSERT_NE(query, nullptr);
    ASSERT_TRUE(query->set_attribute("id", "native-query"));
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    const char* texts[] = {"Alpha", "Beta", "Gamma"};
    for (size_t i = 0; i < 3; i++) {
        size_t index = i * 3;
        fixture->contributions[index] = native_bound_box(fixture, 800 + i);
        fixture->contributions[index].kind = TYPESET_CONTRIBUTION_MARK;
        fixture->marks[i] = {TYPESET_MARK_NATIVE, "chapter", fixture->contributions[index].source, texts[i],
            lam::up((const TypesetRecord*)&fixture->exact), 0, false};
        fixture->contributions[index].mark = &fixture->marks[i];
    }
    fixture->targets[0] = {"native-anchor", native_bound_box(fixture, 900).source, 0,
        lam::up((const TypesetRecord*)&fixture->exact)};
    for (size_t index : {2u, 5u}) {
        fixture->contributions[index] = native_bound_box(fixture, 900);
        fixture->contributions[index].kind = TYPESET_CONTRIBUTION_TARGET;
        fixture->contributions[index].target = &fixture->targets[0];
    }
    // the first anchor occurs after page one's forced eject; the second closes its page range.
    fixture->contributions[1] = native_bound_box(fixture, 1000);
    fixture->contributions[1].boundary.legality = TYPESET_BREAK_FORCED;
    fixture->contributions[2].boundary.legality = TYPESET_BREAK_FORBIDDEN;
    fixture->contributions[4] = native_bound_box(fixture, 1001);
    fixture->contributions[4].boundary.legality = TYPESET_BREAK_FORCED;
    fixture->contributions[7] = native_bound_box(fixture, 1002); fixture->contribution_count = 8;
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    binding.targets = fixture->targets; binding.target_count = 1;
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    TypesetPagePolicy policy = native_bound_policy(fixture); options.page_policy = &policy;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_GT(diagnostic.reference_passes, 1u);
    EXPECT_EQ(fixture->committed_pages, 3u);
    const TypesetTarget* target = layout_secondary_target(tree, "native-anchor"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->page_number, 2u); EXPECT_EQ(target->last_page_number, 3u);
    EXPECT_TRUE(typeset_source_same_identity(target->source, fixture->targets[0].source));
    EXPECT_EQ(target->value.get(), &fixture->exact); EXPECT_NE(target->binding.get(), target->value.get());
    for (uint32_t page = 1; page <= 3; page++) {
        const TypesetMark* mark = layout_secondary_mark(tree, TYPESET_MARK_NATIVE, "chapter", page, TYPESET_MARK_START);
        ASSERT_NE(mark, nullptr); EXPECT_STREQ(mark->text, texts[page - 1]);
        EXPECT_EQ(mark->page_number, page); EXPECT_TRUE(mark->at_page_start);
        EXPECT_EQ(mark->value.get(), &fixture->exact); EXPECT_EQ(mark->source.provider, 917u);
        EXPECT_EQ(layout_secondary_mark(tree, TYPESET_MARK_NATIVE, "chapter", page, TYPESET_MARK_FIRST_EXCEPT), nullptr);
    }
    EXPECT_EQ(view_tree_native_state(tree, &fixture->contributions[0].source), nullptr);
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 2; fixture->committed_pages = 0;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->committed_pages, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
    target = layout_secondary_target(preview, "native-anchor"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(((const NativeRegionFixture*)target->value.get())->scaled_points, UINT64_C(9007199254740993));
    const TypesetMark* retained = layout_secondary_mark(preview, TYPESET_MARK_NATIVE, "chapter", 3, TYPESET_MARK_LAST);
    ASSERT_NE(retained, nullptr); EXPECT_STREQ(retained->text, "Gamma");
    EXPECT_EQ(retained->value.get(), target->value.get()); ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
}

TEST_F(SecondaryViewTest, NativeEventsRejectUndeclaredConflictingOrUnsafePayloadsBeforePublication) {
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->contribution_count = 1; fixture->contributions[0] = native_bound_box(fixture, 800);
    fixture->contributions[0].kind = TYPESET_CONTRIBUTION_MARK;
    fixture->marks[0] = {TYPESET_MARK_NATIVE, "chapter", fixture->contributions[0].source, "Alpha",
        lam::up((const TypesetRecord*)&fixture->exact), 0, false};
    fixture->contributions[0].mark = &fixture->marks[0];
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    for (size_t invalid = 0; invalid < 3; invalid++) {
        fixture->marks[0].source.generation = invalid == 0 ? 6 : 5;
        fixture->marks[0].kind = invalid == 1 ? TYPESET_MARK_RUNNING : TYPESET_MARK_NATIVE;
        fixture->marks[0].name = invalid == 2 ? "" : "chapter";
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
        ASSERT_TRUE(view_tree_model_reset(tree));
    }
    fixture->targets[0] = {"anchor", fixture->contributions[0].source, 0, lam::up((const TypesetRecord*)&fixture->exact)};
    fixture->contributions[0].kind = TYPESET_CONTRIBUTION_TARGET; fixture->contributions[0].target = &fixture->targets[0];
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); ASSERT_TRUE(view_tree_model_reset(tree));
    binding.targets = fixture->targets; binding.target_count = 1;
    DomElement* collision = block("collision"); ASSERT_NE(collision, nullptr); ASSERT_TRUE(collision->set_attribute("id", "anchor"));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeInsertionSlicesReserveBodySpaceAndRetainSelectedScratchThroughPreview) {
    init_vector_engine(); ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    stylesheet("#native-note-query::before { content:target-counter(url('#native-note'),page) }");
    DomElement* master = page_master("native", "size:200px 100px; margin:10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "before", "header", "5px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* header = static_content(sequence, "header"); ASSERT_NE(header, nullptr);
    DomElement* query = block(nullptr, nullptr, "span", header); ASSERT_NE(query, nullptr);
    ASSERT_TRUE(query->set_attribute("id", "native-note-query"));
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    ASSERT_NO_FATAL_FAILURE(native_fragment_paint_fixture(fixture));
    fixture->run.x = fixture->run.baseline_y = 0;
    fixture->image_box.content_rect = fixture->image_box.image_rect = {100, 0, 40, 20};
    fixture->region_records[0] = {{917}, UINT64_C(9007199254740993), 2, 40};
    fixture->regions[0] = native_region_material(&fixture->region_records[0], 5, 1100, true);
    fixture->regions[0].measure = native_bound_region_measure;
    fixture->targets[0] = {"native-note", fixture->regions[0].source, 0, fixture->regions[0].source.native};
    fixture->contributions[0] = native_bound_box(fixture, 1200);
    fixture->contributions[1] = native_bound_box(fixture, 1100);
    fixture->contributions[1].kind = TYPESET_CONTRIBUTION_INSERTION;
    fixture->contributions[1].source = fixture->regions[0].source;
    fixture->contributions[1].region = TYPESET_REGION_NOTE; fixture->contributions[1].region_material = &fixture->regions[0];
    fixture->contributions[2] = native_bound_box(fixture, 1201); fixture->contribution_count = 3;
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    binding.region_item = native_bound_region_item; binding.targets = fixture->targets; binding.target_count = 1;
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    TypesetPagePolicy policy = native_bound_policy(fixture); options.page_policy = &policy;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_GT(diagnostic.reference_passes, 1u);
    EXPECT_EQ(fixture->committed_pages, 2u);
    const TypesetTarget* target = layout_secondary_target(tree, "native-note"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->page_number, 1u); EXPECT_EQ(target->last_page_number, 2u);
    const ViewNodeState* notes = view_tree_native_state(tree, &fixture->regions[0].source); ASSERT_NE(notes, nullptr);
    ASSERT_EQ(notes->occurrence_count, 2u);
    for (LayoutViewNode* node = notes->first_occurrence; node; node = node->next_occurrence) {
        EXPECT_EQ(node->role, VIEW_FRAGMENT_NOTE); EXPECT_FLOAT_EQ(node->rect.y, 50); EXPECT_FLOAT_EQ(node->rect.height, 40);
        ASSERT_NE(node->first_child, nullptr); EXPECT_FLOAT_EQ(node->first_child->rect.y, 50);
        EXPECT_EQ(node->native_material->metrics.exact.get(), node->first_child->native_material->metrics.exact.get());
        EXPECT_NE(node->native_material->metrics.exact.get(), &fixture->region_records[0]);
        EXPECT_EQ(((const NativeRegionFixture*)node->native_material->solution.get())->scaled_points, UINT64_C(9007199254740993));
    }
    const ViewNodeState* moved = view_tree_native_state(tree, &fixture->contributions[2].source); ASSERT_NE(moved, nullptr);
    EXPECT_EQ(occurrence_page(moved->first_occurrence), 2u); EXPECT_FLOAT_EQ(moved->first_occurrence->rect.y, 10);
    ImageSurface* first = render_secondary_page_snapshot(tree, 1); ASSERT_NE(first, nullptr);
    ImageSurface* second = render_secondary_page_snapshot(tree, 2); ASSERT_NE(second, nullptr);
    expect_same_surface_pixels(first, second); image_surface_destroy(second);
    EXPECT_EQ(snapshot_pixel(first, 130, 60), UINT32_C(0xff2588dc));
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 1; fixture->committed_pages = 0;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u); EXPECT_EQ(fixture->committed_pages, 0u);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 10; fixture->fail_region_paint = true;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u); EXPECT_EQ(fixture->committed_pages, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
    target = layout_secondary_target(preview, "native-note"); ASSERT_NE(target, nullptr); EXPECT_EQ(target->last_page_number, 2u);
    notes = view_tree_native_state(preview, &target->source); ASSERT_NE(notes, nullptr);
    EXPECT_EQ(((const NativeRegionFixture*)notes->first_occurrence->native_material->solution.get())->scaled_points, UINT64_C(9007199254740993));
    ASSERT_NO_FATAL_FAILURE(native_fragment_export_preview(preview, first, "temp/paged-media-impl/native-regions.png",
        "temp/paged-media-impl/native-regions.svg", "temp/paged-media-impl/native-regions.pdf"));
    image_surface_destroy(first);
    ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
}

TEST_F(SecondaryViewTest, NativeDeferredFloatFlushWaitsForPlacementBeforeResumingBodyAndDiffersFromPageEject) {
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->region_records[0] = {{917}, UINT64_C(9007199254740993), 1, 30};
    fixture->regions[0] = native_region_material(&fixture->region_records[0], 5, 1600);
    fixture->regions[0].measure = native_bound_region_measure; fixture->regions[0].delay_pages = 2;
    fixture->targets[0] = {"figure", fixture->regions[0].source, 0, fixture->regions[0].source.native};
    fixture->contributions[0] = native_bound_box(fixture, 1700);
    fixture->contributions[1] = native_bound_box(fixture, 1600); fixture->contributions[1].source = fixture->regions[0].source;
    fixture->contributions[1].kind = TYPESET_CONTRIBUTION_FLOAT; fixture->contributions[1].region = TYPESET_REGION_FLOAT;
    fixture->contributions[1].region_material = &fixture->regions[0];
    fixture->contributions[2] = native_bound_box(fixture, 1800); fixture->contributions[2].kind = TYPESET_CONTRIBUTION_FLUSH_DEFERRED;
    fixture->contributions[3] = native_bound_box(fixture, 1701); fixture->contribution_count = 4;
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    binding.region_item = native_bound_region_item; binding.targets = fixture->targets; binding.target_count = 1;
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 4u);
    EXPECT_EQ(layout_secondary_target(tree, "figure")->page_number, 3u);
    const ViewNodeState* body = view_tree_native_state(tree, &fixture->contributions[3].source); ASSERT_NE(body, nullptr);
    EXPECT_EQ(occurrence_page(body->first_occurrence), 4u); EXPECT_FLOAT_EQ(body->first_occurrence->rect.y, 10);
    const ViewNodeState* floating = view_tree_native_state(tree, &fixture->regions[0].source); ASSERT_NE(floating, nullptr);
    EXPECT_EQ(floating->first_occurrence->role, VIEW_FRAGMENT_FLOAT); EXPECT_FLOAT_EQ(floating->first_occurrence->rect.y, 10);
    ASSERT_TRUE(view_tree_model_reset(tree));
    fixture->contributions[2].kind = TYPESET_CONTRIBUTION_BOUNDARY;
    fixture->contributions[2].boundary.legality = TYPESET_BREAK_FORCED;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    body = view_tree_native_state(tree, &fixture->contributions[3].source); ASSERT_NE(body, nullptr);
    EXPECT_EQ(occurrence_page(body->first_occurrence), 2u); EXPECT_EQ(layout_secondary_target(tree, "figure")->page_number, 3u);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->contributions[2].kind = TYPESET_CONTRIBUTION_FLUSH_DEFERRED;
    fixture->contribution_count = 3;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 3u);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->contribution_count = 4;
    fixture->regions[0].delay_pages = 0; fixture->contributions[1].region_edge = TYPESET_REGION_END;
    fixture->contributions[2].kind = TYPESET_CONTRIBUTION_BOUNDARY;
    fixture->contributions[2].boundary.legality = TYPESET_BREAK_FORCED;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_EQ(layout_secondary_target(tree, "figure")->page_number, 1u);
    floating = view_tree_native_state(tree, &fixture->regions[0].source); ASSERT_NE(floating, nullptr);
    EXPECT_FLOAT_EQ(floating->first_occurrence->rect.y, 60);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeRegionAdmissionAndPaintFailuresRestoreProviderQueuesAndLeases) {
    ASSERT_NE(page_master("native", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->region_records[0] = {{917}, UINT64_C(9007199254740993), 1, 40};
    fixture->regions[0] = native_region_material(&fixture->region_records[0], 5, 1900);
    fixture->regions[0].measure = native_bound_region_measure;
    fixture->contributions[0] = native_bound_box(fixture, 1900); fixture->contribution_count = 1;
    fixture->contributions[0].kind = TYPESET_CONTRIBUTION_INSERTION;
    fixture->contributions[0].source = fixture->regions[0].source;
    fixture->contributions[0].region_material = &fixture->regions[0];
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    for (size_t invalid = 0; invalid < 6; invalid++) {
        SCOPED_TRACE(invalid);
        binding.region_item = invalid == 0 ? nullptr : native_bound_region_item;
        fixture->contributions[0].region = invalid == 1 ? TYPESET_REGION_MARGIN : TYPESET_REGION_NOTE;
        fixture->contributions[0].region_edge = invalid == 2 ? static_cast<TypesetRegionEdge>(2) : TYPESET_REGION_START;
        fixture->region_records[0].line_height = invalid == 3 || invalid == 4 ? 100 : 40;
        fixture->regions[0].split = invalid == 4; fixture->fail_region_paint = invalid == 5;
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), invalid == 3 || invalid == 4 ? TYPESET_UNPLACEABLE : TYPESET_INVALID);
        EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
        EXPECT_EQ(view_tree_native_state(tree, &fixture->regions[0].source), nullptr);
        ASSERT_TRUE(view_tree_model_reset(tree));
    }
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeParagraphAlternativesResumeAcrossMasterWidthChangesAndPageRollback) {
    stylesheet("p { margin: 0 }");
    DomElement* master = page_master("native", "size: 120px 60px; margin: 10px"); ASSERT_NE(master, nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->contribution_count = 1; fixture->contributions[0] = native_bound_box(fixture, 200);
    fixture->contributions[0].kind = TYPESET_CONTRIBUTION_PARAGRAPH;
    fixture->paragraph = {fixture->items, 4, 20.0f, native_bound_alternatives, typeset_choose_lowest_cost, fixture};
    fixture->paragraph.baseline_aware = true; fixture->contributions[0].paragraph = &fixture->paragraph;
    for (size_t i = 0; i < 4; i++) {
        fixture->items[i].kind = TYPESET_BOX; fixture->items[i].source = fixture->contributions[0].source;
        fixture->items[i].start = i * 9; fixture->items[i].length = 9;
        fixture->items[i].metrics = {45, 15, 5, 15, {0, 0, 45, 20}, lam::up((const TypesetRecord*)&fixture->exact)};
    }
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 1u);
    const ViewNodeState* state = view_tree_native_state(tree, &fixture->contributions[0].source); ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->occurrence_count, 4u);
    const LayoutViewNode* node = state->first_occurrence;
    for (size_t i = 0; i < 4; i++, node = node->next_occurrence) {
        ASSERT_NE(node, nullptr); EXPECT_EQ(node->text_start, i * 9); EXPECT_EQ(node->text_length, 9u);
        EXPECT_FLOAT_EQ(node->rect.x, i % 2 ? 55.0f : 10.0f); EXPECT_FLOAT_EQ(node->rect.y, i < 2 ? 10.0f : 30.0f);
        EXPECT_EQ(node->native_material->metrics.exact.get(), &fixture->exact);
        EXPECT_EQ(node->native_material->solution.get(), &fixture->exact);
    }
    ASSERT_TRUE(master->set_attribute("style", "size: 70px 40px; margin: 10px")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 4u);
    state = view_tree_native_state(tree, &fixture->contributions[0].source); ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->occurrence_count, 4u);
    for (node = state->first_occurrence; node; node = node->next_occurrence) {
        EXPECT_FLOAT_EQ(node->rect.x, 10.0f); EXPECT_FLOAT_EQ(node->rect.y, 10.0f);
    }
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 2;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(tree->model->node_count, 1u);
    EXPECT_EQ(view_tree_native_state(tree, &fixture->contributions[0].source), nullptr);
    EXPECT_EQ(fixture->active.state[0], 0u); EXPECT_EQ(fixture->active.state[1], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeVerticalPackingUsesGlueOrdersConditionalEdgesAndBoundedShrink) {
    stylesheet("p { margin: 0 }");
    DomElement* master = page_master("native", "size: 200px 120px; margin: 10px"); ASSERT_NE(master, nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->contribution_count = 7;
    for (size_t i = 0; i < 7; i++) {
        fixture->contributions[i] = native_bound_box(fixture, 500 + i);
        fixture->contributions[i].metrics.height = 15.0f; fixture->contributions[i].metrics.depth = 5.0f;
        if (!(i % 2)) {
            fixture->contributions[i].kind = TYPESET_CONTRIBUTION_GLUE;
            fixture->contributions[i].glue = {10, 1, 0, static_cast<uint8_t>(i == 2 ? 1 : 2), 0, false, false};
            fixture->contributions[i].boundary.legality = TYPESET_BREAK_FORBIDDEN;
        }
    }
    fixture->contributions[0].glue = {99, 1, 0, 3, 0, true, false};
    fixture->contributions[6].glue = {99, 1, 0, 3, 0, false, true};
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 1u);
    const float positions[] = {10.0f, 40.0f, 90.0f};
    for (size_t i = 0; i < 3; i++) {
        const ViewNodeState* state = view_tree_native_state(tree, &fixture->contributions[i * 2 + 1].source); ASSERT_NE(state, nullptr);
        EXPECT_FLOAT_EQ(state->first_occurrence->rect.y, positions[i]);
    }
    ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_TRUE(master->set_attribute("style", "size: 200px 100px; margin: 10px"));
    fixture->contribution_count = 3;
    for (size_t i = 0; i < 3; i++) fixture->contributions[i] = native_bound_box(fixture, 600 + i);
    fixture->contributions[0].boundary.legality = TYPESET_BREAK_FORBIDDEN;
    fixture->contributions[1].kind = TYPESET_CONTRIBUTION_GLUE;
    fixture->contributions[1].glue = {30, 0, 20, 0, 0, false, false};
    fixture->contributions[1].boundary.legality = TYPESET_BREAK_FORBIDDEN;
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 1u);
    const ViewNodeState* state = view_tree_native_state(tree, &fixture->contributions[2].source); ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.y, 59.0f);
    EXPECT_FLOAT_EQ(fixture->contributions[1].glue.natural, 30.0f); EXPECT_FLOAT_EQ(fixture->contributions[1].glue.shrink, 20.0f);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->contributions[1].glue.shrink = 5.0f;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    native_fragment_release(fixture);
}

static size_t native_whole_paragraph_alternatives(const TypesetParagraph* paragraph, size_t first, float width,
        TypesetLineCandidate* candidates, size_t capacity, void*) {
    NativeFragmentFixture* fixture = (NativeFragmentFixture*)paragraph->context;
    if (capacity < 20) return 20;
    for (size_t i = 0; i < 20; i++) {
        TypesetLineCandidate line = {};
        line.first = line.paint_first = first; line.next = line.paint_end = paragraph->count;
        line.width = 45.0f; line.height = 40.0f + i; line.depth = 5.0f; line.baseline = 15.0f;
        line.cost = 20 - i; line.overflow = line.width > width;
        line.solution = lam::up((const TypesetRecord*)&fixture->solutions[i]); candidates[i] = line;
    }
    return 20;
}

TEST_F(SecondaryViewTest, NativeWholeParagraphSolutionsExceedItemCountAndRetainTheSelectedExactRecord) {
    stylesheet("p { margin: 0 }");
    ASSERT_NE(page_master("native", "size: 120px 100px; margin: 10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->contribution_count = 1; fixture->contributions[0] = native_bound_box(fixture, 700);
    fixture->contributions[0].kind = TYPESET_CONTRIBUTION_PARAGRAPH;
    fixture->items[0].kind = TYPESET_BOX; fixture->items[0].source = fixture->contributions[0].source;
    fixture->items[0].metrics = {45, 15, 5, 15, {0, 0, 45, 20}, lam::up((const TypesetRecord*)&fixture->exact)};
    fixture->paragraph = {fixture->items, 1, 20, native_whole_paragraph_alternatives, typeset_choose_lowest_cost, fixture};
    fixture->paragraph.max_alternatives = 20; fixture->paragraph.baseline_aware = true;
    fixture->contributions[0].paragraph = &fixture->paragraph;
    for (size_t i = 0; i < 20; i++) fixture->solutions[i] = {{917}, UINT64_C(9007199254740993) + i, i + 1, 1.0f};
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 1u);
    const ViewNodeState* state = view_tree_native_state(tree, &fixture->contributions[0].source); ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->occurrence_count, 1u);
    EXPECT_EQ(state->first_occurrence->native_material->solution.get(), &fixture->solutions[19]);
    EXPECT_EQ(fixture->solutions[19].scaled_points, UINT64_C(9007199254741012));
    EXPECT_FLOAT_EQ(state->first_occurrence->parent->rect.height, 59.0f);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_items = 19;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeBindingsRejectConflictingContentAndKeepForcedForbiddenBoundariesAtomic) {
    stylesheet("p { margin: 0 }");
    ASSERT_NE(page_master("native", "size: 200px 100px; margin: 10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    fixture->contribution_count = 3;
    for (size_t i = 0; i < 3; i++) fixture->contributions[i] = native_bound_box(fixture, 400 + i);
    fixture->contributions[0].boundary = {TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_PAGE, -1000, 0};
    fixture->contributions[1].boundary = {TYPESET_BREAK_FORCED, TYPESET_BREAK_PAGE, 1000, 0};
    fixture->contributions[2].boundary.legality = TYPESET_BREAK_FORCED;
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    const ViewNodeState* first = view_tree_native_state(tree, &fixture->contributions[0].source);
    const ViewNodeState* second = view_tree_native_state(tree, &fixture->contributions[1].source);
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    EXPECT_FLOAT_EQ(first->first_occurrence->rect.y, 10.0f); EXPECT_FLOAT_EQ(second->first_occurrence->rect.y, 41.0f);
    ASSERT_TRUE(view_tree_model_reset(tree));
    DomElement* authored = block("Authored", nullptr, "div", flow); ASSERT_NE(authored, nullptr);
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(diagnostic.source.address, flow);
    ASSERT_TRUE(flow->DomNode::remove_child(authored)); ASSERT_TRUE(view_tree_model_reset(tree));
    fixture->contributions[1].boundary.legality = TYPESET_BREAK_FORBIDDEN;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(tree->model->node_count, 1u); EXPECT_EQ(fixture->active.state[0], 0u);
    ASSERT_TRUE(view_tree_model_reset(tree)); fixture->admit = false;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_STALE);
    EXPECT_EQ(tree->model->page_count, 0u);
    fixture->admit = true; ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeFlowPaintUsesPageOriginsAndRetainedConsumersAfterProviderRelease) {
    init_vector_engine();
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    stylesheet("p { margin: 0 }");
    ASSERT_NE(page_master("native", "size: 200px 120px; margin: 10px"), nullptr);
    DomElement* sequence = page_sequence("native"); ASSERT_NE(sequence, nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr);
    ASSERT_TRUE(flow->set_attribute("region-name", "body"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    ASSERT_NO_FATAL_FAILURE(native_fragment_paint_fixture(fixture));
    fixture->run.x = fixture->run.baseline_y = 0.0f;
    fixture->image_box.content_rect = fixture->image_box.image_rect = {100, 0, 40, 20};
    fixture->contribution_count = 2;
    for (size_t i = 0; i < 2; i++) {
        fixture->contributions[i] = native_bound_box(fixture, 300 + i);
        fixture->contributions[i].metrics.advance = 160.0f; fixture->contributions[i].metrics.height = 36.0f;
    }
    fixture->contributions[0].boundary.legality = TYPESET_BREAK_FORCED;
    PagedNativeFlowBinding binding = native_bound_binding(fixture, flow);
    PagedLayoutOptions options = paged_layout_options_default(); options.native_flows = &binding; options.native_flow_count = 1;
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    const ViewNodeState* state = view_tree_native_state(tree, &fixture->contributions[0].source); ASSERT_NE(state, nullptr);
    ASSERT_NE(state->first_occurrence->glyph_run, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->glyph_run->x, 10.0f);
    EXPECT_FLOAT_EQ(state->first_occurrence->glyph_run->baseline_y, 34.0f);
    EXPECT_FLOAT_EQ(fixture->run.x, 0.0f); EXPECT_FLOAT_EQ(fixture->run.baseline_y, 0.0f);
    ImageSurface* first = render_secondary_page_snapshot(tree, 1); ASSERT_NE(first, nullptr);
    ImageSurface* second = render_secondary_page_snapshot(tree, 2); ASSERT_NE(second, nullptr);
    expect_same_surface_pixels(first, second); image_surface_destroy(second);
    EXPECT_EQ(snapshot_pixel(first, 130, 20), UINT32_C(0xff2588dc));
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); native_fragment_release(fixture);
    ASSERT_NO_FATAL_FAILURE(native_fragment_export_preview(preview, first, "temp/paged-media-impl/native-flow.png",
        "temp/paged-media-impl/native-flow.svg", "temp/paged-media-impl/native-flow.pdf"));
    image_surface_destroy(first);
    ASSERT_TRUE(view_tree_secondary_release(&doc, preview));
}

TEST_F(SecondaryViewTest, NativeFragmentsRetainExactSourcesThroughNestedRollbackAndGenerationLeases) {
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    ViewNativeMaterial material = native_fragment_material(fixture);
    ViewTree* tree = secondary(); ASSERT_NE(tree, nullptr);
    ViewPageBox* page = view_tree_page_append(tree, 200, 120, {0, 0, 200, 120}, VIEW_PAGE_RIGHT);
    ASSERT_NE(page, nullptr);
    ViewModelCheckpoint* outer = view_tree_model_checkpoint(tree); ASSERT_NE(outer, nullptr);
    ViewModelStatus status = VIEW_MODEL_INVALID_ARGUMENT;
    LayoutViewNode* first = view_tree_native_fragment_append(tree, &page->node, &material, {20, 16, 80, 31}, &status);
    ASSERT_NE(first, nullptr); EXPECT_EQ(status, VIEW_MODEL_OK); EXPECT_EQ(ref_count_get(&fixture->references), 2);
    EXPECT_EQ(first->source.address, nullptr); ASSERT_NE(first->state, nullptr);
    EXPECT_EQ(first->state->computed_style, nullptr); EXPECT_EQ(tree->model->css, nullptr);
    EXPECT_EQ(view_tree_native_state(tree, &material.source), first->state.get());
    EXPECT_EQ(first->state->first_occurrence.get(), first); EXPECT_EQ(first->state->occurrence_count, 1u);
    EXPECT_EQ(first->native_material->source.node, 81u); EXPECT_EQ(first->text_start, 13u); EXPECT_EQ(first->text_length, 29u);
    EXPECT_EQ(first->native_material->metrics.exact.get(), &fixture->exact);
    EXPECT_EQ(fixture->exact.scaled_points, UINT64_C(9007199254740993));
    EXPECT_FLOAT_EQ(first->native_material->metrics.height, 24); EXPECT_FLOAT_EQ(first->native_material->metrics.depth, 7);
    EXPECT_FLOAT_EQ(first->native_material->metrics.baseline, 24);
    LayoutViewRef first_ref = first->ref;
    ViewModelCheckpoint* inner = view_tree_model_checkpoint(tree); ASSERT_NE(inner, nullptr);
    LayoutViewNode* rejected = view_tree_native_fragment_append(tree, &page->node, &material, {20, 60, 80, 31});
    ASSERT_NE(rejected, nullptr); LayoutViewRef rejected_ref = rejected->ref;
    EXPECT_EQ(ref_count_get(&fixture->references), 3);
    EXPECT_EQ(first->next_occurrence.get(), rejected); EXPECT_EQ(first->state->last_occurrence.get(), rejected);
    EXPECT_EQ(first->state->occurrence_count, 2u);
    ASSERT_TRUE(view_tree_model_restore(tree, inner)); EXPECT_EQ(ref_count_get(&fixture->references), 2);
    EXPECT_EQ(view_tree_node_resolve(tree, rejected_ref), nullptr); EXPECT_EQ(page->node.last_child.get(), first);
    EXPECT_EQ(first->next_occurrence, nullptr); EXPECT_EQ(first->state->last_occurrence.get(), first);
    EXPECT_EQ(first->state->occurrence_count, 1u);
    ASSERT_TRUE(view_tree_model_restore(tree, outer)); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    EXPECT_EQ(page->node.first_child, nullptr); EXPECT_EQ(tree->model->native_leases, nullptr);
    EXPECT_EQ(view_tree_native_state(tree, &material.source), nullptr);
    EXPECT_EQ(view_tree_node_resolve(tree, first_ref), nullptr);
    material.metrics.height = INFINITY;
    EXPECT_EQ(view_tree_native_fragment_append(tree, &page->node, &material, {0, 0, 1, 1}, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_INVALID_ARGUMENT); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    material = native_fragment_material(fixture); fixture->admit = false;
    EXPECT_EQ(view_tree_native_fragment_append(tree, &page->node, &material, {0, 0, 1, 1}, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_STALE_SOURCE); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    fixture->admit = true; material.source.provider++;
    EXPECT_EQ(view_tree_native_fragment_append(tree, &page->node, &material, {0, 0, 1, 1}, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_INVALID_ARGUMENT); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    material = native_fragment_material(fixture);
    LayoutViewNode* committed = view_tree_native_fragment_append(tree, &page->node, &material, {20, 16, 80, 31});
    ASSERT_NE(committed, nullptr); EXPECT_GT(committed->ref.node_id, rejected_ref.node_id);
    ASSERT_TRUE(view_tree_model_commit(tree));
    ViewPreviewOptions preview = view_preview_options_default(); ViewPageSelection selection = {true, nullptr, 0};
    ViewTree* retained = view_tree_page_instances_create(tree, &selection, &preview); ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); EXPECT_EQ(ref_count_get(&fixture->references), 2);
    EXPECT_EQ(view_tree_native_state(tree, &material.source), nullptr);
    EXPECT_EQ(view_tree_native_state(retained, &material.source), committed->state.get());
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); EXPECT_EQ(ref_count_get(&fixture->references), 2);
    const ViewPageBox* old_page = view_tree_page_material(retained, retained->model->pages.get()[0]);
    ASSERT_NE(old_page, nullptr); EXPECT_EQ(old_page->node.first_child.get(), committed);
    EXPECT_EQ(committed->native_material->metrics.exact.get(), &fixture->exact);
    ASSERT_TRUE(view_tree_secondary_release(&doc, retained)); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeSourceIndexSeparatesProvidersGenerationsDomMappingsAndEditions) {
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    ViewNativeMaterial material = native_fragment_material(fixture);
    material.source.node = dom_node_ref(source).expected_id;
    ViewTree* trees[] = {secondary(), secondary()};
    ViewPageBox* pages[2] = {};
    LayoutViewNode* fragments[2] = {};
    for (size_t i = 0; i < 2; i++) {
        ASSERT_NE(trees[i], nullptr);
        pages[i] = view_tree_page_append(trees[i], 200, 120, {0, 0, 200, 120}, VIEW_PAGE_RIGHT);
        ASSERT_NE(pages[i], nullptr);
        fragments[i] = view_tree_native_fragment_append(trees[i], &pages[i]->node, &material, {20, 16, 80, 31});
        ASSERT_NE(fragments[i], nullptr);
        EXPECT_EQ(view_tree_native_state(trees[i], &material.source), fragments[i]->state.get());
    }
    EXPECT_NE(fragments[0]->state, fragments[1]->state);
    LayoutViewNode* dom = view_tree_fragment_append(trees[0], &pages[0]->node, source, {0, 0, 1, 1});
    ASSERT_NE(dom, nullptr); EXPECT_NE(dom->state, fragments[0]->state);
    EXPECT_EQ(view_tree_node_state(trees[0], source, false), dom->state.get());
    ViewNativeMaterial continuation = material; continuation.start = 42; continuation.length = 3;
    LayoutViewNode* tail = view_tree_native_fragment_append(trees[0], &pages[0]->node, &continuation, {20, 60, 80, 31});
    ASSERT_NE(tail, nullptr); EXPECT_EQ(tail->state, fragments[0]->state);
    EXPECT_EQ(fragments[0]->next_occurrence.get(), tail); EXPECT_EQ(tail->text_start, 42u);
    EXPECT_EQ(fragments[0]->state->occurrence_count, 2u);
    for (size_t field = 0; field < 2; field++) {
        ViewNativeMaterial other = material; other.source.native = nullptr; other.metrics.exact = nullptr;
        if (field) other.source.generation++; else other.source.provider++;
        EXPECT_EQ(view_tree_native_state(trees[0], &other.source), nullptr);
        LayoutViewNode* separate = view_tree_native_fragment_append(trees[0], &pages[0]->node, &other, {0, 0, 1, 1});
        ASSERT_NE(separate, nullptr); EXPECT_NE(separate->state, fragments[0]->state);
        EXPECT_EQ(view_tree_native_state(trees[0], &other.source), separate->state.get());
        EXPECT_EQ(view_tree_native_state(trees[1], &other.source), nullptr);
    }
    TypesetSource query = material.source; query.native = nullptr;
    EXPECT_EQ(view_tree_native_state(trees[0], &query), fragments[0]->state.get());
    continuation.source.offset_unit = TYPESET_UTF8_BYTES;
    ViewModelStatus status = VIEW_MODEL_OK;
    EXPECT_EQ(view_tree_native_fragment_append(trees[0], &pages[0]->node, &continuation, {0, 0, 1, 1}, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_INVALID_ARGUMENT);
    EXPECT_EQ(view_tree_native_state(trees[0], &continuation.source), nullptr);
    EXPECT_EQ(fragments[0]->state->occurrence_count, 2u); EXPECT_EQ(tail->next_occurrence, nullptr);
    ASSERT_TRUE(view_tree_model_reset(trees[0]));
    EXPECT_EQ(view_tree_native_state(trees[0], &material.source), nullptr);
    EXPECT_EQ(view_tree_native_state(trees[1], &material.source), fragments[1]->state.get());
    for (ViewTree* tree : trees) ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    EXPECT_EQ(ref_count_get(&fixture->references), 1); EXPECT_EQ(fixture->retained, fixture->released);
    native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, NativeGlyphFragmentsSharePaintSnapshotsAndPdfAfterProducerViewRelease) {
    init_vector_engine();
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    NativeFragmentFixture* fixture = native_fragment_fixture(); ASSERT_NE(fixture, nullptr);
    ASSERT_NO_FATAL_FAILURE(native_fragment_paint_fixture(fixture));
    ViewNativeMaterial material = native_fragment_material(fixture); material.glyph_run = lam::up((const PaintGlyphRun*)&fixture->run);
    material.image_box = lam::up((const PaintImageBox*)&fixture->image_box);
    ViewTree* tree = secondary(); ViewPageBox* page = view_tree_page_append(tree, 200, 120, {0, 0, 200, 120}, VIEW_PAGE_RIGHT);
    ASSERT_NE(page, nullptr);
    LayoutViewNode* fragment = view_tree_native_fragment_append(tree, &page->node, &material, {20, 16, 80, 43});
    ASSERT_NE(fragment, nullptr); EXPECT_NE(fragment->glyph_run.get(), &fixture->run);
    EXPECT_EQ(fragment->native_material->glyph_run.get(), fragment->glyph_run.get());
    EXPECT_EQ(fragment->glyph_run->owned_text, nullptr); EXPECT_EQ(fragment->glyph_run->text.get(), fixture->run.text.get());
    EXPECT_NE(fragment->image_box.get(), &fixture->image_box);
    EXPECT_EQ(fragment->native_material->image_box.get(), fragment->image_box.get());
    ASSERT_TRUE(view_tree_model_commit(tree)); EXPECT_EQ(tree->model->css, nullptr);
    ASSERT_TRUE(render_secondary_view_to_svg(tree, "temp/paged-media-impl/native-material.svg", 1, 1));
    char* svg = read_text_file("temp/paged-media-impl/native-material.svg"); ASSERT_NE(svg, nullptr);
    EXPECT_NE(strstr(svg, "<path d="), nullptr); EXPECT_NE(strstr(svg, "data:image/png;base64,"), nullptr);
    free(svg);
    ImageSurface* before = render_secondary_page_snapshot(tree, 1); ASSERT_NE(before, nullptr);
    for (size_t glyph = 0; glyph < 2; glyph++) {
        size_t painted = 0;
        for (size_t y = 25 + glyph * 12; y < 40 + glyph * 12; y++)
            for (size_t x = 20 + glyph * 40; x < 40 + glyph * 40; x++)
                if (snapshot_pixel(before, x, y) != UINT32_C(0xffffffff)) painted++;
        EXPECT_GT(painted, 20u);
    }
    EXPECT_EQ(snapshot_pixel(before, 140, 35), UINT32_C(0xff2588dc));
    ViewPreviewOptions preview = view_preview_options_default(); ViewPageSelection selection = {true, nullptr, 0};
    ViewTree* retained = view_tree_page_instances_create(tree, &selection, &preview); ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ImageSurface* after = render_secondary_page_snapshot(retained, 1); ASSERT_NE(after, nullptr);
    EXPECT_EQ(before->width, after->width); EXPECT_EQ(before->height, after->height);
    expect_same_surface_pixels(before, after);
    save_surface_to_png(after, "temp/paged-media-impl/native-material.png");
    image_surface_destroy(before); image_surface_destroy(after);
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    ASSERT_TRUE(render_secondary_view_to_pdf(retained, "temp/paged-media-impl/native-glyphs.pdf"));
    char* pdf = read_text_file("temp/paged-media-impl/native-glyphs.pdf"); ASSERT_NE(pdf, nullptr);
    EXPECT_NE(strstr(pdf, "/Count 1"), nullptr); EXPECT_NE(strstr(pdf, "/MediaBox [0 0 150.00 90.00]"), nullptr);
    free(pdf); EXPECT_EQ(source->x, 11.25f); EXPECT_EQ(source->height, 91.75f);
    ASSERT_TRUE(view_tree_secondary_release(&doc, retained)); EXPECT_EQ(ref_count_get(&fixture->references), 1);
    EXPECT_EQ(fixture->retained, fixture->released); native_fragment_release(fixture);
}

TEST_F(SecondaryViewTest, PositionedGlyphIdsLowerWithoutTextOrRemeasurement) {
    stylesheet("p { font-size: 18px }");
    ViewTree* tree = secondary();
    ViewCssStyle* style = view_css_resolve(tree, source);
    ASSERT_NE(style, nullptr);
    uint32_t glyphs[] = {font_get_glyph_index(style->font.font_handle, 'A'), font_get_glyph_index(style->font.font_handle, 'A')};
    ASSERT_NE(glyphs[0], 0u);
    float xs[] = {0.0f, 60.0f}, ys[] = {0.0f, 20.0f};
    PaintGlyphRun run = {};
    run.font = lam::up(&style->font_box); run.color = style->color; run.font_size = 18.0f;
    run.x = 20.0f; run.baseline_y = 40.0f;
    run.glyph_ids = lam::up(glyphs); run.xs = lam::up(xs); run.ys = lam::up(ys); run.count = 1;
    RdtPath* path = render_path_create_glyph_run(&run);
    ASSERT_NE(path, nullptr);
    float l, t, r, b;
    ASSERT_TRUE(rdt_path_get_bounds(path, &l, &t, &r, &b));
    rdt_path_free(path);
    run.count = 2;
    path = render_path_create_glyph_run(&run);
    ASSERT_NE(path, nullptr);
    float l2, t2, r2, b2;
    ASSERT_TRUE(rdt_path_get_bounds(path, &l2, &t2, &r2, &b2));
    EXPECT_NEAR(r2 - r, 60.0f, 0.001f);
    EXPECT_NEAR(b2 - b, 20.0f, 0.001f);
    rdt_path_free(path);
    ViewPageBox* page = view_tree_page_append(tree, 200.0f, 120.0f, {0, 0, 200, 120}, VIEW_PAGE_RIGHT);
    ASSERT_NE(page, nullptr);
    LayoutViewNode* fragment = view_tree_fragment_append(tree, &page->node, source, {20, 20, 100, 50});
    ASSERT_NE(fragment, nullptr);
    fragment->glyph_run = lam::up(&run);
    ASSERT_TRUE(view_tree_model_commit(tree));
    PaintList paint = {}; paint_list_init(&paint, nullptr);
    ASSERT_TRUE(layout_secondary_paint_page(tree, page, &paint));
    StrBuf* svg = strbuf_new();
    PaintSvgLoweringStats stats = {};
    paint_ir_lower_svg(&paint, svg, nullptr, &stats);
    EXPECT_EQ(stats.unsupported_count, 0);
    EXPECT_NE(strstr(svg->str, "<path d="), nullptr);
    strbuf_free(svg); paint_list_destroy(&paint);
    ASSERT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/positioned-glyphs.pdf"));
}

TEST_F(SecondaryViewTest, ParagraphResumesAtChangedPageWidthWithExactSourceCoverage) {
    stylesheet("@page { size: 140px 80px; margin: 10px } @page :left { size: 100px 80px } "
        "p { margin: 0; font-size: 10px; line-height: 12px }");
    const char* words = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu "
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu "
        "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu";
    DomText* content = DomText::create_copy(words, strlen(words), source);
    ASSERT_NE(content, nullptr);
    ASSERT_TRUE(source->DomNode::append_child(content));
    ViewTree* tree = secondary();
    PagedLayoutOptions options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, nullptr), TYPESET_OK);
    ASSERT_GT(tree->model->page_count, 1u);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->content_rect.width, 120.0f);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[1]->content_rect.width, 80.0f);
    ViewNodeState* state = view_tree_node_state(tree, content, false);
    ASSERT_NE(state, nullptr);
    size_t offset = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        EXPECT_EQ(fragment->text_start, offset);
        offset += fragment->text_length;
    }
    EXPECT_EQ(offset, strlen(words));
    EXPECT_EQ(content->rect, nullptr);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, ResetInvalidatesOnlyItsOwnGeneration) {
    ViewTree* a = secondary();
    ViewTree* b = secondary();
    pages(a, 2);
    pages(b, 1);
    LayoutViewRef old_a = a->model->pages.get()[0]->node.ref;
    LayoutViewRef old_b = b->model->pages.get()[0]->node.ref;
    a->reset_retained();
    EXPECT_EQ(view_tree_node_resolve(a, old_a), nullptr);
    EXPECT_NE(view_tree_node_resolve(b, old_b), nullptr);
    EXPECT_EQ(a->model->page_count, 0u);
    EXPECT_EQ(b->model->page_count, 1u);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
    pages(a, 1);
    EXPECT_EQ(view_tree_node_resolve(a, old_a), nullptr);
}

TEST_F(SecondaryViewTest, SourceEpochInvalidatesRetainedViews) {
    ViewTree* a = secondary();
    ViewTree* b = secondary();
    pages(a, 2);
    pages(b, 2);
    doc.mutation_epoch++;
    ViewPreviewOptions options = view_preview_options_default();
    EXPECT_EQ(view_tree_preview_arrange(a, nullptr, &options), VIEW_MODEL_STALE_SOURCE);
    EXPECT_EQ(view_tree_preview_arrange(b, nullptr, &options), VIEW_MODEL_STALE_SOURCE);
    a->reset_retained();
    pages(a, 1);
    EXPECT_EQ(view_tree_preview_arrange(a, nullptr, &options), VIEW_MODEL_OK);
    EXPECT_FALSE(view_tree_model_source_valid(b));
}

TEST_F(SecondaryViewTest, GridGroupsFivePagesWithoutCreatingAnotherPage) {
    ViewTree* tree = secondary();
    pages(tree, 5);
    ViewPreviewOptions options = view_preview_options_default();
    options.rows = options.columns = 2;
    options.scale = 0.5f;
    options.padding = 10.0f;
    options.column_gap = 20.0f;
    options.row_gap = 30.0f;
    options.group_gap = 40.0f;
    ASSERT_EQ(view_tree_preview_arrange(tree, nullptr, &options), VIEW_MODEL_OK);
    ASSERT_EQ(tree->model->placement_count, 5u);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[0].rect.x, 10.0f);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[1].rect.x, 80.0f);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[2].rect.y, 140.0f);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[4].rect.y, 280.0f);
    EXPECT_FLOAT_EQ(tree->model->preview_bounds.width, 140.0f);
    EXPECT_FLOAT_EQ(tree->model->preview_bounds.height, 390.0f);
    EXPECT_EQ(tree->model->page_count, 5u);
    options.groups_horizontal = true;
    ASSERT_EQ(view_tree_preview_arrange(tree, nullptr, &options), VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[4].rect.x, 170.0f);
    EXPECT_FLOAT_EQ(tree->model->preview_bounds.width, 230.0f);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->node.rect.width, 100.0f);
}

static void expect_same_surface_pixels(const ImageSurface* left, const ImageSurface* right) {
    ASSERT_NE(left, nullptr); ASSERT_NE(right, nullptr);
    ASSERT_EQ(left->width, right->width); ASSERT_EQ(left->height, right->height);
    for (int y = 0; y < left->height; y++) EXPECT_EQ(memcmp((const uint8_t*)left->pixels + y * left->pitch,
        (const uint8_t*)right->pixels + y * right->pitch, (size_t)left->width * 4), 0) << "row " << y;
}

static uint32_t snapshot_pixel(const ImageSurface* surface, size_t x, size_t y) {
    return ((uint32_t*)((uint8_t*)surface->pixels + y * surface->pitch))[x];
}

struct PageSnapshotCacheScope {
    RenderPageSnapshotCache* cache;
    ~PageSnapshotCacheScope() { render_page_snapshot_cache_destroy(cache); }
};

static void expect_same_page_pixels(ViewTree* left, ViewTree* right, uint32_t number) {
    ImageSurface* a = render_secondary_page_snapshot(left, number, 0.5f);
    ImageSurface* b = render_secondary_page_snapshot(right, number, 0.5f);
    EXPECT_NE(a, nullptr); EXPECT_NE(b, nullptr);
    if (a && b) expect_same_surface_pixels(a, b);
    image_surface_destroy(a); image_surface_destroy(b);
}

TEST_F(SecondaryViewTest, WindowPreviewRecomposesWithoutReplacingDefaultGeometryOrGrowingItsSurface) {
    preview_document();
    RenderPagedOptions options = render_paged_options_default();
    UiContext ui = {}; ui.document = lam::up(&doc); ui.paged_options = lam::up(&options);
    ui.device_scale = 1.0f; ui.viewport_width = 120.0f; ui.viewport_height = 160.0f;
    ViewTree* browsing = doc.view_tree;
    ASSERT_TRUE(render_paged_window_compose(&ui));
    EXPECT_EQ(doc.view_tree.get(), browsing); EXPECT_FLOAT_EQ(source->width, 640.0f);
    uint64_t original_id = ui.paged_view->model->tree_id;
    ImageSurface* surface = render_surface_create_budgeted((MemContext*)doc.services.mem_ctx, 120.0f, 160.0f);
    ASSERT_NE(surface, nullptr); ui.surface = lam::up(surface);
    render_paged_window_scroll(&ui, 0.0f, 160.0f);
    render_html_doc(&ui, browsing, nullptr);
    EXPECT_EQ(snapshot_pixel(surface, 25, 70), 0xff00ff00u);
    DomElement* first = (DomElement*)source->first_child.get();
    EXPECT_TRUE(first->set_attribute("style", "background: #ffffff"));
    // this low-level fixture has no DOM host to advance the mutation epoch after the write.
    doc.mutation_epoch++;
    EXPECT_FALSE(view_tree_model_source_valid(ui.paged_view));
    render_paged_window_scroll(&ui, 0.0f, 0.0f);
    render_html_doc(&ui, browsing, nullptr);
    EXPECT_NE(ui.paged_view->model->tree_id, original_id);
    EXPECT_EQ(snapshot_pixel(surface, 25, 70), 0xffffffffu);
    EXPECT_EQ(ui.surface.get(), surface); EXPECT_EQ(surface->width, 120); EXPECT_EQ(surface->height, 160);
    EXPECT_EQ(doc.view_tree.get(), browsing); EXPECT_FLOAT_EQ(source->x, 11.25f);
    EXPECT_FLOAT_EQ(source->width, 640.0f); EXPECT_FLOAT_EQ(source->height, 91.75f);
    options.preview_pages = lam::up("99");
    ViewTree* committed = ui.paged_view;
    EXPECT_FALSE(render_paged_window_compose(&ui));
    EXPECT_EQ(ui.paged_view.get(), committed); EXPECT_TRUE(view_tree_model_source_valid(committed));
    image_surface_destroy(surface);
}

TEST_F(SecondaryViewTest, PageInstanceRootsShareFinalizedContentWithIndependentPlacements) {
    preview_document();
    DomElement* first = (DomElement*)source->first_child.get();
    ASSERT_TRUE(first->set_attribute("id", "first"));
    ViewTree* original = secondary(); ViewTree* sibling = secondary(); pages(sibling, 1);
    PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(original, &layout, nullptr), TYPESET_OK);
    ViewPageBox* material = original->model->pages.get()[0];
    LayoutViewNode* parent = material->node.parent;
    LayoutViewRef sibling_page = sibling->model->pages.get()[0]->node.ref;
    ViewNodeState* state = view_tree_node_state(original, first->first_child, false); ASSERT_NE(state, nullptr);
    PaintGlyphRun* run = state->first_occurrence->glyph_run;
    ViewPreviewOptions grid = view_preview_options_default(); grid.rows = grid.columns = 2; grid.scale = 0.5f;
    ViewModelStatus status = VIEW_MODEL_INVALID_ARGUMENT;
    ViewTree* a = view_tree_page_instances_create(original, nullptr, &grid, &status);
    ASSERT_NE(a, nullptr); EXPECT_EQ(status, VIEW_MODEL_OK);
    ViewPreviewOptions book = grid; book.arrangement = VIEW_PAGES_BOOK; book.anchor_page = 2;
    ViewTree* b = view_tree_page_instances_create(a, nullptr, &book, &status); ASSERT_NE(b, nullptr);
    ViewPageRange range = {5, 5}; ViewPageSelection selection = {false, &range, 1};
    ViewPreviewOptions thumbnail = view_preview_options_default(); thumbnail.scale = 0.25f;
    ViewTree* c = view_tree_page_instances_create(b, &selection, &thumbnail, &status); ASSERT_NE(c, nullptr);
    EXPECT_EQ(a->model->page_count, 5u); EXPECT_EQ(b->model->page_count, 5u); EXPECT_EQ(c->model->page_count, 5u);
    EXPECT_EQ(a->model->placement_count, 5u); EXPECT_EQ(b->model->placement_count, 2u); EXPECT_EQ(c->model->placement_count, 1u);
    EXPECT_EQ(c->model->placements.get()[0].page_number, 5u);
    EXPECT_EQ(original->model->placement_count, 0u);
    ViewPagePlacement retained = a->model->placements.get()[0];
    book.right_binding = true;
    ASSERT_EQ(view_tree_preview_arrange(b, nullptr, &book), VIEW_MODEL_OK);
    EXPECT_FLOAT_EQ(a->model->placements.get()[0].rect.x, retained.rect.x);
    ViewTree* instances[] = {a, b, c};
    for (ViewTree* instance : instances) {
        EXPECT_EQ(instance->model->pages.get()[0]->node.kind, LAYOUT_VIEW_PAGE_INSTANCE);
        EXPECT_EQ(instance->model->pages.get()[0]->node.parent.get(), instance->model->root.get());
        EXPECT_EQ(instance->model->pages.get()[0]->node.first_child, nullptr);
        EXPECT_EQ(view_tree_page_material(instance, instance->model->pages.get()[0]), material);
        EXPECT_EQ(view_tree_page_content_owner(instance), original);
        EXPECT_EQ(view_tree_node_state(instance, first->first_child, false), state);
        EXPECT_EQ(view_tree_node_state(instance, first->first_child, true), nullptr);
        EXPECT_EQ(view_tree_node_resolve(instance, material->node.ref), nullptr);
        EXPECT_EQ(layout_secondary_target(instance, "first"), layout_secondary_target(original, "first"));
        EXPECT_FALSE(view_tree_model_touch_node(instance, &instance->model->pages.get()[0]->node));
        expect_same_page_pixels(instance, original, 1);
    }
    PageSnapshotCacheScope cache = {render_page_snapshot_cache_create(4, 1024 * 1024)}; ASSERT_NE(cache.cache, nullptr);
    const ImageSurface* pixels = render_page_snapshot_cache_get(cache.cache, original, 1, 0.5f); ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(render_page_snapshot_cache_get(cache.cache, a, 1, 0.5f), pixels);
    EXPECT_EQ(render_page_snapshot_cache_get(cache.cache, c, 1, 0.5f), pixels);
    EXPECT_EQ(render_page_snapshot_cache_stats(cache.cache).renders, 1u);
    EXPECT_EQ(render_page_snapshot_cache_stats(cache.cache).hits, 2u);
    EXPECT_EQ(material->node.parent.get(), parent); EXPECT_EQ(state->first_occurrence->glyph_run.get(), run);
    EXPECT_EQ(view_tree_node_resolve(sibling, sibling_page), &sibling->model->pages.get()[0]->node);
    EXPECT_FLOAT_EQ(source->width, 640.0f); EXPECT_FLOAT_EQ(source->x, 11.25f);
}

TEST_F(SecondaryViewTest, PageInstancesRetainOldArenaAndFontsAcrossSourceResetAndRelease) {
    preview_document();
    uint32_t baseline = mem_context_live_count((MemContext*)doc.services.mem_ctx);
    ViewTree* original = secondary(); PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(original, &layout, nullptr), TYPESET_OK);
    ViewPageBox* old_page = original->model->pages.get()[0]; LayoutViewRef old_ref = old_page->node.ref;
    ViewPreviewOptions options = view_preview_options_default(); options.columns = 2; options.scale = 0.5f;
    ViewTree* old_preview = view_tree_page_instances_create(original, nullptr, &options); ASSERT_NE(old_preview, nullptr);
    original->reset_retained();
    EXPECT_EQ(view_tree_node_resolve(original, old_ref), nullptr); EXPECT_FALSE(original->model->committed);
    EXPECT_EQ(original->model->page_count, 0u); EXPECT_EQ(original->model->tree_id, old_ref.tree_id);
    ViewTree* retired = view_tree_page_content_owner(old_preview); ASSERT_NE(retired, nullptr); EXPECT_NE(retired, original);
    EXPECT_EQ(view_tree_node_resolve(retired, old_ref), &old_page->node);
    EXPECT_FALSE(view_tree_model_reset(retired));
    view_pool_destroy(retired);
    EXPECT_EQ(view_tree_node_resolve(retired, old_ref), &old_page->node);
    EXPECT_EQ(view_tree_page_material(old_preview, old_preview->model->pages.get()[0]), old_page);
    original->model->environment.page_width = 200.0f;
    ASSERT_EQ(layout_secondary_view(original, &layout, nullptr), TYPESET_OK);
    EXPECT_NE(original->model->pages.get()[0], old_page);
    ViewTree* new_preview = view_tree_page_instances_create(original, nullptr, &options); ASSERT_NE(new_preview, nullptr);
    ViewTree* another_old = view_tree_page_instances_create(old_preview, nullptr, &options); ASSERT_NE(another_old, nullptr);
    ASSERT_TRUE(view_tree_secondary_release(&doc, original));
    ASSERT_TRUE(view_tree_secondary_release(&doc, old_preview));
    expect_same_page_pixels(another_old, new_preview, 1);
    EXPECT_EQ(view_tree_node_resolve(retired, old_ref), &old_page->node);
    ASSERT_TRUE(view_tree_secondary_release(&doc, new_preview));
    ImageSurface* page = render_secondary_page_snapshot(another_old, 1, 0.5f); ASSERT_NE(page, nullptr);
    EXPECT_EQ(snapshot_pixel(page, 10, 35), 0xff0000ffu); image_surface_destroy(page);
    ASSERT_TRUE(view_tree_secondary_release(&doc, another_old));
    EXPECT_EQ(mem_context_live_count((MemContext*)doc.services.mem_ctx), baseline);
    EXPECT_EQ(doc.secondary_view_trees, nullptr); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, PageInstanceResetAndRawSourceDestroyRespectGenerationLeases) {
    preview_document();
    uint32_t baseline = mem_context_live_count((MemContext*)doc.services.mem_ctx);
    ViewTree* original = secondary(); PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(original, &layout, nullptr), TYPESET_OK);
    ViewPreviewOptions options = view_preview_options_default();
    ViewTree* a = view_tree_page_instances_create(original, nullptr, &options); ASSERT_NE(a, nullptr);
    ViewTree* b = view_tree_page_instances_create(a, nullptr, &options); ASSERT_NE(b, nullptr);
    LayoutViewRef old_instance = a->model->pages.get()[0]->node.ref;
    ASSERT_TRUE(view_tree_model_reset(a));
    EXPECT_FALSE(a->model->page_instances); EXPECT_EQ(view_tree_node_resolve(a, old_instance), nullptr);
    ASSERT_EQ(layout_secondary_view(a, &layout, nullptr), TYPESET_OK);
    expect_same_page_pixels(a, b, 1);
    view_pool_destroy(original);
    EXPECT_EQ(original->model, nullptr);
    ASSERT_NE(view_tree_page_content_owner(b), nullptr);
    expect_same_page_pixels(a, b, 1);
    ASSERT_TRUE(view_tree_secondary_release(&doc, b));
    ASSERT_TRUE(view_tree_secondary_release(&doc, a));
    ASSERT_TRUE(view_tree_secondary_release(&doc, original));
    EXPECT_EQ(mem_context_live_count((MemContext*)doc.services.mem_ctx), baseline);
}

TEST_F(SecondaryViewTest, PageInstancesRejectInvalidAndStaleInputsWithoutChangingTheEdition) {
    ViewTree* original = secondary(); ViewPreviewOptions options = view_preview_options_default();
    ViewModelStatus status = VIEW_MODEL_OK;
    EXPECT_EQ(view_tree_page_instances_create(original, nullptr, &options, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_INVALID_ARGUMENT);
    pages(original, 3); ViewPageBox* material = original->model->pages.get()[0];
    ViewPageRange range = {4, 4}; ViewPageSelection selection = {false, &range, 1};
    uint32_t baseline = mem_context_live_count((MemContext*)doc.services.mem_ctx);
    EXPECT_EQ(view_tree_page_instances_create(original, &selection, &options, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_INVALID_PAGE_RANGE);
    options.scale = NAN;
    EXPECT_EQ(view_tree_page_instances_create(original, nullptr, &options, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_INVALID_ARGUMENT);
    EXPECT_EQ(original->model->page_generation, nullptr); EXPECT_EQ(doc.secondary_view_trees.get(), original);
    EXPECT_EQ(mem_context_live_count((MemContext*)doc.services.mem_ctx), baseline);
    options = view_preview_options_default();
    memtrack_fault_inject(0);
    EXPECT_EQ(view_tree_page_instances_create(original, nullptr, &options, &status), nullptr);
    memtrack_fault_clear(); EXPECT_EQ(status, VIEW_MODEL_OUT_OF_MEMORY);
    EXPECT_EQ(original->model->page_generation, nullptr);
    EXPECT_EQ(mem_context_live_count((MemContext*)doc.services.mem_ctx), baseline);
    ViewTree* instance = view_tree_page_instances_create(original, nullptr, &options); ASSERT_NE(instance, nullptr);
    baseline = mem_context_live_count((MemContext*)doc.services.mem_ctx);
    memtrack_fault_inject(0);
    original->reset_retained();
    memtrack_fault_clear();
    EXPECT_TRUE(original->model->committed); EXPECT_EQ(original->model->pages.get()[0], material);
    EXPECT_EQ(view_tree_page_content_owner(instance), original);
    EXPECT_EQ(mem_context_live_count((MemContext*)doc.services.mem_ctx), baseline);
    doc.mutation_epoch++;
    EXPECT_EQ(view_tree_page_content_owner(instance), nullptr);
    EXPECT_EQ(view_tree_page_material(instance, instance->model->pages.get()[0]), nullptr);
    EXPECT_EQ(view_tree_page_instances_create(instance, nullptr, &options, &status), nullptr);
    EXPECT_EQ(status, VIEW_MODEL_STALE_SOURCE);
    EXPECT_EQ(render_secondary_page_snapshot(instance, 1), nullptr);
}

TEST_F(SecondaryViewTest, PageInstancesRenderIndependentRootsAndExportTheCompletePhysicalEdition) {
    ASSERT_TRUE(create_dir("temp/paged-media-instances")); preview_document();
    ViewTree* original = secondary(); PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(original, &layout, nullptr), TYPESET_OK);
    ViewPageRange range = {3, 3}; ViewPageSelection selection = {false, &range, 1};
    ViewPreviewOptions options = view_preview_options_default(); options.arrangement = VIEW_PAGES_BOOK;
    options.anchor_page = 2; options.scale = 0.5f; options.column_gap = 8.0f;
    ViewTree* instance = view_tree_page_instances_create(original, &selection, &options); ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->model->page_count, 5u); EXPECT_EQ(instance->model->placement_count, 1u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, original));
    ImageSurface* book = render_secondary_view_snapshot(instance, 1.0f, Color{0xff808080}); ASSERT_NE(book, nullptr);
    EXPECT_EQ(book->width, 128); EXPECT_EQ(book->height, 80);
    EXPECT_EQ(snapshot_pixel(book, 10, 35), 0xff808080u);
    EXPECT_EQ(snapshot_pixel(book, 78, 35), 0xffff0000u);
    save_surface_to_png(book, "temp/paged-media-instances/book.png"); image_surface_destroy(book);
    ASSERT_TRUE(render_secondary_view_to_svg(instance, "temp/paged-media-instances/book.svg"));
    ASSERT_TRUE(render_secondary_view_to_pdf(instance, "temp/paged-media-instances/all.pdf"));
    ASSERT_TRUE(render_secondary_view_to_pdf(instance, "temp/paged-media-instances/selected.pdf", &selection));
    options = view_preview_options_default(); options.rows = options.columns = 2; options.scale = 0.5f;
    ViewTree* grid = view_tree_page_instances_create(instance, nullptr, &options); ASSERT_NE(grid, nullptr);
    ASSERT_TRUE(render_secondary_view_to_png(grid, "temp/paged-media-instances/grid.png"));
    EXPECT_EQ(instance->model->placement_count, 1u); EXPECT_EQ(grid->model->placement_count, 5u);
    view_tree_secondary_release_all(&doc);
    EXPECT_EQ(doc.secondary_view_trees, nullptr);
}

TEST_F(SecondaryViewTest, DocumentCleanupReleasesRegisteredInstancesAndRetiredGenerations) {
    uint32_t baseline = mem_context_live_count((MemContext*)doc.services.mem_ctx);
    DomDocument other; ASSERT_TRUE(other.init(input));
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED);
    ViewTree* original = view_tree_secondary_create(&other, &environment); ASSERT_NE(original, nullptr);
    pages(original, 3);
    ViewPreviewOptions options = view_preview_options_default();
    ViewTree* first = view_tree_page_instances_create(original, nullptr, &options); ASSERT_NE(first, nullptr);
    original->reset_retained(); pages(original, 2);
    ViewTree* second = view_tree_page_instances_create(original, nullptr, &options); ASSERT_NE(second, nullptr);
    ASSERT_NE(view_tree_page_instances_create(first, nullptr, &options), nullptr);
    // document-pool cleanup is also responsible for generations no longer in its view registry.
    other.destroy();
    EXPECT_EQ(other.secondary_view_trees, nullptr);
    EXPECT_EQ(mem_context_live_count((MemContext*)doc.services.mem_ctx), baseline);
}

TEST_F(SecondaryViewTest, PhysicalPageSnapshotsIgnoreThePreviewFilterAndPreserveGlyphPlacement) {
    preview_document();
    ViewTree* tree = secondary();
    PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &layout, nullptr), TYPESET_OK);
    ViewPageRange range = {3, 4}; ViewPageSelection selection = {false, &range, 1};
    ViewPreviewOptions options = view_preview_options_default();
    options.arrangement = VIEW_PAGES_BOOK; options.scale = 0.25f; options.padding = 20.0f;
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    LayoutViewRef page_ref = tree->model->pages.get()[0]->node.ref;
    uint32_t presentation = tree->model->presentation_generation;
    ImageSurface* surface = render_secondary_page_snapshot(tree, 1, 0.5f);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->width, 60); EXPECT_EQ(surface->height, 80);
    EXPECT_EQ(snapshot_pixel(surface, 10, 35), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(surface, 2, 2), 0xffffffffu);
    size_t ink = 0;
    for (size_t y = 7; y < 18; y++) for (size_t x = 7; x < 35; x++) {
        Color pixel = {snapshot_pixel(surface, x, y)};
        if (pixel.r < 100 && pixel.g < 100 && pixel.b < 100) ink++;
    }
    EXPECT_GT(ink, 3u);
    save_surface_to_png(surface, "temp/paged-media-impl/thumbnail-page-one.png");
    image_surface_destroy(surface);
    EXPECT_EQ(view_tree_node_resolve(tree, page_ref), &tree->model->pages.get()[0]->node);
    EXPECT_EQ(tree->model->presentation_generation, presentation);
    EXPECT_EQ(tree->model->page_count, 5u);
    EXPECT_EQ(render_secondary_page_snapshot(tree, 0), nullptr);
    EXPECT_EQ(render_secondary_page_snapshot(tree, 6), nullptr);
    EXPECT_EQ(render_secondary_page_snapshot(tree, 1, NAN), nullptr);
    EXPECT_EQ(render_secondary_page_snapshot(tree, 1, -1.0f), nullptr);
    doc.mutation_epoch++;
    EXPECT_EQ(render_secondary_page_snapshot(tree, 1), nullptr);
}

TEST_F(SecondaryViewTest, IndependentThumbnailCachesSurvivePresentationChangesAndSourceTreeRelease) {
    preview_document();
    ViewTree* tree = secondary();
    PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &layout, nullptr), TYPESET_OK);
    PageSnapshotCacheScope small = {render_page_snapshot_cache_create(4, 1024 * 1024)};
    PageSnapshotCacheScope large = {render_page_snapshot_cache_create(4, 1024 * 1024)};
    ASSERT_NE(small.cache, nullptr); ASSERT_NE(large.cache, nullptr);
    const ImageSurface* thumbnail = render_page_snapshot_cache_get(small.cache, tree, 1, 0.5f);
    const ImageSurface* full = render_page_snapshot_cache_get(large.cache, tree, 1);
    ASSERT_NE(thumbnail, nullptr); ASSERT_NE(full, nullptr);
    EXPECT_NE(thumbnail, full); EXPECT_EQ(thumbnail->width, 60); EXPECT_EQ(full->width, 120);
    LayoutViewRef page_ref = tree->model->pages.get()[0]->node.ref;
    DomElement* first = (DomElement*)source->first_child.get();
    LayoutViewNode* glyph = source_fragment(tree, first->first_child, VIEW_FRAGMENT_BODY, false);
    ASSERT_NE(glyph, nullptr); PaintGlyphRun* run = glyph->glyph_run;
    ViewPageRange range = {3, 4}; ViewPageSelection selection = {false, &range, 1};
    ViewPreviewOptions options = view_preview_options_default();
    options.arrangement = VIEW_PAGES_BOOK; options.scale = 0.25f;
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    EXPECT_EQ(render_page_snapshot_cache_get(small.cache, tree, 1, 0.5f), thumbnail);
    EXPECT_EQ(render_page_snapshot_cache_get(large.cache, tree, 1), full);
    EXPECT_EQ(render_page_snapshot_cache_stats(small.cache).hits, 1u);
    EXPECT_EQ(render_page_snapshot_cache_stats(large.cache).renders, 1u);
    EXPECT_EQ(view_tree_node_resolve(tree, page_ref), &tree->model->pages.get()[0]->node);
    EXPECT_EQ(glyph->glyph_run.get(), run);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    // raster copies remain valid after all source-view arenas and font handles have been released.
    EXPECT_EQ(snapshot_pixel(thumbnail, 10, 35), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(full, 20, 70), 0xff0000ffu);
    EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, ThumbnailCachesBoundBytesAndEntriesAndEvictTheLeastRecentlyUsedPage) {
    preview_document();
    ViewTree* tree = secondary();
    PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &layout, nullptr), TYPESET_OK);
    size_t page_bytes = 0; ASSERT_TRUE(render_surface_allocation_size(60.0f, 80.0f, &page_bytes));
    PageSnapshotCacheScope cache = {render_page_snapshot_cache_create(2, page_bytes * 2)};
    ASSERT_NE(cache.cache, nullptr);
    ASSERT_NE(render_page_snapshot_cache_get(cache.cache, tree, 1, 0.5f), nullptr);
    const ImageSurface* second = render_page_snapshot_cache_get(cache.cache, tree, 2, 0.5f);
    ASSERT_NE(second, nullptr); auto second_handle = second->self;
    ASSERT_NE(render_page_snapshot_cache_get(cache.cache, tree, 1, 0.5f), nullptr);
    ASSERT_NE(render_page_snapshot_cache_get(cache.cache, tree, 3, 0.5f), nullptr);
    EXPECT_EQ(image_surface_lookup(second_handle), nullptr);
    RenderPageSnapshotCacheStats stats = render_page_snapshot_cache_stats(cache.cache);
    EXPECT_EQ(stats.entries, 2u); EXPECT_EQ(stats.bytes, page_bytes * 2);
    EXPECT_EQ(stats.renders, 3u); EXPECT_EQ(stats.hits, 1u); EXPECT_EQ(stats.evictions, 1u);
    EXPECT_EQ(render_page_snapshot_cache_get(cache.cache, tree, 1), nullptr);
    EXPECT_EQ(render_page_snapshot_cache_get(cache.cache, tree, 1, INFINITY), nullptr);
    EXPECT_EQ(render_page_snapshot_cache_stats(cache.cache).renders, 3u);
    PageSnapshotCacheScope byte_cache = {render_page_snapshot_cache_create(4, page_bytes)};
    ASSERT_NE(byte_cache.cache, nullptr);
    ASSERT_NE(render_page_snapshot_cache_get(byte_cache.cache, tree, 1, 0.5f), nullptr);
    ASSERT_NE(render_page_snapshot_cache_get(byte_cache.cache, tree, 2, 0.5f), nullptr);
    stats = render_page_snapshot_cache_stats(byte_cache.cache);
    EXPECT_EQ(stats.entries, 1u); EXPECT_EQ(stats.bytes, page_bytes); EXPECT_EQ(stats.evictions, 1u);
    EXPECT_EQ(render_page_snapshot_cache_create(0, page_bytes), nullptr);
    EXPECT_EQ(render_page_snapshot_cache_create(SIZE_MAX, page_bytes), nullptr);
}

TEST_F(SecondaryViewTest, ThumbnailCachesRejectStaleLayoutsAndRebindAfterRecomposition) {
    preview_document();
    ViewTree* tree = secondary();
    PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &layout, nullptr), TYPESET_OK);
    PageSnapshotCacheScope cache = {render_page_snapshot_cache_create(4, 1024 * 1024)};
    ASSERT_NE(cache.cache, nullptr);
    const ImageSurface* first = render_page_snapshot_cache_get(cache.cache, tree, 1, 0.5f);
    ASSERT_NE(first, nullptr); auto old_handle = first->self;
    tree->model->environment.resource_generation++;
    ASSERT_NE(render_page_snapshot_cache_get(cache.cache, tree, 1, 0.5f), nullptr);
    EXPECT_EQ(image_surface_lookup(old_handle), nullptr);
    EXPECT_EQ(render_page_snapshot_cache_stats(cache.cache).renders, 2u);
    tree->reset_retained();
    EXPECT_EQ(render_page_snapshot_cache_get(cache.cache, tree, 1, 0.5f), nullptr);
    EXPECT_EQ(render_page_snapshot_cache_stats(cache.cache).entries, 0u);
    ASSERT_EQ(layout_secondary_view(tree, &layout, nullptr), TYPESET_OK);
    ASSERT_NE(render_page_snapshot_cache_get(cache.cache, tree, 1, 0.5f), nullptr);
    EXPECT_EQ(render_page_snapshot_cache_stats(cache.cache).renders, 3u);
    doc.mutation_epoch++;
    EXPECT_EQ(render_page_snapshot_cache_get(cache.cache, tree, 1, 0.5f), nullptr);
    EXPECT_EQ(render_page_snapshot_cache_stats(cache.cache).entries, 0u);
    render_page_snapshot_cache_clear(cache.cache);
}

TEST_F(SecondaryViewTest, GridSnapshotsPaintPagesAndFurnitureWithoutRepagination) {
    preview_document();
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
    PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &layout, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 5u);
    ViewPreviewOptions options = view_preview_options_default();
    options.rows = options.columns = 2; options.scale = 0.5f; options.padding = 10.0f;
    options.column_gap = 12.0f; options.row_gap = 14.0f; options.group_gap = 20.0f;
    ASSERT_EQ(view_tree_preview_arrange(tree, nullptr, &options), VIEW_MODEL_OK);
    LayoutViewRef page_ref = tree->model->pages.get()[0]->node.ref;
    DomElement* first = (DomElement*)source->first_child.get();
    LayoutViewNode* glyph = source_fragment(tree, first->first_child, VIEW_FRAGMENT_BODY, false);
    ASSERT_NE(glyph, nullptr); ASSERT_NE(glyph->glyph_run, nullptr);
    PaintGlyphRun* run = glyph->glyph_run;
    uint32_t generation = tree->model->presentation_generation;
    ImageSurface* surface = render_secondary_view_snapshot(tree, 2.0f);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->width, 304); EXPECT_EQ(surface->height, 588);
    EXPECT_EQ(surface->alpha_mode, IMAGE_ALPHA_STRAIGHT);
    EXPECT_EQ(snapshot_pixel(surface, 50, 90), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(surface, 194, 90), 0xff00ff00u);
    EXPECT_EQ(snapshot_pixel(surface, 50, 278), 0xffff0000u);
    EXPECT_EQ(snapshot_pixel(surface, 194, 278), 0xff0080ffu);
    EXPECT_EQ(snapshot_pixel(surface, 50, 478), 0xffff0080u);
    EXPECT_EQ(snapshot_pixel(surface, 150, 50), 0xffe8e8e8u);
    EXPECT_EQ(snapshot_pixel(surface, 25, 25), 0xffffffffu);
    size_t body_ink = 0, footer_ink = 0;
    for (size_t y = 40; y < 60; y++) for (size_t x = 40; x < 100; x++) {
        Color pixel = {snapshot_pixel(surface, x, y)};
        if (pixel.r < 50 && pixel.g < 50 && pixel.b < 50) body_ink++;
    }
    for (size_t y = 158; y < 180; y++) for (size_t x = 65; x < 95; x++) {
        Color pixel = {snapshot_pixel(surface, x, y)};
        if (pixel.r < 50 && pixel.g < 50 && pixel.b < 50) footer_ink++;
    }
    EXPECT_GT(body_ink, 15u); EXPECT_GT(footer_ink, 3u);
    image_surface_destroy(surface);
    ASSERT_TRUE(render_secondary_view_to_png(tree, "temp/paged-media-impl/preview-grid.png", 2.0f));
    EXPECT_EQ(tree->model->presentation_generation, generation);
    EXPECT_EQ(view_tree_node_resolve(tree, page_ref), &tree->model->pages.get()[0]->node);
    EXPECT_EQ(glyph->glyph_run.get(), run);
    EXPECT_FLOAT_EQ(source->width, 640.0f); EXPECT_FLOAT_EQ(source->x, 11.25f);
    ASSERT_TRUE(render_secondary_view_to_pdf(tree, "temp/paged-media-impl/preview-grid-physical.pdf"));
    char* pdf = read_text_file("temp/paged-media-impl/preview-grid-physical.pdf"); ASSERT_NE(pdf, nullptr);
    EXPECT_NE(strstr(pdf, "/Count 5"), nullptr);
    EXPECT_NE(strstr(pdf, "/MediaBox [0 0 90.00 120.00]"), nullptr); free(pdf);
    options.groups_horizontal = options.column_major = true;
    ASSERT_EQ(view_tree_preview_arrange(tree, nullptr, &options), VIEW_MODEL_OK);
    surface = render_secondary_view_snapshot(tree);
    ASSERT_NE(surface, nullptr); EXPECT_EQ(surface->width, 232); EXPECT_EQ(surface->height, 194);
    EXPECT_EQ(snapshot_pixel(surface, 25, 139), 0xff00ff00u);
    EXPECT_EQ(snapshot_pixel(surface, 97, 45), 0xffff0000u);
    EXPECT_EQ(snapshot_pixel(surface, 177, 45), 0xffff0080u);
    image_surface_destroy(surface);
    ASSERT_TRUE(render_secondary_view_to_png(tree, "temp/paged-media-impl/preview-grid-horizontal.png"));
    ViewPageRange range = {2, 3}; ViewPageSelection selection = {false, &range, 1};
    options.rows = options.columns = 1; options.scale = 1.0f; options.groups_horizontal = false;
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    surface = render_secondary_view_snapshot(tree);
    ASSERT_NE(surface, nullptr); EXPECT_EQ(surface->width, 140); EXPECT_EQ(surface->height, 360);
    EXPECT_EQ(snapshot_pixel(surface, 40, 70), 0xff00ff00u);
    EXPECT_EQ(snapshot_pixel(surface, 40, 250), 0xffff0000u);
    image_surface_destroy(surface);
    ASSERT_TRUE(render_secondary_view_to_png(tree, "temp/paged-media-impl/preview-filtered-column.png"));
    EXPECT_EQ(view_tree_node_resolve(tree, page_ref), &tree->model->pages.get()[0]->node);
    EXPECT_EQ(glyph->glyph_run.get(), run);
}

TEST_F(SecondaryViewTest, FilteredFacingSnapshotsRetainEmptyPartnersAndBinding) {
    preview_document();
    ViewTree* tree = secondary(); PagedLayoutDiagnostic diagnostic = {};
    PagedLayoutOptions layout = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &layout, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewPageRange range = {3, 4}; ViewPageSelection selection = {false, &range, 1};
    ViewPreviewOptions options = view_preview_options_default();
    options.arrangement = VIEW_PAGES_BOOK; options.padding = 8.0f; options.column_gap = 12.0f;
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    ImageSurface* surface = render_secondary_view_snapshot(tree);
    ASSERT_NE(surface, nullptr); EXPECT_EQ(surface->width, 268); EXPECT_EQ(surface->height, 176);
    EXPECT_EQ(snapshot_pixel(surface, 40, 70), 0xffe8e8e8u);
    EXPECT_EQ(snapshot_pixel(surface, 170, 70), 0xffff0000u);
    image_surface_destroy(surface);
    ASSERT_TRUE(render_secondary_view_to_png(tree, "temp/paged-media-impl/preview-book-filtered.png"));
    surface = render_secondary_view_snapshot(tree, 1.0f, Color{0}); ASSERT_NE(surface, nullptr);
    EXPECT_EQ(snapshot_pixel(surface, 40, 70), 0u);
    EXPECT_EQ(snapshot_pixel(surface, 145, 15), 0xffffffffu);
    image_surface_destroy(surface);
    options.anchor_page = 4;
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    surface = render_secondary_view_snapshot(tree);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(snapshot_pixel(surface, 40, 70), 0xff0080ffu);
    EXPECT_EQ(snapshot_pixel(surface, 170, 70), 0xffe8e8e8u);
    image_surface_destroy(surface);
    ASSERT_TRUE(render_secondary_view_to_png(tree, "temp/paged-media-impl/preview-book-next.png"));
    options.anchor_page = 2; options.right_binding = true;
    ASSERT_EQ(view_tree_preview_arrange(tree, nullptr, &options), VIEW_MODEL_OK);
    surface = render_secondary_view_snapshot(tree, 0.5f);
    ASSERT_NE(surface, nullptr);
    // binding changes spread grouping while the composed pages retain their physical left/right styles.
    EXPECT_EQ(tree->model->pages.get()[0]->side, VIEW_PAGE_RIGHT);
    EXPECT_EQ(tree->model->pages.get()[1]->side, VIEW_PAGE_LEFT);
    EXPECT_EQ(snapshot_pixel(surface, 20, 35), 0xff00ff00u);
    EXPECT_EQ(snapshot_pixel(surface, 85, 35), 0xff0000ffu);
    image_surface_destroy(surface);
    ASSERT_TRUE(render_secondary_view_to_png(tree, "temp/paged-media-impl/preview-book-right-binding.png", 0.5f));
    EXPECT_EQ(tree->model->page_count, 5u);
}

TEST_F(SecondaryViewTest, SnapshotsRejectInvalidDensityAndStaleOrUncommittedViews) {
    ViewTree* tree = secondary();
    EXPECT_EQ(render_secondary_view_snapshot(tree), nullptr);
    pages(tree, 1);
    ViewPreviewOptions options = view_preview_options_default();
    ASSERT_EQ(view_tree_preview_arrange(tree, nullptr, &options), VIEW_MODEL_OK);
    EXPECT_EQ(render_secondary_view_snapshot(tree, 0.0f), nullptr);
    EXPECT_EQ(render_secondary_view_snapshot(tree, -1.0f), nullptr);
    EXPECT_EQ(render_secondary_view_snapshot(tree, NAN), nullptr);
    EXPECT_EQ(render_secondary_view_snapshot(tree, INFINITY), nullptr);
    EXPECT_EQ(render_secondary_view_snapshot(tree, 1.0e30f), nullptr);
    EXPECT_FALSE(render_secondary_view_to_png(tree, nullptr));
    EXPECT_FALSE(render_secondary_view_to_svg(tree, nullptr));
    EXPECT_FALSE(render_secondary_view_to_svg(tree, "temp/paged-media-impl/invalid-preview.svg", 0.0f));
    EXPECT_FALSE(render_secondary_view_to_svg(tree, "temp/paged-media-impl/invalid-preview.svg", INFINITY));
    ViewPageSelection empty = {false, nullptr, 0}; options.padding = 0.0f;
    ASSERT_EQ(view_tree_preview_arrange(tree, &empty, &options), VIEW_MODEL_OK);
    EXPECT_EQ(render_secondary_view_snapshot(tree), nullptr);
    doc.mutation_epoch++;
    EXPECT_EQ(render_secondary_view_snapshot(tree), nullptr);
    EXPECT_FALSE(render_secondary_view_to_png(tree, "temp/paged-media-impl/stale-preview.png"));
    EXPECT_FALSE(render_secondary_view_to_svg(tree, "temp/paged-media-impl/stale-preview.svg"));
}

TEST_F(SecondaryViewTest, SelectionMergesAndPreservesPhysicalOrder) {
    ViewTree* tree = secondary();
    pages(tree, 6);
    ViewPageRange ranges[] = {{4, 5}, {2, 4}, {4, 4}};
    ViewPageSelection selection = {false, ranges, 3};
    ViewPreviewOptions options = view_preview_options_default();
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    ASSERT_EQ(tree->model->placement_count, 4u);
    for (size_t i = 0; i < 4; i++) EXPECT_EQ(tree->model->placements.get()[i].page_number, i + 2);
    uint32_t generation = tree->model->presentation_generation;
    ranges[0] = {6, 7};
    EXPECT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_INVALID_PAGE_RANGE);
    EXPECT_EQ(tree->model->presentation_generation, generation);
    EXPECT_EQ(tree->model->placement_count, 4u);
    ViewPageSelection empty = {false, nullptr, 0};
    EXPECT_EQ(view_tree_preview_arrange(tree, &empty, &options), VIEW_MODEL_OK);
    EXPECT_EQ(tree->model->placement_count, 0u);
    EXPECT_EQ(tree->model->page_count, 6u);
}

TEST_F(SecondaryViewTest, BookFiltersPreserveOriginalFacingPartners) {
    ViewTree* tree = secondary();
    pages(tree, 6);
    ViewPreviewOptions options = view_preview_options_default();
    options.arrangement = VIEW_PAGES_BOOK;
    options.column_gap = 10.0f;
    ASSERT_EQ(view_tree_preview_arrange(tree, nullptr, &options), VIEW_MODEL_OK);
    ASSERT_EQ(tree->model->placement_count, 1u);
    EXPECT_EQ(tree->model->placements.get()[0].page_number, 1u);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[0].rect.x, 110.0f);
    ViewPageRange range = {3, 4};
    ViewPageSelection selection = {false, &range, 1};
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    ASSERT_EQ(tree->model->placement_count, 1u);
    EXPECT_EQ(tree->model->placements.get()[0].page_number, 3u);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[0].rect.x, 110.0f);
    options.anchor_page = 4;
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    EXPECT_EQ(tree->model->placements.get()[0].page_number, 4u);
    EXPECT_FLOAT_EQ(tree->model->placements.get()[0].rect.x, 0.0f);
    options.anchor_page = 6;
    EXPECT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_INVALID_PAGE_RANGE);
}

struct PaintedPages {
    size_t count;
    uint32_t numbers[8];
    bool had_preview_transform;
};

static bool capture_page(ViewTree*, const ViewPageBox* page,
                         const ViewPagePlacement* placement, void* data) {
    PaintedPages* capture = (PaintedPages*)data;
    if (capture->count == 8) return false;
    capture->numbers[capture->count++] = page->page_number;
    capture->had_preview_transform |= placement != nullptr;
    return true;
}

TEST_F(SecondaryViewTest, PaintCullingAndPhysicalExportUseDifferentIndexes) {
    ViewTree* tree = secondary();
    pages(tree, 5);
    ViewPreviewOptions options = view_preview_options_default();
    ViewPageRange range = {2, 3};
    ViewPageSelection selection = {false, &range, 1};
    ASSERT_EQ(view_tree_preview_arrange(tree, &selection, &options), VIEW_MODEL_OK);
    PaintedPages preview = {};
    RdtLogicalRect clip = {0.0f, 200.0f, 100.0f, 200.0f};
    ASSERT_EQ(view_tree_preview_paint(tree, &clip, capture_page, &preview), VIEW_MODEL_OK);
    EXPECT_EQ(preview.count, 1u);
    EXPECT_EQ(preview.numbers[0], 3u);
    EXPECT_TRUE(preview.had_preview_transform);
    PaintedPages output = {};
    ASSERT_EQ(view_tree_pages_visit(tree, nullptr, capture_page, &output), VIEW_MODEL_OK);
    EXPECT_EQ(output.count, 5u);
    EXPECT_FALSE(output.had_preview_transform);
    EXPECT_EQ(output.numbers[0], 1u);
    EXPECT_EQ(output.numbers[4], 5u);
}

TEST_F(SecondaryViewTest, InvalidGeometryAndForeignSourceAreRejected) {
    ViewEnvironment environment = view_environment_default(VIEW_PRESENTATION_PAGED);
    environment.device_scale = NAN;
    EXPECT_EQ(view_tree_secondary_create(&doc, &environment), nullptr);
    ViewTree* tree = secondary();
    EXPECT_EQ(view_tree_page_append(tree, 100.0f, 200.0f,
        {20.0f, 0.0f, 100.0f, 200.0f}, VIEW_PAGE_RIGHT), nullptr);
    EXPECT_EQ(tree->model->page_count, 0u);
    DomNode foreign = {};
    EXPECT_EQ(view_tree_node_state(tree, &foreign, true), nullptr);
    EXPECT_EQ(view_tree_fragment_append(tree, tree->model->root, source,
        {0.0f, 0.0f, INFINITY, 20.0f}), nullptr);
}

TEST_F(SecondaryViewTest, NativeSequencesKeepPhysicalIndexesSeparateFromFolioResetsAndEndPadding) {
    stylesheet("p, div { margin:0; font:10px/12px Arial }");
    ASSERT_NE(page_master("sheet", "size:160px 100px; margin:10px"), nullptr);
    DomElement* first = page_sequence("sheet", "7", "even"); ASSERT_NE(first, nullptr);
    DomElement* a = block("First", nullptr, "div", first); ASSERT_NE(a, nullptr);
    DomElement* second = page_sequence("sheet", "2", "end-on-odd"); ASSERT_NE(second, nullptr);
    DomElement* b = block("Second", nullptr, "div", second); ASSERT_NE(b, nullptr);
    DomElement* third = page_sequence("sheet", "auto-odd"); ASSERT_NE(third, nullptr);
    DomElement* c = block("Third", nullptr, "div", third); ASSERT_NE(c, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 5u);
    const uint32_t folios[] = {7, 8, 2, 3, 5};
    const uint32_t ordinals[] = {1, 2, 1, 2, 1};
    for (size_t i = 0; i < 5; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        EXPECT_EQ(page->page_number, i + 1); EXPECT_EQ(page->sequence_page, ordinals[i]); EXPECT_EQ(page->folio, folios[i]);
        EXPECT_EQ(page->blank, i == 1 || i == 3);
        EXPECT_EQ(page->side, i % 2 ? VIEW_PAGE_LEFT : VIEW_PAGE_RIGHT);
    }
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, a->first_child, false)->first_occurrence), 1u);
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, b->first_child, false)->first_occurrence), 3u);
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, c->first_child, false)->first_occurrence), 5u);
    ViewPageSelection selection = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &selection, &preview_options); ASSERT_NE(preview, nullptr);
    EXPECT_EQ(preview->model->pages.get()[2]->folio, 2u); EXPECT_EQ(preview->model->pages.get()[2]->sequence_page, 1u);
}

TEST_F(SecondaryViewTest, NativeFolioFormatsRetainUnicodeDigitsGroupingAndPreviewLabels) {
    stylesheet("div { margin:0; font:10px/12px Arial }");
    ASSERT_NE(page_master("sheet", "size:200px 100px; margin:10px"), nullptr);
    struct Case { const char* format; const char* initial; const char* label; const char* separator; };
    const Case cases[] = {{"(0001)", "37", "(0037)", nullptr}, {"I", "9", "IX", nullptr},
        {"a", "27", "aa", nullptr}, {"१", "42", "४२", nullptr}, {"๐๐๑", "7", "๐๐๗", nullptr},
        {"α", "37", "37", nullptr}, {"1.1.", "37", "37.", nullptr}, {"1", "1234567", "1 234 567", " "}};
    for (const Case& example : cases) {
        DomElement* sequence = page_sequence("sheet", example.initial); ASSERT_NE(sequence, nullptr);
        ASSERT_TRUE(sequence->set_attribute("format", example.format));
        if (example.separator) {
            ASSERT_TRUE(sequence->set_attribute("grouping-separator", example.separator));
            ASSERT_TRUE(sequence->set_attribute("grouping-size", "3"));
        }
        ASSERT_NE(block("Content", nullptr, "div", sequence), nullptr);
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, sizeof(cases) / sizeof(cases[0]));
    for (size_t i = 0; i < tree->model->page_count; i++) EXPECT_STREQ(tree->model->pages.get()[i]->label, cases[i].label) << i;
    ViewPageSelection selection = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &selection, &preview_options); ASSERT_NE(preview, nullptr);
    EXPECT_STREQ(preview->model->pages.get()[0]->label, "(0037)");
}

TEST_F(SecondaryViewTest, NativeFolioQueriesUseTargetFormattingAndFirstLastAreaStrategies) {
    stylesheet("div { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    ASSERT_NE(page_master("sheet", "size:300px 100px; margin:10px"), nullptr);
    DomElement* preface = page_sequence("sheet", "4", "even"); ASSERT_NE(preface, nullptr);
    ASSERT_TRUE(preface->set_attribute("format", "i"));
    DomElement* line = block(nullptr, nullptr, "div", preface); ASSERT_NE(line, nullptr);
    DomElement* current = page_control("r:folio", line); ASSERT_NE(current, nullptr);
    DomElement* first = page_control("r:folio-ref", line); ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->set_attribute("ref-id", "content"));
    DomElement* last = page_control("r:folio-ref", line); ASSERT_NE(last, nullptr);
    ASSERT_TRUE(last->set_attribute("ref-id", "content")); ASSERT_TRUE(last->set_attribute("edge", "last"));
    DomElement* all = page_control("r:folio-ref", line); ASSERT_NE(all, nullptr);
    ASSERT_TRUE(all->set_attribute("ref-id", "chapter")); ASSERT_TRUE(all->set_attribute("edge", "last"));
    DomElement* normal = page_control("r:folio-ref", line); ASSERT_NE(normal, nullptr);
    ASSERT_TRUE(normal->set_attribute("ref-id", "chapter")); ASSERT_TRUE(normal->set_attribute("edge", "last"));
    ASSERT_TRUE(normal->set_attribute("area", "normal"));
    DomElement* chapter = page_sequence("sheet", "12", "even"); ASSERT_NE(chapter, nullptr);
    ASSERT_TRUE(chapter->set_attribute("format", "(001)")); ASSERT_TRUE(chapter->set_attribute("id", "chapter"));
    DomElement* content = block(nullptr, nullptr, "div", chapter); ASSERT_NE(content, nullptr);
    ASSERT_TRUE(content->set_attribute("id", "content"));
    for (size_t i = 0; i < 3; i++) ASSERT_NE(block("Text", i ? "break-before:page" : nullptr, "div", content), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 6u); EXPECT_GE(diagnostic.reference_passes, 2u);
    const char* labels[] = {"iv", "(012)", "(014)", "(015)", "(014)"};
    DomElement* queries[] = {current, first, last, all, normal};
    for (size_t i = 0; i < 5; i++) {
        LayoutViewNode* fragment = source_fragment(tree, queries[i], VIEW_FRAGMENT_BODY, false); ASSERT_NE(fragment, nullptr) << i;
        ASSERT_NE(fragment->glyph_run, nullptr); EXPECT_STREQ(fragment->glyph_run->text, labels[i]) << i;
    }
    const TypesetTarget* target = layout_secondary_target(tree, "content"); ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->page_number, 3u); EXPECT_EQ(target->last_page_number, 5u);
}

TEST_F(SecondaryViewTest, NativeFolioQueriesDiagnoseUnplacedTargetsAndInvalidFormats) {
    ASSERT_NE(page_master("sheet", "size:200px 100px; margin:10px"), nullptr);
    DomElement* sequence = page_sequence("sheet"); ASSERT_NE(sequence, nullptr);
    DomElement* line = block(nullptr, nullptr, "div", sequence); ASSERT_NE(line, nullptr);
    DomElement* query = page_control("r:folio-ref", line); ASSERT_NE(query, nullptr); ASSERT_TRUE(query->set_attribute("ref-id", "missing"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, query);
    EXPECT_EQ(tree->model->page_count, 0u); ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    ASSERT_TRUE(sequence->set_attribute("grouping-separator", "ab")); ASSERT_TRUE(sequence->set_attribute("grouping-size", "3"));
    tree = secondary(); EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(diagnostic.source.address, sequence); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, CurrentFoliosMeasureEachRepeatedOccurrenceAndKeepGeneratedTextAfterReset) {
    stylesheet("@page sheet { @top-center { content:element(Header) } } div { margin:0; font:10px/12px Arial }");
    ASSERT_NE(page_master("sheet", "size:200px 100px; margin:20px"), nullptr);
    DomElement* sequence = page_sequence("sheet", "9"); ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(sequence->set_attribute("format", "(001)"));
    DomElement* running = block(nullptr, "position:running(Header)", "div", sequence); ASSERT_NE(running, nullptr);
    ASSERT_NE(page_control("r:folio", running), nullptr);
    for (size_t i = 0; i < 3; i++) ASSERT_NE(block("Body", i ? "break-before:page" : nullptr, "div", sequence), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    const char* labels[] = {"(009)", "(010)", "(011)"};
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    for (size_t i = 0; i < 3; i++) {
        strbuf_reset(text); append_fragment_text(tree->model->pages.get()[i]->margin_boxes[CSS_PAGE_TOP_CENTER], text);
        EXPECT_STREQ(text->str, labels[i]) << i;
    }
    strbuf_free(text);
    ViewPageSelection all = {true, nullptr, 0}; ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, &all, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(sequence->set_attribute("format", "I")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_STREQ(tree->model->pages.get()[0]->label, "IX");
    EXPECT_STREQ(preview->model->pages.get()[0]->label, "(009)");
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    const ViewPageBox* retained = view_tree_page_material(preview, preview->model->pages.get()[2]); ASSERT_NE(retained, nullptr);
    text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(retained->margin_boxes[CSS_PAGE_TOP_CENTER], text);
    EXPECT_STREQ(text->str, "(011)"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, NativeFolioQueriesUseFinalCssCountersAndOwnTextWithoutTargets) {
    stylesheet("@page { size:200px 100px; margin:10px; counter-increment:page 0; counter-reset:page 99 } div { margin:0; font:10px/12px Arial }");
    DomElement* first = block(nullptr); ASSERT_NE(first, nullptr);
    DomElement* a = page_control("r:folio", first); ASSERT_NE(a, nullptr);
    DomElement* second = block(nullptr, "break-before:page"); ASSERT_NE(second, nullptr);
    DomElement* b = page_control("r:folio", second); ASSERT_NE(b, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_GE(diagnostic.reference_passes, 3u);
    for (DomElement* query : {a, b}) {
        LayoutViewNode* fragment = source_fragment(tree, query, VIEW_FRAGMENT_BODY, false); ASSERT_NE(fragment, nullptr);
        ASSERT_NE(fragment->glyph_run, nullptr); EXPECT_STREQ(fragment->glyph_run->text, "99");
    }
    EXPECT_STREQ(tree->model->pages.get()[0]->label, "99"); EXPECT_STREQ(tree->model->pages.get()[1]->label, "99");
    ViewTree* continuous = secondary(VIEW_PRESENTATION_CONTINUOUS);
    EXPECT_EQ(layout_secondary_view(continuous, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "folio queries require paged presentation");
}

TEST_F(SecondaryViewTest, NativeEdgeRegionsApplyPrecedenceAndRepeatStaticContentOnBlankPages) {
    stylesheet("div { margin:0; font:10px/12px Arial }");
    DomElement* master = page_master("sheet", "size:240px 180px; margin:10px"); ASSERT_NE(master, nullptr);
    ASSERT_NE(page_region(master, "body", "main", nullptr, "margin:30px 20px 30px 30px"), nullptr);
    DomElement* before = page_region(master, "before", "head", "20px", "background-color:blue; overflow:hidden"); ASSERT_NE(before, nullptr);
    ASSERT_TRUE(before->set_attribute("precedence", "true"));
    DomElement* after = page_region(master, "after", "foot", "15px", "background-color:yellow"); ASSERT_NE(after, nullptr);
    ASSERT_TRUE(after->set_attribute("display-align", "after"));
    ASSERT_NE(page_region(master, "start", "side", "30px", "background-color:red; overflow:hidden"), nullptr);
    ASSERT_NE(page_region(master, "end", "end", "20px", "background-color:green"), nullptr);
    DomElement* sequence = page_sequence("sheet", "9", "even"); ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(sequence->set_attribute("format", "I"));
    DomElement* header = static_content(sequence, "head"); ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->set_attribute("id", "HeaderContent"));
    DomElement* header_line = block(nullptr, nullptr, "div", header); ASSERT_NE(header_line, nullptr);
    ASSERT_NE(page_control("r:folio", header_line), nullptr);
    ASSERT_NE(static_content(sequence, "foot", "Footer"), nullptr);
    ASSERT_NE(static_content(sequence, "side", "Sidebar"), nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr); ASSERT_TRUE(flow->set_attribute("region-name", "main"));
    DomElement* body = block("Body", nullptr, "div", flow); ASSERT_NE(body, nullptr);
    DomElement* citation = page_control("r:folio-ref", body); ASSERT_NE(citation, nullptr);
    ASSERT_TRUE(citation->set_attribute("ref-id", "HeaderContent")); ASSERT_TRUE(citation->set_attribute("edge", "last"));
    ASSERT_TRUE(citation->set_attribute("area", "normal"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u); EXPECT_TRUE(tree->model->pages.get()[1]->blank);
    const ViewPageStyle* style = tree->model->pages.get()[0]->style;
    EXPECT_FLOAT_EQ(style->content_rect.x, 40); EXPECT_FLOAT_EQ(style->content_rect.y, 40);
    EXPECT_FLOAT_EQ(style->content_rect.width, 170); EXPECT_FLOAT_EQ(style->content_rect.height, 100);
    EXPECT_FLOAT_EQ(style->edge_rects[0].x, 10); EXPECT_FLOAT_EQ(style->edge_rects[0].width, 220);
    EXPECT_FLOAT_EQ(style->edge_rects[1].x, 40); EXPECT_FLOAT_EQ(style->edge_rects[1].y, 155); EXPECT_FLOAT_EQ(style->edge_rects[1].width, 170);
    EXPECT_FLOAT_EQ(style->edge_rects[2].y, 30); EXPECT_FLOAT_EQ(style->edge_rects[2].height, 140);
    const char* labels[] = {"IX", "X"}; StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr);
    for (size_t i = 0; i < 2; i++) {
        const ViewPageBox* page = tree->model->pages.get()[i];
        ASSERT_NE(page->static_boxes[0], nullptr); EXPECT_TRUE(page->static_boxes[0]->clip_content);
        strbuf_reset(text); append_fragment_text(page->static_boxes[0], text); EXPECT_STREQ(text->str, labels[i]);
        EXPECT_EQ(page->static_boxes[0]->role, VIEW_FRAGMENT_STATIC);
        ASSERT_NE(page->static_boxes[1]->first_child, nullptr);
        EXPECT_FLOAT_EQ(page->static_boxes[1]->first_child->rect.y, 158);
    }
    strbuf_free(text);
    LayoutViewNode* cited = source_fragment(tree, citation, VIEW_FRAGMENT_BODY, false); ASSERT_NE(cited, nullptr);
    ASSERT_NE(cited->glyph_run, nullptr); EXPECT_STREQ(cited->glyph_run->text, "X");
    init_vector_engine();
    ImageSurface* snapshot = render_secondary_page_snapshot(tree, 2, 1.0f); ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot_pixel(snapshot, 200, 15), 0xffff0000u); // blue header owns the before/start corner.
    EXPECT_EQ(snapshot_pixel(snapshot, 15, 150), 0xff0000ffu); // start region owns the after/start corner.
    image_surface_destroy(snapshot);
    ASSERT_TRUE(before->set_attribute("style", "overflow:scroll")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "static region overflow requires visible, hidden or clip");
    EXPECT_EQ(diagnostic.source.address, before); EXPECT_EQ(tree->model->page_count, 0u);
}

TEST_F(SecondaryViewTest, NativeStaticBindingsValidateRegionNamesAndSelectedMasterGeometry) {
    DomElement* master = page_master("sheet", "size:200px 100px; margin:10px"); ASSERT_NE(master, nullptr);
    DomElement* region = page_region(master, "before", "head", "12px"); ASSERT_NE(region, nullptr);
    DomElement* sequence = page_sequence("sheet"); ASSERT_NE(sequence, nullptr);
    DomElement* binding = static_content(sequence, "head", "Header"); ASSERT_NE(binding, nullptr);
    ASSERT_NE(block("Body", nullptr, "div", sequence), nullptr);
    DomElement* duplicate = static_content(sequence, "head", "Duplicate"); ASSERT_NE(duplicate, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "duplicate sequence region binding"); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(sequence->DomNode::remove_child(duplicate)); ASSERT_TRUE(binding->set_attribute("region-name", "unknown"));
    ASSERT_TRUE(view_tree_model_reset(tree)); EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "static content references an unknown edge region");
    ASSERT_TRUE(binding->set_attribute("region-name", "head")); ASSERT_TRUE(region->set_attribute("extent", "100%"));
    ASSERT_TRUE(view_tree_model_reset(tree)); EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "logical region extent requires a length"); EXPECT_EQ(diagnostic.source.address, region);
}

TEST_F(SecondaryViewTest, NativeBodyDecorationAndOverflowConstrainOnlyBodyPaintAndRetainTheirGeometry) {
    stylesheet("p, div { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    DomElement* master = page_master("sheet", "size:180px 130px; margin:10px"); ASSERT_NE(master, nullptr);
    DomElement* body_region = page_region(master, "body", "main", nullptr,
        "margin:20px; border:2px solid blue; padding:5px; overflow:hidden"); ASSERT_NE(body_region, nullptr);
    ASSERT_NE(page_region(master, "before", "head", "10px", "background-color:green"), nullptr);
    DomElement* sequence = page_sequence("sheet"); ASSERT_NE(sequence, nullptr);
    ASSERT_NE(static_content(sequence, "head"), nullptr);
    DomElement* flow = page_control("r:flow", sequence); ASSERT_NE(flow, nullptr); ASSERT_TRUE(flow->set_attribute("region-name", "main"));
    DomElement* content = block(nullptr, "width:180px; height:12px; background-color:red", "div", flow); ASSERT_NE(content, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    const ViewPageStyle* style = tree->model->pages.get()[0]->style;
    EXPECT_FLOAT_EQ(style->body_rect.x, 30); EXPECT_FLOAT_EQ(style->body_rect.width, 120);
    EXPECT_FLOAT_EQ(style->content_rect.x, 37); EXPECT_FLOAT_EQ(style->content_rect.y, 37);
    EXPECT_FLOAT_EQ(style->content_rect.width, 106); EXPECT_FLOAT_EQ(style->content_rect.height, 56);
    EXPECT_TRUE(style->body_clip);
    LayoutViewNode* placed = source_fragment(tree, content, VIEW_FRAGMENT_BODY, true); ASSERT_NE(placed, nullptr);
    EXPECT_FLOAT_EQ(placed->rect.x, 37); EXPECT_FLOAT_EQ(placed->rect.width, 180);
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(preview, nullptr);
    init_vector_engine();
    ImageSurface* hidden = render_secondary_page_snapshot(preview, 1, 1.0f); ASSERT_NE(hidden, nullptr);
    EXPECT_EQ(snapshot_pixel(hidden, 145, 42), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(hidden, 149, 42), 0xffff0000u); // the body's border remains outside its overflow clip.
    EXPECT_EQ(snapshot_pixel(hidden, 160, 42), 0xffffffffu);
    EXPECT_EQ(snapshot_pixel(hidden, 160, 15), 0xff008000u); // body clipping cannot hide edge furniture.
    image_surface_destroy(hidden);
    ASSERT_TRUE(body_region->set_attribute("style", "margin:20px; border:2px solid blue; padding:5px; overflow:visible"));
    ASSERT_TRUE(view_tree_model_reset(tree)); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ImageSurface* visible = render_secondary_page_snapshot(tree, 1, 1.0f); ASSERT_NE(visible, nullptr);
    EXPECT_EQ(snapshot_pixel(visible, 160, 42), 0xff0000ffu); image_surface_destroy(visible);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    hidden = render_secondary_page_snapshot(preview, 1, 1.0f); ASSERT_NE(hidden, nullptr);
    EXPECT_EQ(snapshot_pixel(hidden, 160, 42), 0xffffffffu); image_surface_destroy(hidden);
    ViewTree* invalid = secondary();
    ASSERT_TRUE(body_region->set_attribute("box-policy", "zero-border-padding"));
    EXPECT_EQ(layout_secondary_view(invalid, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "region box policy requires zero border and padding");
    EXPECT_EQ(diagnostic.source.address, body_region); EXPECT_EQ(invalid->model->page_count, 0u);
    ASSERT_TRUE(body_region->remove_attribute("box-policy"));
    ASSERT_TRUE(body_region->set_attribute("style", "overflow:scroll")); ASSERT_TRUE(view_tree_model_reset(invalid));
    EXPECT_EQ(layout_secondary_view(invalid, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "body region overflow requires visible, hidden or clip"); EXPECT_EQ(diagnostic.source.address, body_region);
}

TEST_F(SecondaryViewTest, NativeStaticRegionsRemeasureDecorationsForSelectedMastersAndKeepRetainedClips) {
    stylesheet("p, div { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    DomElement* first = page_master("first", "size:180px 130px; margin:10px"); ASSERT_NE(first, nullptr);
    DomElement* later = page_master("later", "size:220px 150px; margin:10px"); ASSERT_NE(later, nullptr);
    ASSERT_NE(page_region(first, "body", "body", nullptr, "margin-top:30px"), nullptr);
    ASSERT_NE(page_region(later, "body", "body", nullptr, "margin-top:40px"), nullptr);
    DomElement* head = page_region(first, "before", "head", "20px", "border:2px solid blue; padding:3px; overflow:hidden"); ASSERT_NE(head, nullptr);
    ASSERT_NE(page_region(later, "before", "head", "30px", "border:2px solid green; padding:4px; overflow:hidden"), nullptr);
    DomElement* program = page_control("r:sequence-master"); ASSERT_NE(program, nullptr); ASSERT_TRUE(program->set_attribute("name", "book"));
    DomElement* run = page_control("r:master-run", program); ASSERT_NE(run, nullptr);
    ASSERT_NE(master_choice(run, "first", "first"), nullptr); ASSERT_NE(master_choice(run, "later"), nullptr);
    DomElement* sequence = page_sequence("book"); ASSERT_NE(sequence, nullptr);
    DomElement* binding = static_content(sequence, "head"); ASSERT_NE(binding, nullptr);
    ASSERT_NE(block(nullptr, "width:300px; height:40px; background-color:red", "div", binding), nullptr);
    ASSERT_NE(block("A", nullptr, "div", sequence), nullptr); ASSERT_NE(block("B", "break-before:page", "div", sequence), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    const ViewPageBox* a = tree->model->pages.get()[0], *b = tree->model->pages.get()[1];
    ASSERT_NE(a->static_boxes[0]->first_child, nullptr); ASSERT_NE(b->static_boxes[0]->first_child, nullptr);
    EXPECT_FLOAT_EQ(a->static_boxes[0]->first_child->rect.x, 15); EXPECT_FLOAT_EQ(a->static_boxes[0]->first_child->rect.y, 15);
    EXPECT_FLOAT_EQ(b->static_boxes[0]->first_child->rect.x, 16); EXPECT_FLOAT_EQ(b->static_boxes[0]->first_child->rect.y, 16);
    EXPECT_FLOAT_EQ(a->style->edge_rects[0].width, 160); EXPECT_FLOAT_EQ(b->style->edge_rects[0].width, 200);
    ViewPreviewOptions preview_options = view_preview_options_default();
    ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options); ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(head->set_attribute("style", "overflow:visible")); ASSERT_TRUE(view_tree_model_reset(tree));
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); init_vector_engine();
    ImageSurface* snapshot = render_secondary_page_snapshot(preview, 1, 1.0f); ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot_pixel(snapshot, 169, 20), 0xffff0000u); // static border survives content clipping.
    EXPECT_EQ(snapshot_pixel(snapshot, 160, 27), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(snapshot, 160, 35), 0xffffffffu); image_surface_destroy(snapshot);
    snapshot = render_secondary_page_snapshot(preview, 2, 1.0f); ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot_pixel(snapshot, 209, 20), 0xff008000u);
    EXPECT_EQ(snapshot_pixel(snapshot, 200, 37), 0xff0000ffu);
    EXPECT_EQ(snapshot_pixel(snapshot, 200, 45), 0xffffffffu); image_surface_destroy(snapshot);
}

TEST_F(SecondaryViewTest, FoRegionPoliciesUseCommonClippingAndDiagnoseOriginalBorderPaddingSources) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='10pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'>"
        "<f:region-body margin='15pt' overflow='visible'/><f:region-before extent='12pt'/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet'><f:static-content flow-name='xsl-region-before'><f:block>Header</f:block></f:static-content>"
        "<f:flow flow-name='xsl-region-body'><f:block>Body</f:block></f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    DomElement* master = fo->first_child_element()->first_child_element(); ASSERT_NE(master, nullptr);
    DomElement* regions[] = {master->first_child_element(), master->last_child_element()};
    RadiantFoOptions fo_options = radiant_fo_options_default(); PagedLayoutOptions options = paged_layout_options_default();
    PagedLayoutDiagnostic diagnostic = {};
    for (size_t i = 0; i < 3; i++) {
        if (i) ASSERT_TRUE(regions[i - 1]->set_attribute(i == 1 ? "padding" : "border", i == 1 ? "1pt" : "1pt solid red"));
        RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
        DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr);
        ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins)); doc.root = lam::up(generated);
        ViewTree* tree = secondary();
        TypesetStatus status = layout_secondary_view(tree, &options, &diagnostic);
        if (!i) {
            ASSERT_EQ(status, TYPESET_OK) << diagnostic.reason;
            ASSERT_EQ(tree->model->page_count, 1u);
            EXPECT_FALSE(tree->model->pages.get()[0]->style->body_clip);
            EXPECT_TRUE(tree->model->pages.get()[0]->style->edge_clip[0]); // FO auto overflow refines to a clipped viewport.
        } else {
            EXPECT_EQ(status, TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
            EXPECT_STREQ(diagnostic.reason, "region box policy requires zero border and padding");
            ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, regions[i - 1]);
            EXPECT_STREQ(diagnostic.origin->qname, i == 1 ? "f:region-body" : "f:region-before");
            ASSERT_TRUE(regions[i - 1]->remove_attribute(i == 1 ? "padding" : "border"));
        }
    }
}

TEST_F(SecondaryViewTest, NativeConditionalMastersUseOrderedRunsAndLogicalFolioParity) {
    stylesheet("p, div { margin:0; font:10px/12px Arial }");
    ASSERT_NE(page_master("first", "size:160px 100px; margin:10px"), nullptr);
    ASSERT_NE(page_master("odd", "size:180px 100px; margin:10px"), nullptr);
    ASSERT_NE(page_master("even", "size:200px 100px; margin:10px"), nullptr);
    DomElement* master = page_control("r:sequence-master"); ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* skip = page_control("r:master-run", master); ASSERT_NE(skip, nullptr); ASSERT_TRUE(skip->set_attribute("maximum-repeats", "0"));
    ASSERT_NE(master_choice(skip, "odd"), nullptr);
    DomElement* single = page_control("r:master-run", master); ASSERT_NE(single, nullptr); ASSERT_TRUE(single->set_attribute("maximum-repeats", "1"));
    ASSERT_NE(master_choice(single, "first"), nullptr);
    DomElement* run = page_control("r:master-run", master); ASSERT_NE(run, nullptr);
    ASSERT_NE(master_choice(run, "even", "any", "even"), nullptr); ASSERT_NE(master_choice(run, "odd"), nullptr);
    DomElement* seq = page_sequence("chapter", "2"); ASSERT_NE(seq, nullptr);
    ASSERT_NE(block("A", nullptr, "div", seq), nullptr);
    ASSERT_NE(block("B", "break-before:page", "div", seq), nullptr);
    ASSERT_NE(block("C", "break-before:page", "div", seq), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    const char* names[] = {"first", "odd", "even"};
    for (size_t i = 0; i < 3; i++) { EXPECT_STREQ(tree->model->pages.get()[i]->name, names[i]); EXPECT_EQ(tree->model->pages.get()[i]->folio, i + 2); }
}

TEST_F(SecondaryViewTest, NativeTerminalMastersRemeasureWidthBeforeCommittingAndRetainMaterialForTheLastPage) {
    stylesheet("p, div { margin:0; font:10px/20px Arial; orphans:1; widows:1 }");
    ASSERT_NE(page_master("normal", "size:200px 80px; margin:10px"), nullptr);
    ASSERT_NE(page_master("last", "size:80px 80px; margin:10px"), nullptr);
    DomElement* master = page_control("r:sequence-master"); ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* run = page_control("r:master-run", master); ASSERT_NE(run, nullptr);
    ASSERT_NE(master_choice(run, "last", "last"), nullptr); ASSERT_NE(master_choice(run, "normal"), nullptr);
    DomElement* seq = page_sequence("chapter"); ASSERT_NE(seq, nullptr);
    DomElement* content = block("Alpha Beta Gamma Delta Epsilon Zeta Eta Theta Iota Kappa Lambda Mu Nu Xi Omicron Pi Rho Sigma Tau Upsilon Phi Chi Psi Omega", nullptr, "div", seq);
    ASSERT_NE(content, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_GE(tree->model->page_count, 2u); EXPECT_GE(diagnostic.reference_passes, 2u);
    for (size_t i = 0; i < tree->model->page_count; i++) {
        bool last = i + 1 == tree->model->page_count;
        EXPECT_STREQ(tree->model->pages.get()[i]->name, last ? "last" : "normal");
        EXPECT_FLOAT_EQ(tree->model->pages.get()[i]->content_rect.width, last ? 60.0f : 180.0f);
    }
    ViewNodeState* text = view_tree_node_state(tree, content->first_child, false); ASSERT_NE(text, nullptr);
    size_t covered = 0;
    for (LayoutViewNode* node = text->first_occurrence; node; node = node->next_occurrence) covered += node->text_length;
    EXPECT_EQ(covered, strlen(content->first_child->as_text()->text));
}

TEST_F(SecondaryViewTest, NativeOnlyAndBlankTerminalMastersIncludeRealSequencePadding) {
    stylesheet("p, div { margin:0; font:10px/12px Arial }");
    ASSERT_NE(page_master("normal", "size:160px 100px; margin:10px"), nullptr);
    ASSERT_NE(page_master("only", "size:180px 100px; margin:10px"), nullptr);
    ASSERT_NE(page_master("blank", "size:200px 100px; margin:10px"), nullptr);
    DomElement* master = page_control("r:sequence-master"); ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* run = page_control("r:master-run", master); ASSERT_NE(run, nullptr);
    ASSERT_NE(master_choice(run, "blank", "last", "any", "blank"), nullptr);
    ASSERT_NE(master_choice(run, "only", "only"), nullptr); ASSERT_NE(master_choice(run, "normal"), nullptr);
    DomElement* first = page_sequence("chapter", "1", "no-force"); ASSERT_NE(first, nullptr); ASSERT_NE(block("Only", nullptr, "div", first), nullptr);
    DomElement* second = page_sequence("chapter", "1", "even"); ASSERT_NE(second, nullptr); ASSERT_NE(block("Padded", nullptr, "div", second), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_STREQ(tree->model->pages.get()[0]->name, "only"); EXPECT_STREQ(tree->model->pages.get()[1]->name, "normal");
    EXPECT_STREQ(tree->model->pages.get()[2]->name, "blank"); EXPECT_TRUE(tree->model->pages.get()[2]->blank);
}

TEST_F(SecondaryViewTest, NativeTerminalWidthCanRequireAnAdditionalPageWithoutCountOscillation) {
    stylesheet("p, div { margin:0; font:10px/20px Arial; orphans:1; widows:1 }");
    ASSERT_NE(page_master("normal", "size:200px 80px; margin:10px"), nullptr);
    ASSERT_NE(page_master("last", "size:80px 80px; margin:10px"), nullptr);
    DomElement* master = page_control("r:sequence-master"); ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* run = page_control("r:master-run", master); ASSERT_NE(run, nullptr);
    ASSERT_NE(master_choice(run, "last", "last"), nullptr); ASSERT_NE(master_choice(run, "normal"), nullptr);
    DomElement* seq = page_sequence("chapter"); ASSERT_NE(seq, nullptr);
    DomElement* content[6] = {};
    for (size_t i = 0; i < 6; i++) { content[i] = block("Alpha Beta Gamma Delta", nullptr, "div", seq); ASSERT_NE(content[i], nullptr); }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u); EXPECT_EQ(diagnostic.reference_passes, 2u);
    EXPECT_STREQ(tree->model->pages.get()[1]->name, "normal"); EXPECT_STREQ(tree->model->pages.get()[2]->name, "last");
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, content[4]->first_child, false)->first_occurrence), 2u);
    EXPECT_EQ(occurrence_page(view_tree_node_state(tree, content[5]->first_child, false)->first_occurrence), 3u);
    options.max_reference_passes = 1; ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, NativeSequenceAutoPaddingRoundingAndLatePageBudgetFailureAreTransactional) {
    stylesheet("p, div { margin:0; font:10px/12px Arial }");
    ASSERT_NE(page_master("sheet", "size:160px 100px; margin:10px"), nullptr);
    DomElement* first = page_sequence("sheet", "2.6", "auto"); ASSERT_NE(first, nullptr);
    ASSERT_NE(block("First", nullptr, "div", first), nullptr);
    DomElement* second = page_sequence("sheet", "1", "even"); ASSERT_NE(second, nullptr);
    ASSERT_NE(block("Second", nullptr, "div", second), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); options.max_pages = 3;
    PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
    options.max_pages = 4; ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 4u);
    EXPECT_EQ(tree->model->pages.get()[0]->folio, 3u); EXPECT_EQ(tree->model->pages.get()[1]->folio, 4u);
    EXPECT_EQ(tree->model->pages.get()[2]->folio, 1u); EXPECT_EQ(tree->model->pages.get()[3]->folio, 2u);
    EXPECT_TRUE(tree->model->pages.get()[1]->blank); EXPECT_TRUE(tree->model->pages.get()[3]->blank);
}

TEST_F(SecondaryViewTest, NativeSequenceMasterExhaustionAndInvalidPredicatesDiagnoseWithoutPartialPages) {
    stylesheet("p, div { margin:0; font:10px/12px Arial }");
    ASSERT_NE(page_master("sheet", "size:160px 100px; margin:10px"), nullptr);
    DomElement* master = page_control("r:sequence-master"); ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* run = page_control("r:master-run", master); ASSERT_NE(run, nullptr); ASSERT_TRUE(run->set_attribute("maximum-repeats", "1"));
    DomElement* choice = master_choice(run, "sheet"); ASSERT_NE(choice, nullptr);
    DomElement* seq = page_sequence("chapter"); ASSERT_NE(seq, nullptr);
    ASSERT_NE(block("First", nullptr, "div", seq), nullptr); ASSERT_NE(block("Second", "break-before:page", "div", seq), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
    EXPECT_NE(strstr(diagnostic.reason, "repeat limit"), nullptr); EXPECT_EQ(diagnostic.source.address, seq);
    ASSERT_TRUE(choice->set_attribute("odd-or-even", "left")); ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "invalid conditional master predicate"); EXPECT_EQ(diagnostic.source.address, choice);
}

TEST_F(SecondaryViewTest, NativeWiderOnlyMasterCanFitMaterialThatNeedsSeveralOrdinaryPages) {
    stylesheet("p, div { margin:0; font:10px/20px Arial; orphans:1; widows:1 }");
    ASSERT_NE(page_master("normal", "size:80px 80px; margin:10px"), nullptr);
    ASSERT_NE(page_master("only", "size:240px 80px; margin:10px"), nullptr);
    DomElement* master = page_control("r:sequence-master"); ASSERT_NE(master, nullptr); ASSERT_TRUE(master->set_attribute("name", "chapter"));
    DomElement* run = page_control("r:master-run", master); ASSERT_NE(run, nullptr);
    ASSERT_NE(master_choice(run, "only", "only"), nullptr); ASSERT_NE(master_choice(run, "normal"), nullptr);
    DomElement* seq = page_sequence("chapter"); ASSERT_NE(seq, nullptr);
    ASSERT_NE(block("Alpha Beta Gamma Delta Epsilon Zeta Eta Theta Iota Kappa Lambda Mu Nu Xi Omicron Pi", nullptr, "div", seq), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 1u); EXPECT_STREQ(tree->model->pages.get()[0]->name, "only");
    EXPECT_FLOAT_EQ(tree->model->pages.get()[0]->content_rect.width, 220.0f);
}

TEST_F(SecondaryViewTest, NativeWhitespaceLinefeedModesKeepSourceRangesAndControlOnlyTheirOwnBreaks) {
    stylesheet("@page { size:220px 220px; margin:10px } p, div { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    const char* modes[] = {"ignore", "treat-as-space", "preserve", "treat-as-zero-width-space"}; DomElement* nodes[4] = {};
    for (size_t i = 0; i < 4; i++) {
        nodes[i] = block("A\nB"); ASSERT_NE(nodes[i], nullptr); ASSERT_TRUE(nodes[i]->set_attribute("r:linefeed-treatment", modes[i]));
    }
    DomElement* soft = block("A\nB", "width:8px"); ASSERT_NE(soft, nullptr);
    ASSERT_TRUE(soft->set_attribute("r:linefeed-treatment", "treat-as-zero-width-space"));
    DomElement* nowrap = block("A\nB"); ASSERT_NE(nowrap, nullptr);
    ASSERT_TRUE(nowrap->set_attribute("r:linefeed-treatment", "preserve")); ASSERT_TRUE(nowrap->set_attribute("r:wrap-option", "no-wrap"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    for (size_t i = 0; i < 4; i++) {
        SCOPED_TRACE(modes[i]); LayoutViewNode* a = source_glyph_text(tree, nodes[i]->first_child, "A");
        LayoutViewNode* b = source_glyph_text(tree, nodes[i]->first_child, "B"); ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr);
        if (i == 2) { EXPECT_FLOAT_EQ(a->rect.x, b->rect.x); EXPECT_FLOAT_EQ(b->rect.y - a->rect.y, 12.0f); }
        else {
            EXPECT_FLOAT_EQ(a->rect.y, b->rect.y);
            float space = i == 1 ? font_measure_char(a->computed_style->font.font_handle, ' ') : 0.0f;
            EXPECT_NEAR(b->rect.x, a->rect.x + a->rect.width + space, 0.001f);
        }
        EXPECT_STREQ(nodes[i]->first_child->as_text()->text, "A\nB");
    }
    for (DomElement* node : {soft, nowrap}) {
        LayoutViewNode* a = source_glyph_text(tree, node->first_child, "A"), *b = source_glyph_text(tree, node->first_child, "B");
        ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); EXPECT_FLOAT_EQ(a->rect.x, b->rect.x); EXPECT_FLOAT_EQ(b->rect.y - a->rect.y, 12.0f);
    }
}

TEST_F(SecondaryViewTest, NativeWhitespaceInheritanceUsesParentComputedTraitsAndCssPreLineKeepsOnlyLinefeeds) {
    stylesheet("@page { size:220px 120px; margin:10px } p, div, span { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* inherited = block("A\nB", "white-space:pre-wrap"); ASSERT_NE(inherited, nullptr);
    ASSERT_TRUE(inherited->set_attribute("r:linefeed-treatment", "inherit"));
    ASSERT_TRUE(inherited->set_attribute("r:white-space-collapse", "inherit"));
    DomElement* preline = block("  A  \n  B  ", "white-space:pre-line"); ASSERT_NE(preline, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ViewCssStyle* style = view_css_resolve(tree, inherited); ASSERT_NE(style, nullptr); ASSERT_NE(style->whitespace, nullptr);
    EXPECT_EQ(style->whitespace->linefeed, RADIANT_LINEFEED_SPACE); EXPECT_TRUE(style->whitespace->collapse);
    LayoutViewNode* a = source_glyph_text(tree, inherited->first_child, "A"), *b = source_glyph_text(tree, inherited->first_child, "B");
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); EXPECT_FLOAT_EQ(a->rect.y, b->rect.y);
    a = source_glyph_text(tree, preline->first_child, "A"); b = source_glyph_text(tree, preline->first_child, "B");
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); EXPECT_FLOAT_EQ(a->rect.x, 10.0f); EXPECT_FLOAT_EQ(b->rect.x, a->rect.x);
    EXPECT_FLOAT_EQ(b->rect.y - a->rect.y, 12.0f);
}

TEST_F(SecondaryViewTest, NativeWhitespaceTreatmentControlsBothLineEdgesIndependentlyOfCollapsing) {
    stylesheet("@page { size:220px 220px; margin:10px } p, div { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    const char* modes[] = {"ignore", "preserve", "ignore-if-before-linefeed", "ignore-if-after-linefeed", "ignore-if-surrounding-linefeed"};
    const float leading[] = {0, 2, 2, 0, 0}, total[] = {0, 4, 2, 2, 0}; DomElement* nodes[5] = {};
    for (size_t i = 0; i < 5; i++) {
        nodes[i] = block("  A  \n  B  "); ASSERT_NE(nodes[i], nullptr);
        ASSERT_TRUE(nodes[i]->set_attribute("r:linefeed-treatment", "preserve")); ASSERT_TRUE(nodes[i]->set_attribute("r:white-space-collapse", "false"));
        ASSERT_TRUE(nodes[i]->set_attribute("r:white-space-treatment", modes[i]));
    }
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    for (size_t i = 0; i < 5; i++) {
        SCOPED_TRACE(modes[i]); LayoutViewNode* a = source_glyph_text(tree, nodes[i]->first_child, "A"), *b = source_glyph_text(tree, nodes[i]->first_child, "B");
        ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); float space = font_measure_char(a->computed_style->font.font_handle, ' ');
        EXPECT_NEAR(a->rect.x, 10.0f + leading[i] * space, 0.001f); EXPECT_FLOAT_EQ(a->rect.x, b->rect.x);
        float width = 0.0f; ViewNodeState* state = view_tree_node_state(tree, nodes[i]->first_child, false); ASSERT_NE(state, nullptr);
        for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
            if (fragment->parent == a->parent) width += fragment->rect.width;
        EXPECT_NEAR(width, a->rect.width + total[i] * space, 0.001f);
    }
}

TEST_F(SecondaryViewTest, NativeWhitespaceCollapsesAcrossInlineSourcesAndRetainsIndependentPreviewStyles) {
    stylesheet("@page { size:220px 100px; margin:10px } p, div, span { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    ASSERT_TRUE(source->set_attribute("xmlns:r", RADIANT_PAGE_NAMESPACE));
    DomElement* paragraph = block("A  "); ASSERT_NE(paragraph, nullptr);
    ASSERT_TRUE(paragraph->set_attribute("r:linefeed-treatment", "preserve"));
    ASSERT_TRUE(paragraph->set_attribute("r:white-space-treatment", "preserve"));
    ASSERT_TRUE(paragraph->set_attribute("r:white-space-collapse", "true"));
    DomElement* middle = block("  \n  B  ", nullptr, "span", paragraph); ASSERT_NE(middle, nullptr);
    DomElement* end = block(" C", nullptr, "span", paragraph); ASSERT_NE(end, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* a = source_glyph_text(tree, paragraph->first_child, "A"), *b = source_glyph_text(tree, middle->first_child, "B");
    LayoutViewNode* c = source_glyph_text(tree, end->first_child, "C"); ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(c, nullptr);
    float space = font_measure_char(a->computed_style->font.font_handle, ' ');
    EXPECT_FLOAT_EQ(a->rect.x, b->rect.x); EXPECT_FLOAT_EQ(b->rect.y - a->rect.y, 12.0f);
    EXPECT_NEAR(c->rect.x, b->rect.x + b->rect.width + space, 0.001f);
    ViewPreviewOptions preview_options = view_preview_options_default(); ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options);
    ASSERT_NE(preview, nullptr); ASSERT_TRUE(paragraph->set_attribute("r:wrap-option", "sometimes")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "invalid native whitespace trait"); EXPECT_EQ(diagnostic.source.address, paragraph);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); const ViewPageBox* page = view_tree_page_material(preview, preview->model->pages.get()[0]); ASSERT_NE(page, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&page->node, text); EXPECT_STREQ(text->str, "AB C"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, FoWhitespaceRefinesToInheritedNativeTraitsAndCommonDiagnosticsKeepOriginalSources) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block linefeed-treatment='preserve' white-space-treatment='preserve' white-space-collapse='false'>A\n<f:inline>B</f:inline></f:block>"
        "</f:flow></f:page-sequence></f:root>"); ASSERT_NE(fo, nullptr);
    DomElement* original = fo->last_child_element()->last_child_element()->first_child_element(); ASSERT_NE(original, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    for (size_t i = 0; i < 2; i++) {
        if (i) ASSERT_TRUE(original->set_attribute("white-space-collapse", "maybe"));
        RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
        ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
        DomElement* generated = build_dom_tree_from_element(translated->root, &doc, nullptr); ASSERT_NE(generated, nullptr);
        ASSERT_TRUE(radiant_page_set_origins(&doc, translated->origins)); doc.root = lam::up(generated);
        ViewTree* tree = secondary(); TypesetStatus status = layout_secondary_view(tree, &options, &diagnostic);
        if (!i) {
            ASSERT_EQ(status, TYPESET_OK) << diagnostic.reason;
            DomElement* block = generated->last_child_element()->last_child_element()->first_child_element();
            ViewCssStyle* inline_style = view_css_resolve(tree, block->last_child_element()); ASSERT_NE(inline_style, nullptr); ASSERT_NE(inline_style->whitespace, nullptr);
            EXPECT_EQ(inline_style->whitespace->linefeed, RADIANT_LINEFEED_PRESERVE); EXPECT_FALSE(inline_style->whitespace->collapse);
            EXPECT_FALSE(inline_style->whitespace->discard_start); EXPECT_FALSE(inline_style->whitespace->discard_end);
        } else {
            EXPECT_EQ(status, TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u); ASSERT_NE(diagnostic.origin, nullptr);
            EXPECT_EQ(diagnostic.origin->source.address, original); EXPECT_STREQ(diagnostic.reason, "invalid native whitespace trait");
        }
    }
}

TEST_F(SecondaryViewTest, NativeAuthoredNoteUsesTheLastCallAreaAndDoesNotGenerateNumbering) {
    stylesheet("@page { size:120px 60px; margin:10px } p, div { margin:0; font:10px/12px Arial; white-space:pre-wrap; orphans:1; widows:1 }");
    DomElement* paragraph = block(nullptr); ASSERT_NE(paragraph, nullptr);
    DomElement* call = nullptr, *body = nullptr;
    DomElement* note = authored_note(paragraph, "A\nB\nC\nD", "Authored body", &call, &body); ASSERT_NE(note, nullptr);
    ASSERT_TRUE(call->set_attribute("style", "color:red")); ASSERT_TRUE(body->set_attribute("style", "color:blue"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 2u);
    ViewNodeState* state = view_tree_node_state(tree, call->first_child, false); ASSERT_NE(state, nullptr);
    bool first = false, last = false;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence) {
        EXPECT_EQ(fragment->role, VIEW_FRAGMENT_BODY);
        if (!fragment->glyph_run) continue;
        EXPECT_EQ(fragment->glyph_run->color.r, 255); EXPECT_EQ(fragment->glyph_run->color.b, 0);
        first |= occurrence_page(fragment) == 1; last |= occurrence_page(fragment) == 2;
    }
    EXPECT_TRUE(first); EXPECT_TRUE(last);
    LayoutViewNode* note_box = source_fragment(tree, body, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(note_box, nullptr);
    EXPECT_EQ(occurrence_page(note_box), 2u);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(note_box, text);
    EXPECT_STREQ(text->str, "Authored body"); strbuf_free(text);
    ViewPreviewOptions preview_options = view_preview_options_default(); ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options);
    ASSERT_NE(preview, nullptr); options.max_pages = 1; ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    const ViewPageBox* retained = view_tree_page_material(preview, preview->model->pages.get()[1]); ASSERT_NE(retained, nullptr);
    text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&retained->node, text);
    EXPECT_NE(strstr(text->str, "D"), nullptr); EXPECT_NE(strstr(text->str, "Authored body"), nullptr); strbuf_free(text);
}

TEST_F(SecondaryViewTest, NativeAuthoredEmptyCallKeepsAnAnchorAndClassicNotesKeepTheirOwnCounter) {
    stylesheet("@page { size:220px 120px; margin:10px } p, div, span { margin:0; font:10px/12px Arial }");
    DomElement* paragraph = block("Statement "); ASSERT_NE(paragraph, nullptr);
    DomElement* body = nullptr;
    DomElement* note = authored_note(paragraph, nullptr, "Explicit", nullptr, &body); ASSERT_NE(note, nullptr);
    DomElement* classic = block("Classic", "float:footnote", "span", paragraph); ASSERT_NE(classic, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    LayoutViewNode* anchor = source_fragment(tree, note, VIEW_FRAGMENT_BODY, false); ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(anchor->glyph_run, nullptr); EXPECT_FLOAT_EQ(anchor->rect.width, 0.0f);
    LayoutViewNode* call = source_fragment(tree, classic, VIEW_FRAGMENT_BODY, false); ASSERT_NE(call, nullptr);
    ASSERT_NE(call->glyph_run, nullptr); EXPECT_STREQ(call->glyph_run->text, "1");
    LayoutViewNode* note_box = source_fragment(tree, body, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(note_box, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(note_box, text);
    EXPECT_STREQ(text->str, "Explicit"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, NativeAuthoredNoteAnchorsAfterRetainedWhitespaceAreasFromItsCall) {
    stylesheet("@page { size:120px 40px; margin:10px } p, div { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    DomElement* paragraph = block(nullptr), *call = nullptr, *body = nullptr; ASSERT_NE(paragraph, nullptr);
    ASSERT_NE(authored_note(paragraph, "A\n  ", "Body", &call, &body), nullptr);
    ASSERT_TRUE(call->set_attribute("r:linefeed-treatment", "preserve"));
    ASSERT_TRUE(call->set_attribute("r:white-space-treatment", "preserve"));
    ASSERT_TRUE(call->set_attribute("r:white-space-collapse", "false"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    LayoutViewNode* note = source_fragment(tree, body, VIEW_FRAGMENT_NOTE, true); ASSERT_NE(note, nullptr); EXPECT_EQ(occurrence_page(note), 3u);
    ViewNodeState* state = view_tree_node_state(tree, call->first_child, false); ASSERT_NE(state, nullptr); size_t spaces = 0;
    for (LayoutViewNode* fragment = state->first_occurrence; fragment; fragment = fragment->next_occurrence)
        if (!fragment->glyph_run && fragment->rect.width > 0.0f) { EXPECT_EQ(occurrence_page(fragment), 2u); spaces++; }
    EXPECT_EQ(spaces, 2u);
}

TEST_F(SecondaryViewTest, NativeAuthoredNotesDiagnoseInvalidBindingsAndNestedOrBlockCalls) {
    stylesheet("@page { size:220px 120px; margin:10px } p, div { margin:0; font:10px/12px Arial }");
    DomElement* call = nullptr, *body = nullptr;
    DomElement* note = authored_note(source, "Call", "Body", &call, &body); ASSERT_NE(note, nullptr);
    DomElement* invalid = block("Extra", nullptr, "span", note); ASSERT_NE(invalid, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "note requires one note-call followed by one note-body");
    ASSERT_TRUE(note->DomNode::remove_child(invalid)); ASSERT_TRUE(view_tree_model_reset(tree));
    DomElement* block_call = block("Block", nullptr, "div", call); ASSERT_NE(block_call, nullptr);
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "note call requires inline material"); EXPECT_EQ(diagnostic.source.address, call);
    ASSERT_TRUE(call->DomNode::remove_child(block_call)); ASSERT_TRUE(view_tree_model_reset(tree));
    DomElement* nested = authored_note(body->first_child_element(), "Nested", "Body"); ASSERT_NE(nested, nullptr);
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_STREQ(diagnostic.reason, "footnotes require the paged body flow"); EXPECT_EQ(diagnostic.source.address, nested);
}

TEST_F(SecondaryViewTest, FoAuthoredFootnotesLowerToCommonBindingsAndDiagnoseOriginalNestedSources) {
    DomElement* fo = formatting_root(
        "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='9pt'>"
        "<f:layout-master-set><f:simple-page-master master-name='sheet' page-width='150pt' page-height='90pt'>"
        "<f:region-body margin='7.5pt'/></f:simple-page-master></f:layout-master-set>"
        "<f:page-sequence master-reference='sheet' force-page-count='no-force'><f:flow flow-name='xsl-region-body'>"
        "<f:block>Statement <f:footnote><f:inline color='red'>[a]</f:inline><f:footnote-body color='blue'>"
        "<f:block>[a] Authored body</f:block></f:footnote-body></f:footnote></f:block></f:flow></f:page-sequence></f:root>");
    ASSERT_NE(fo, nullptr); DomElement* original_note = fo->last_child_element()->last_child_element()->first_child_element()->last_child_element();
    ASSERT_NE(original_note, nullptr);
    RadiantFoOptions fo_options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &fo_options);
    ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* note = generated->last_child_element()->last_child_element()->first_child_element()->last_child_element(); ASSERT_NE(note, nullptr);
    EXPECT_TRUE(radiant_page_element(note, "note")); EXPECT_TRUE(radiant_page_element(note->first_child_element(), "note-call"));
    EXPECT_TRUE(radiant_page_element(note->last_child_element(), "note-body"));
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    EXPECT_EQ(tree->model->page_count, 1u);
    LayoutViewNode* note_box = source_fragment(tree, note->last_child_element(), VIEW_FRAGMENT_NOTE, true); ASSERT_NE(note_box, nullptr);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(note_box, text);
    EXPECT_STREQ(text->str, "[a] Authored body"); strbuf_free(text);
    DomElement* nested = block(nullptr, nullptr, "f:footnote", original_note->last_child_element()->first_child_element()); ASSERT_NE(nested, nullptr);
    translated = radiant_fo_translate(&doc, fo, &fo_options); ASSERT_NE(translated, nullptr);
    EXPECT_EQ(translated->diagnostic.status, TYPESET_INVALID); EXPECT_EQ(translated->diagnostic.source.address, nested);
    EXPECT_STREQ(translated->diagnostic.reason, "FO footnote cannot descend from a footnote, float or marker");
}

TEST_F(SecondaryViewTest, NativeTerminalMasterWaitsForActualNoteContinuationAndDrainsBeforeTheNextSequence) {
    stylesheet("p, div, span { margin:0; font:10px/12px Arial; white-space:pre-wrap; orphans:1; widows:1 }");
    DomElement* sequence = terminal_sequence("size:200px 100px; margin:10px", "size:120px 60px; margin:10px"); ASSERT_NE(sequence, nullptr);
    DomElement* call = block("Call ", nullptr, "div", sequence); ASSERT_NE(call, nullptr);
    DomElement* note = block("A\nB\nC\nD\nE\nF\nG\nH", "float:footnote", "span", call); ASSERT_NE(note, nullptr);
    DomElement* following = page_sequence("normal", "20"); ASSERT_NE(following, nullptr);
    ASSERT_NE(block("Following", nullptr, "div", following), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_STREQ(tree->model->pages.get()[0]->name, "normal"); EXPECT_STREQ(tree->model->pages.get()[1]->name, "last");
    EXPECT_EQ(tree->model->pages.get()[0]->sequence.get(), tree->model->pages.get()[1]->sequence.get());
    EXPECT_EQ(tree->model->pages.get()[2]->sequence->source.address, following); EXPECT_EQ(tree->model->pages.get()[2]->folio, 20u);
    ViewNodeState* notes = view_tree_node_state(tree, note, false); ASSERT_NE(notes, nullptr);
    size_t occurrences = 0;
    for (LayoutViewNode* node = notes->first_occurrence; node; node = node->next_occurrence) if (node->role == VIEW_FRAGMENT_NOTE && node->paint_box) {
        EXPECT_LE(occurrence_page(node), 2u); occurrences++;
    }
    EXPECT_EQ(occurrences, 2u);
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&tree->model->pages.get()[1]->node, text);
    EXPECT_NE(strstr(text->str, "F"), nullptr); EXPECT_NE(strstr(text->str, "H"), nullptr); EXPECT_EQ(strstr(text->str, "Following"), nullptr); strbuf_free(text);
}

TEST_F(SecondaryViewTest, NativeTerminalNotePagesRetainARealTailForASmallerLastRegion) {
    stylesheet("p, div, span { margin:0; font:10px/12px Arial; white-space:pre-wrap; orphans:1; widows:1 }");
    DomElement* sequence = terminal_sequence("size:200px 100px; margin:10px", "size:120px 40px; margin:10px"); ASSERT_NE(sequence, nullptr);
    DomElement* call = block("Call ", nullptr, "div", sequence); ASSERT_NE(call, nullptr);
    ASSERT_NE(block("A\nB\nC\nD\nE\nF\nG\nH", "float:footnote", "span", call), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_STREQ(tree->model->pages.get()[0]->name, "normal"); EXPECT_STREQ(tree->model->pages.get()[1]->name, "normal");
    EXPECT_STREQ(tree->model->pages.get()[2]->name, "last");
    StrBuf* text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&tree->model->pages.get()[2]->node, text);
    EXPECT_STREQ(text->str, "H"); strbuf_free(text);
    ViewPreviewOptions preview_options = view_preview_options_default(); ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options);
    ASSERT_NE(preview, nullptr); options.max_pages = 2; ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    EXPECT_EQ(preview->model->page_count, 3u); ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    const ViewPageBox* retained = view_tree_page_material(preview, preview->model->pages.get()[2]); ASSERT_NE(retained, nullptr);
    text = strbuf_new(); ASSERT_NE(text, nullptr); append_fragment_text(&retained->node, text); EXPECT_STREQ(text->str, "H"); strbuf_free(text);
}

TEST_F(SecondaryViewTest, NativeTerminalFloatPagesReinsertAnIndivisibleTailAndMeasureItsActualFolio) {
    stylesheet("p, div { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    DomElement* sequence = terminal_sequence("size:200px 100px; margin:10px", "size:120px 40px; margin:10px"); ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(sequence->set_attribute("initial-page-number", "7")); ASSERT_TRUE(sequence->set_attribute("format", "I"));
    ASSERT_NE(block("Body", "height:60px", "div", sequence), nullptr);
    DomElement* first = block("A", "float:top; float-reference:page; height:36px", "div", sequence); ASSERT_NE(first, nullptr);
    DomElement* tail = block("Page ", "float:top; float-reference:page", "div", sequence); ASSERT_NE(tail, nullptr);
    DomElement* folio = page_control("r:folio", tail); ASSERT_NE(folio, nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_STREQ(tree->model->pages.get()[1]->name, "normal"); EXPECT_STREQ(tree->model->pages.get()[2]->name, "last");
    EXPECT_EQ(occurrence_page(source_fragment(tree, first, VIEW_FRAGMENT_FLOAT, true)), 2u);
    EXPECT_EQ(occurrence_page(source_fragment(tree, tail, VIEW_FRAGMENT_FLOAT, true)), 3u);
    LayoutViewNode* number = source_fragment(tree, folio, VIEW_FRAGMENT_FLOAT, false); ASSERT_NE(number, nullptr);
    ASSERT_NE(number->glyph_run, nullptr); EXPECT_STREQ(number->glyph_run->text, "IX");
    EXPECT_FLOAT_EQ(number->rect.width, font_measure_text(number->computed_style->font.font_handle, "IX", 2).width);
}

TEST_F(SecondaryViewTest, NativeNoteFolioMeasurementRejectsAnOverflowingFormattedAtomicQuery) {
    stylesheet("p, div, span { margin:0; font:10px/12px Arial; orphans:1; widows:1 }");
    DomElement* sequence = terminal_sequence("size:60px 100px; margin:10px", "size:60px 60px; margin:10px"); ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(sequence->set_attribute("initial-page-number", "888")); ASSERT_TRUE(sequence->set_attribute("format", "I"));
    DomElement* call = block("Call ", nullptr, "div", sequence); ASSERT_NE(call, nullptr);
    DomElement* note = block(nullptr, "float:footnote", "span", call); ASSERT_NE(note, nullptr);
    ASSERT_NE(page_control("r:folio", note), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, NativeTerminalOnlyMasterCannotPublishNonterminalFallbackPages) {
    stylesheet("p, div, span { margin:0; font:10px/12px Arial; white-space:pre-wrap; orphans:1; widows:1 }");
    DomElement* sequence = terminal_sequence("size:200px 100px; margin:10px", "size:120px 60px; margin:10px", false); ASSERT_NE(sequence, nullptr);
    DomElement* call = block("Call ", nullptr, "div", sequence); ASSERT_NE(call, nullptr);
    ASSERT_NE(block("A\nB\nC\nD\nE\nF\nG\nH", "float:footnote", "span", call), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_STREQ(diagnostic.reason, "sequence material cannot satisfy its terminal master alternatives");
    EXPECT_EQ(diagnostic.source.address, sequence); EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, NativePaddingCannotReplaceAnUnplaceableOrdinaryBodyWithTerminalGeometry) {
    stylesheet("p, div { margin:0; font:10px/30px Arial; orphans:1; widows:1 }");
    DomElement* sequence = terminal_sequence("size:120px 40px; margin:10px", "size:200px 100px; margin:10px"); ASSERT_NE(sequence, nullptr);
    ASSERT_TRUE(sequence->set_attribute("force-page-count", "even")); ASSERT_NE(block("Body", nullptr, "div", sequence), nullptr);
    ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_UNPLACEABLE);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_FALSE(tree->model->committed);
}

TEST_F(SecondaryViewTest, FoSequenceProgramsLowerToCommonControlsAndGeneratedRulesRetainProvenance) {
    const char* xml = "<f:root xmlns:f='http://www.w3.org/1999/XSL/Format' font-family='Arial' font-size='7.5pt' line-height='15pt'>"
        "<f:layout-master-set><f:page-sequence-master master-name='chapter'>"
        "<f:single-page-master-reference master-reference='first'/>"
        "<f:repeatable-page-master-alternatives><f:conditional-page-master-reference master-reference='even' odd-or-even='even'/>"
        "<f:conditional-page-master-reference master-reference='odd'/></f:repeatable-page-master-alternatives></f:page-sequence-master>"
        "<f:simple-page-master master-name='first' page-width='150pt' page-height='90pt'><f:region-body/></f:simple-page-master>"
        "<f:simple-page-master master-name='even' page-width='165pt' page-height='90pt'><f:region-body/></f:simple-page-master>"
        "<f:simple-page-master master-name='odd' page-width='180pt' page-height='90pt'><f:region-body/></f:simple-page-master>"
        "</f:layout-master-set><f:page-sequence master-reference='chapter' initial-page-number='2' force-page-count='no-force'>"
        "<f:flow flow-name='xsl-region-body'><f:block>A</f:block><f:block break-before='page'>B</f:block>"
        "<f:block break-before='page'>C</f:block></f:flow></f:page-sequence></f:root>";
    XmlParseOptions xml_options = {true, true, nullptr, nullptr}; parse_xml_with_options(input, xml, &xml_options); ASSERT_FALSE(input->parse_failed);
    ElementReader original = ElementReader(input->root).childAt(0).asElement(); ASSERT_TRUE(original.isValid());
    DomElement* fo = build_dom_tree_from_element(const_cast<Element*>(original.element()), &doc, nullptr); ASSERT_NE(fo, nullptr);
    RadiantFoOptions options = radiant_fo_options_default(); RadiantFoTranslation* translated = radiant_fo_translate(&doc, fo, &options);
    ASSERT_NE(translated, nullptr); ASSERT_EQ(translated->diagnostic.status, TYPESET_OK) << translated->diagnostic.reason;
    DomElement* generated = install_fo_translation(translated); ASSERT_NE(generated, nullptr);
    DomElement* program = generated->first_child_element()->first_child_element(); ASSERT_NE(program, nullptr);
    DomElement* run = program->first_child_element(); ASSERT_NE(run, nullptr);
    EXPECT_TRUE(radiant_page_element(program, "sequence-master")); EXPECT_STREQ(run->get_attribute("maximum-repeats"), "1");
    DomElement* implicit_rule = run->first_child_element(); ASSERT_NE(implicit_rule, nullptr);
    const RadiantSourceOrigin* origin = radiant_page_source_origin(&doc, implicit_rule); ASSERT_NE(origin, nullptr);
    EXPECT_STREQ(origin->qname, "f:single-page-master-reference");
    ViewTree* tree = secondary(); PagedLayoutOptions layout_options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ASSERT_EQ(layout_secondary_view(tree, &layout_options, &diagnostic), TYPESET_OK) << diagnostic.reason;
    ASSERT_EQ(tree->model->page_count, 3u);
    EXPECT_STREQ(tree->model->pages.get()[0]->name, "first"); EXPECT_STREQ(tree->model->pages.get()[1]->name, "odd"); EXPECT_STREQ(tree->model->pages.get()[2]->name, "even");
    EXPECT_EQ(tree->model->pages.get()[0]->folio, 2u); EXPECT_EQ(tree->model->pages.get()[2]->folio, 4u);
    ASSERT_TRUE(implicit_rule->set_attribute("master-reference", "missing")); ASSERT_TRUE(view_tree_model_reset(tree)); diagnostic = {};
    EXPECT_EQ(layout_secondary_view(tree, &layout_options, &diagnostic), TYPESET_INVALID);
    ASSERT_NE(diagnostic.origin, nullptr); EXPECT_EQ(diagnostic.origin->source.address, origin->source.address);
    EXPECT_STREQ(diagnostic.origin->qname, "f:single-page-master-reference"); EXPECT_EQ(tree->model->page_count, 0u);
}


TEST_F(SecondaryViewTest, FileLoadedNativePagesResolveOrderedRelativeAndPrintStylesheets) {
    ASSERT_TRUE(create_dir("temp/paged-media-impl"));
    write_text_file("temp/paged-media-impl/native-import.css", ".probe{font:11px/12px Arial}");
    write_text_file("temp/paged-media-impl/native-linked.css",
        "@import 'native-import.css'; .probe{width:23px}");
    const char* path = "temp/paged-media-impl/native-styles.rpd";
    write_text_file(path, "<r:page-document xmlns:r='urn:lambda:radiant:page'>"
        "<style>.probe{width:11px;height:20px;margin:0}</style>"
        "<link rel='stylesheet' href='native-linked.css'/>"
        "<style media='print'>.probe{width:37px}</style>"
        "<style media='screen'>.probe{width:99px}</style>"
        "<r:fixed-pages><r:fixed-page width='100px' height='60px'>"
        "<div class='probe'>A</div></r:fixed-page></r:fixed-pages></r:page-document>");
    RenderExportSession session = {};
    ASSERT_TRUE(render_export_session_begin(&session, path, 0, 0, 800, 1200, 1.0f, true, true));
    ViewTree* tree = session.paged_view; ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->model->page_count, 1u);
    const RadiantFixedPage* fixed = tree->model->pages.get()[0]->fixed; ASSERT_NE(fixed, nullptr);
    DomElement* page = (DomElement*)fixed->source.address;
    ASSERT_NE(page->first_child, nullptr); DomElement* probe = page->first_child->as_element(); ASSERT_NE(probe, nullptr);
    ViewCssStyle* style = view_css_resolve(tree, probe); ASSERT_NE(style, nullptr);
    EXPECT_FLOAT_EQ(style->font.font_size, 11.0f);
    ViewNodeState* state = view_tree_node_state(tree, probe, false); ASSERT_NE(state, nullptr);
    ASSERT_NE(state->first_occurrence, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.width, 37.0f);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.height, 20.0f);
    render_export_session_end(&session);
}

TEST_F(SecondaryViewTest, NativeFixedPagesRetainSourceGeometryAndIgnoreFlowPageRules) {
    stylesheet("@page{size:20px 30px;margin:9px;@top-center{content:'unwanted'}}"
        "p,div{margin:0;font:10px/12px Arial}div{break-before:left;keep-with-next:always}");
    DomElement* container = page_control("r:fixed-pages"); ASSERT_NE(container, nullptr);
    DomElement* first = fixed_page(container, "60pt", "90pt"); ASSERT_NE(first, nullptr);
    ASSERT_TRUE(first->set_attribute("source-media-box", "-10 -20 100 80"));
    ASSERT_TRUE(first->set_attribute("source-crop-box", "0 -10 90 50"));
    ASSERT_TRUE(first->set_attribute("source-trim-box", "1 -9 89 49"));
    ASSERT_TRUE(first->set_attribute("source-bleed-box", "-1 -11 91 51"));
    ASSERT_TRUE(first->set_attribute("source-art-box", "2 -8 88 48"));
    ASSERT_TRUE(first->set_attribute("source-rotation", "-270"));
    ASSERT_TRUE(first->set_attribute("source-page", "49")); ASSERT_TRUE(first->set_attribute("source-label", "XLIX"));
    DomElement* text = block("Fixed source", "height:180px;background:#ddeeff", "div", first); ASSERT_NE(text, nullptr);
    DomElement* last = fixed_page(container, "120px", "60px"); ASSERT_NE(last, nullptr);
    ASSERT_NE(block("Second", nullptr, "div", last), nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
    ViewTree* tree = secondary(); ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK);
    ASSERT_EQ(tree->model->page_count, 2u);
    const ViewPageBox* page = tree->model->pages.get()[0]; ASSERT_NE(page->fixed, nullptr); EXPECT_EQ(page->style, nullptr);
    EXPECT_FLOAT_EQ(page->node.rect.width, 80.0f); EXPECT_FLOAT_EQ(page->node.rect.height, 120.0f);
    EXPECT_EQ(page->folio, 49u); EXPECT_EQ(page->page_number, 1u); EXPECT_STREQ(page->label, "XLIX");
    EXPECT_EQ(page->fixed->geometry.rotation, -270); EXPECT_EQ(page->fixed->geometry.box_mask, 31u);
    EXPECT_FLOAT_EQ(page->fixed->geometry.boxes[RADIANT_SOURCE_MEDIA].x, -10.0f);
    EXPECT_FLOAT_EQ(page->fixed->geometry.boxes[RADIANT_SOURCE_MEDIA].height, 100.0f);
    EXPECT_FLOAT_EQ(page->fixed->geometry.boxes[RADIANT_SOURCE_TRIM].width, 88.0f);
    EXPECT_EQ(page->margin_boxes[CSS_PAGE_TOP_CENTER], nullptr); EXPECT_FALSE(page->blank);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[1]->node.rect.width, 120.0f);
    EXPECT_FLOAT_EQ(tree->model->pages.get()[1]->node.rect.height, 60.0f);
    ViewNodeState* state = view_tree_node_state(tree, text, false); ASSERT_NE(state, nullptr); ASSERT_NE(state->first_occurrence, nullptr);
    EXPECT_FLOAT_EQ(state->first_occurrence->rect.x, 0.0f); EXPECT_FLOAT_EQ(state->first_occurrence->rect.height, 180.0f);
    PaintList paint = {}; paint_list_init(&paint, nullptr); ASSERT_TRUE(layout_secondary_paint_page(tree, page, &paint));
    ASSERT_TRUE(paint_ir_validate(&paint, nullptr)); paint_list_destroy(&paint);
    ViewPreviewOptions preview_options = view_preview_options_default(); ViewTree* preview = view_tree_page_instances_create(tree, nullptr, &preview_options);
    ASSERT_NE(preview, nullptr); ASSERT_TRUE(first->set_attribute("width", "61pt")); ASSERT_TRUE(view_tree_model_reset(tree));
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
    const ViewPageBox* retained = view_tree_page_material(preview, preview->model->pages.get()[0]); ASSERT_NE(retained, nullptr);
    EXPECT_STREQ(retained->label, "XLIX"); EXPECT_EQ(retained->fixed->geometry.rotation, -270);
    EXPECT_FLOAT_EQ(retained->node.rect.width, 80.0f); EXPECT_FLOAT_EQ(retained->fixed->geometry.boxes[RADIANT_SOURCE_ART].width, 86.0f);
}

TEST_F(SecondaryViewTest, NativeFixedPagesDiagnoseInvalidGeometryAndMixedContent) {
    stylesheet("p,div{margin:0;font:10px/12px Arial}");
    struct Invalid { const char* name; const char* value; };
    const Invalid cases[] = {{"width", "0px"}, {"width", "10%"}, {"width", "1em"}, {"height", "NaNpx"},
        {"source-media-box", "0 0 75"}, {"source-media-box", "0 0 75 75 garbage"},
        {"source-media-box", "0 0 0 75"}, {"source-media-box", "0 0 1e100 75"},
        {"source-crop-box", "80 80 90 90"}, {"source-crop-box", "0 0 70 75"},
        {"source-rotation", "45"}, {"source-rotation", "90.5"}, {"source-page", "0"}};
    for (const Invalid& item : cases) {
        SCOPED_TRACE(item.name);
        SCOPED_TRACE(item.value);
        DomElement* container = page_control("r:fixed-pages"); ASSERT_NE(container, nullptr);
        DomElement* fixed = fixed_page(container, "100px", "100px"); ASSERT_NE(fixed, nullptr);
        ASSERT_TRUE(fixed->set_attribute("source-media-box", "0 0 75 75")); ASSERT_TRUE(fixed->set_attribute(item.name, item.value));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(tree->model->page_count, 0u);
        EXPECT_EQ(diagnostic.source.address, fixed); ASSERT_TRUE(view_tree_secondary_release(&doc, tree));
        ASSERT_TRUE(source->DomNode::remove_child(container));
    }
    DomElement* container = page_control("r:fixed-pages"); ASSERT_NE(container, nullptr);
    ASSERT_NE(fixed_page(container, "100px", "100px"), nullptr); DomElement* extra = block("Mixed"); ASSERT_NE(extra, nullptr);
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {}; ViewTree* tree = secondary();
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(diagnostic.source.address, extra);
    ASSERT_TRUE(view_tree_secondary_release(&doc, tree)); ASSERT_TRUE(source->DomNode::remove_child(extra));
    ViewTree* continuous = secondary(VIEW_PRESENTATION_CONTINUOUS);
    EXPECT_EQ(layout_secondary_view(continuous, &options, &diagnostic), TYPESET_INVALID); EXPECT_EQ(continuous->model->page_count, 0u);
    EXPECT_STREQ(diagnostic.reason, "fixed page controls require paged presentation");
}

TEST_F(SecondaryViewTest, NativeFixedPagesPreserveFullSequencesAndRollbackLateBudgetFailure) {
    stylesheet("p,div{margin:0;font:10px/12px Arial}");
    DomElement* container = page_control("r:fixed-pages"); ASSERT_NE(container, nullptr);
    for (size_t i = 0; i < 53; i++) {
        DomElement* fixed = fixed_page(container, i % 2 ? "60px" : "80px", "40px"); ASSERT_NE(fixed, nullptr);
        ASSERT_NE(block("A", nullptr, "div", fixed), nullptr);
    }
    PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {}; ViewTree* tree = secondary();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK); EXPECT_EQ(tree->model->page_count, 53u);
    EXPECT_EQ(tree->model->pages.get()[52]->folio, 53u); EXPECT_FLOAT_EQ(tree->model->pages.get()[51]->node.rect.width, 60.0f);
    ASSERT_TRUE(view_tree_model_reset(tree)); options.max_pages = 52;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED); EXPECT_EQ(tree->model->page_count, 0u);
    ASSERT_TRUE(view_tree_model_reset(tree)); options = paged_layout_options_default(); options.max_items = 20;
    EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_BUDGET_EXHAUSTED);
    EXPECT_EQ(tree->model->page_count, 0u); EXPECT_EQ(tree->model->node_count, 1u); EXPECT_FALSE(tree->model->committed);
    ASSERT_TRUE(view_tree_model_reset(tree)); options = paged_layout_options_default();
    ASSERT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_OK); EXPECT_EQ(tree->model->page_count, 53u);
}
