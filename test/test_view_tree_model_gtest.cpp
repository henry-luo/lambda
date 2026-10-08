#include "../radiant/layout_table.hpp"
#include <gtest/gtest.h>
#include "../radiant/view.hpp"
#include "../radiant/view_tree_model.hpp"
#include "../radiant/view_tree_css.hpp"
#include "../radiant/typeset.hpp"
#include "../radiant/typeset_marks.hpp"
#include "../radiant/typeset_regions.hpp"
#include "../radiant/layout_paged.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/render.hpp"
#include "../lambda/input/css/css_paged_media.hpp"
#include "../lambda/input/css/selector_matcher.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lib/mem_factory.h"
#include "../lib/memtrack.h"
#include "../lib/tagged.hpp"
#include "../lib/file.h"
#include "../lib/font/font.h"
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
        {"--pages", "1,"}, {"--page-grid", "0x2"}, {"--page-grid", "2x3x4"}, {"--page-grid", "2"},
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
        rdt_engine_init(0); vector_engine = true;
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
    DomElement* svg_image(const char* attributes, const char* css = nullptr);
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
};
static LayoutViewNode* source_fragment(ViewTree* tree, DomNode* source, ViewFragmentRole role, bool box, bool last = false);
static uint32_t occurrence_page(LayoutViewNode* occurrence);
static void append_fragment_text(LayoutViewNode* node, StrBuf* text);
static uint32_t snapshot_pixel(const ImageSurface* surface, size_t x, size_t y);

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

DomElement* SecondaryViewTest::svg_image(const char* attributes, const char* css) {
    StrBuf* url = strbuf_new();
    if (!url) return nullptr;
    strbuf_append_str(url, "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' ");
    strbuf_append_str(url, attributes);
    strbuf_append_str(url, "%3E%3Crect width='100%25' height='100%25' fill='red'/%3E%3C/svg%3E");
    DomElement* result = image(css, nullptr, url->str);
    strbuf_free(url); return result;
}

static LayoutViewNode* source_image_fragment(ViewTree* tree, DomNode* source) {
    ViewNodeState* state = view_tree_node_state(tree, source, false);
    for (LayoutViewNode* node = state ? state->first_occurrence.get() : nullptr; node; node = node->next_occurrence)
        if (node->image_box) return node;
    return nullptr;
}

TEST_F(SecondaryViewTest, ResponsiveImagesKeepIndependentDensityAndReselectWithoutChangingSharedPixels) {
    ASSERT_TRUE(create_dir("temp/paged-media-responsive"));
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    for (int y = 0; y < before->height; y++) EXPECT_EQ(memcmp((uint8_t*)before->pixels + y * before->pitch,
        (uint8_t*)after->pixels + y * after->pitch, (size_t)before->width * 4), 0);
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
    rdt_engine_init(0); vector_engine = true;
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
    for (int y = 0; y < before->height; y++) EXPECT_EQ(memcmp((uint8_t*)before->pixels + y * before->pitch,
        (uint8_t*)after->pixels + y * after->pitch, (size_t)before->width * 4), 0);
    image_surface_destroy(before); image_surface_destroy(after);
    EXPECT_FLOAT_EQ(source->x, 11.25f); EXPECT_FLOAT_EQ(source->width, 640.0f);
}

TEST_F(SecondaryViewTest, UnequalSpanningGridsFailBeforePublishing) {
    stylesheet("@page { size: 240px 140px; margin: 10px } p { margin: 0; font: 10px/12px Arial } td { vertical-align: top }");
    DomElement* table = fixed_table(1); ASSERT_NE(table, nullptr);
    DomElement* cell = table->first_child->as_element()->first_child->as_element()->first_child->as_element();
    const struct { const char* attribute; const char* value; } cases[] = {{"colspan", "2"}, {"colspan", "1001"}};
    for (const auto& test : cases) {
        SCOPED_TRACE(test.value); ASSERT_TRUE(cell->set_attribute(test.attribute, test.value));
        ViewTree* tree = secondary(); PagedLayoutOptions options = paged_layout_options_default(); PagedLayoutDiagnostic diagnostic = {};
        EXPECT_EQ(layout_secondary_view(tree, &options, &diagnostic), TYPESET_INVALID);
        EXPECT_FALSE(tree->model->committed); EXPECT_EQ(tree->model->page_count, 0u); ASSERT_NE(diagnostic.reason, nullptr);
        ASSERT_TRUE(cell->remove_attribute(test.attribute));
    }
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    for (int y = 0; y < before->height; y++) EXPECT_EQ(memcmp((uint8_t*)before->pixels + y * before->pitch,
        (uint8_t*)after->pixels + y * after->pitch, (size_t)before->width * 4), 0);
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
    rdt_engine_init(0); vector_engine = true;
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
    ViewTree* excess = secondary(); EXPECT_EQ(layout_secondary_view(excess, &options, &diagnostic), TYPESET_INVALID);
    EXPECT_EQ(excess->model->page_count, 0u); EXPECT_FALSE(excess->model->committed);
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

static uint32_t occurrence_page(LayoutViewNode* node) {
    while (node && node->kind != LAYOUT_VIEW_PAGE) node = node->parent;
    return node ? ((ViewPageBox*)node)->page_number : 0;
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

static void append_fragment_text(LayoutViewNode* node, StrBuf* text) {
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    rdt_engine_init(0); vector_engine = true;
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
    if (a && b) {
        EXPECT_EQ(a->width, b->width); EXPECT_EQ(a->height, b->height);
        if (a->width == b->width && a->height == b->height)
            for (size_t y = 0; y < (size_t)a->height; y++)
                EXPECT_EQ(memcmp((uint8_t*)a->pixels + y * a->pitch,
                    (uint8_t*)b->pixels + y * b->pitch, (size_t)a->width * 4), 0) << "row " << y;
    }
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
