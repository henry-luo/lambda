#pragma once
#include "css_parser.hpp"

enum CssPagePseudo : uint8_t {
    CSS_PAGE_FIRST = 1, CSS_PAGE_LEFT = 2, CSS_PAGE_RIGHT = 4, CSS_PAGE_BLANK = 8,
};

struct CssPageSelector {
    const char* name;
    uint8_t pseudos;
    uint32_t named_specificity, state_specificity, side_specificity;
};

enum CssPageMarginBox : uint8_t {
    CSS_PAGE_TOP_LEFT_CORNER, CSS_PAGE_TOP_LEFT, CSS_PAGE_TOP_CENTER, CSS_PAGE_TOP_RIGHT,
    CSS_PAGE_TOP_RIGHT_CORNER, CSS_PAGE_RIGHT_TOP, CSS_PAGE_RIGHT_MIDDLE, CSS_PAGE_RIGHT_BOTTOM,
    CSS_PAGE_BOTTOM_RIGHT_CORNER, CSS_PAGE_BOTTOM_RIGHT, CSS_PAGE_BOTTOM_CENTER, CSS_PAGE_BOTTOM_LEFT,
    CSS_PAGE_BOTTOM_LEFT_CORNER, CSS_PAGE_LEFT_BOTTOM, CSS_PAGE_LEFT_MIDDLE, CSS_PAGE_LEFT_TOP,
    CSS_PAGE_MARGIN_BOX_COUNT,
};

enum CssPageAreaKind : uint8_t { CSS_PAGE_AREA_PAGE, CSS_PAGE_AREA_MARGIN, CSS_PAGE_AREA_FOOTNOTE };
struct CssPageAreaRule {
    CssPageAreaKind kind;
    CssPageMarginBox box;
    CssDeclaration** declarations;
    size_t declaration_count;
};

struct CssPageRule {
    const char* selector_text; // canonical prelude retains repeated pseudo classes

    CssPageSelector* selectors;
    size_t selector_count;
    CssDeclaration** declarations;
    size_t declaration_count;
    CssPageAreaRule* areas;
    size_t area_count;
};

// Parsing shares the normal tokenizer/value grammar and keeps authored rules immutable after construction.
CssPageRule* css_page_rule_parse(const CssToken* tokens, int selector_start,
    int selector_end, int body_start, int body_end, Pool* pool);
bool css_page_selector_matches(const CssPageSelector* selector, const char* page_name,
                              uint8_t page_pseudos);
int css_page_specificity_compare(const CssPageSelector* a, const CssPageSelector* b);
const char* css_page_margin_box_name(CssPageMarginBox box);

CssPageRule* css_page_selectors_parse_text(const char* text, size_t length, Pool* pool);
