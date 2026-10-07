#include "css_paged_media.hpp"
#include "css_formatter.hpp"
#include "../../../lib/mem_grow.hpp"
#include "../../../lib/str.h"
#include <string.h>

static const char* page_margin_names[CSS_PAGE_MARGIN_BOX_COUNT] = {
    "top-left-corner", "top-left", "top-center", "top-right", "top-right-corner",
    "right-top", "right-middle", "right-bottom", "bottom-right-corner", "bottom-right",
    "bottom-center", "bottom-left", "bottom-left-corner", "left-bottom", "left-middle", "left-top",
};

const char* css_page_margin_box_name(CssPageMarginBox box) {
    return box < CSS_PAGE_MARGIN_BOX_COUNT ? page_margin_names[box] : nullptr;
}

static int page_space(const CssToken* tokens, int pos, int end, bool whitespace = true) {
    while (pos < end && ((whitespace && tokens[pos].type == CSS_TOKEN_WHITESPACE) ||
                        tokens[pos].type == CSS_TOKEN_COMMENT)) pos++;
    return pos;
}

static bool page_token_is(const CssToken* token, const char* name) {
    const char* value = token->value;
    if (value && token->type == CSS_TOKEN_AT_KEYWORD && value[0] == '@') value++;
    return value && str_icmp_cstr(value, name) == 0;
}

static bool page_selectors_parse(CssPageRule* rule, const CssToken* tokens,
                                int start, int end, Pool* pool) {
    size_t capacity = 0;
    int pos = page_space(tokens, start, end);
    if (pos < end && tokens[pos].type != CSS_TOKEN_IDENT && tokens[pos].type != CSS_TOKEN_COLON) return false;
    do {
        CssPageSelector selector = {};
        if (pos < end && tokens[pos].type == CSS_TOKEN_IDENT) {
            if (page_token_is(&tokens[pos], "auto")) return false;
            selector.name = pool_dup_n(pool, tokens[pos].value, strlen(tokens[pos].value));
            if (!selector.name) return false;
            selector.named_specificity = 1;
            pos = page_space(tokens, pos + 1, end, false);
        }
        while (pos < end && tokens[pos].type == CSS_TOKEN_COLON) {
            pos = page_space(tokens, pos + 1, end, false);
            if (pos >= end || tokens[pos].type != CSS_TOKEN_IDENT) return false;
            uint8_t flag = 0;
            if (page_token_is(&tokens[pos], "first")) flag = CSS_PAGE_FIRST;
            else if (page_token_is(&tokens[pos], "left")) flag = CSS_PAGE_LEFT;
            else if (page_token_is(&tokens[pos], "right")) flag = CSS_PAGE_RIGHT;
            else if (page_token_is(&tokens[pos], "blank")) flag = CSS_PAGE_BLANK;
            else return false;
            selector.pseudos |= flag;
            if (flag == CSS_PAGE_FIRST || flag == CSS_PAGE_BLANK) selector.state_specificity++;
            else selector.side_specificity++;
            pos = page_space(tokens, pos + 1, end, false);
        }
        if (!lam::pool_grow_array(pool, &rule->selectors, &capacity,
                                  rule->selector_count + 1, 4)) return false;
        rule->selectors[rule->selector_count++] = selector;
        pos = page_space(tokens, pos, end);
        if (pos == end) {
            rule->selector_text = css_format_page_selector_tokens(tokens, start, end, pool);
            return rule->selector_text != nullptr;
        }
        if (tokens[pos].type != CSS_TOKEN_COMMA) return false;
        pos = page_space(tokens, pos + 1, end);
        if (pos == end || (tokens[pos].type != CSS_TOKEN_IDENT &&
                          tokens[pos].type != CSS_TOKEN_COLON)) return false;
    } while (pos < end);
    return false;
}

CssPageRule* css_page_selectors_parse_text(const char* text, size_t length, Pool* pool) {
    size_t count = 0;
    CssToken* tokens = css_tokenize(text, length, pool, &count);
    if (!tokens || !count) return nullptr;
    if (tokens[count - 1].type == CSS_TOKEN_EOF) count--;
    CssPageRule* rule = (CssPageRule*)pool_calloc(pool, sizeof(CssPageRule));
    return rule && page_selectors_parse(rule, tokens, 0, (int)count, pool) ? rule : nullptr;
}

static bool page_declarations_parse(const CssToken* tokens, int start, int end,
        Pool* pool, CssDeclaration*** declarations, size_t* count) {
    size_t capacity = 0;
    int pos = start;
    while ((pos = page_space(tokens, pos, end)) < end) {
        int before = pos;
        if (tokens[pos].type == CSS_TOKEN_AT_KEYWORD) {
            // Unknown nested rules cannot consume a later top-level descriptor.
            int braces = 0;
            do {
                if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) braces++;
                else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE && braces > 0) {
                    braces--;
                    if (!braces) { pos++; break; }
                } else if (tokens[pos].type == CSS_TOKEN_SEMICOLON && !braces) { pos++; break; }
                pos++;
            } while (pos < end);
            continue;
        }
        CssDeclaration* declaration = css_parse_declaration_from_tokens(tokens, &pos, end, pool);
        if (declaration) {
            if (!lam::pool_grow_array(pool, declarations, &capacity, *count + 1, 8)) return false;
            (*declarations)[(*count)++] = declaration;
        }
        if (pos < end && tokens[pos].type == CSS_TOKEN_SEMICOLON) pos++;
        if (pos <= before) pos = before + 1;
    }
    return true;
}

CssPageRule* css_page_rule_parse(const CssToken* tokens, int selector_start,
        int selector_end, int body_start, int body_end, Pool* pool) {
    if (!tokens || !pool || selector_start < 0 || selector_end < selector_start ||
        body_start < selector_end || body_end < body_start) return nullptr;
    CssPageRule* rule = (CssPageRule*)pool_calloc(pool, sizeof(CssPageRule));
    if (!rule || !page_selectors_parse(rule, tokens, selector_start, selector_end, pool) ||
        !page_declarations_parse(tokens, body_start, body_end, pool,
                                 &rule->declarations, &rule->declaration_count)) return nullptr;
    size_t capacity = 0;
    for (int pos = body_start; pos < body_end;) {
        if (tokens[pos].type != CSS_TOKEN_AT_KEYWORD) { pos++; continue; }
        int name_pos = pos++;
        int box = -1;
        bool footnote = page_token_is(&tokens[name_pos], "footnote");
        for (int i = 0; i < CSS_PAGE_MARGIN_BOX_COUNT; i++) {
            if (page_token_is(&tokens[name_pos], page_margin_names[i])) { box = i; break; }
        }
        int opening = page_space(tokens, pos, body_end);
        if (opening >= body_end || tokens[opening].type != CSS_TOKEN_LEFT_BRACE) {
            // Consume the entire unknown prelude; a known box admits no prelude.
            while (opening < body_end && tokens[opening].type != CSS_TOKEN_LEFT_BRACE &&
                   tokens[opening].type != CSS_TOKEN_SEMICOLON) opening++;
            box = -1;
            footnote = false;
        }
        if (opening >= body_end || tokens[opening].type != CSS_TOKEN_LEFT_BRACE) { pos = opening + 1; continue; }
        pos = opening + 1;
        int depth = 1;
        while (pos < body_end && depth) {
            if (tokens[pos].type == CSS_TOKEN_LEFT_BRACE) depth++;
            else if (tokens[pos].type == CSS_TOKEN_RIGHT_BRACE) depth--;
            pos++;
        }
        if ((!footnote && box < 0) || depth) continue;
        CssPageAreaRule area = {};
        area.kind = footnote ? CSS_PAGE_AREA_FOOTNOTE : CSS_PAGE_AREA_MARGIN;
        if (!footnote) area.box = static_cast<CssPageMarginBox>(box);
        if (!page_declarations_parse(tokens, opening + 1, pos - 1, pool,
                                     &area.declarations, &area.declaration_count) ||
            !lam::pool_grow_array(pool, &rule->areas, &capacity, rule->area_count + 1, 4)) return nullptr;
        rule->areas[rule->area_count++] = area;
    }
    return rule;
}

bool css_page_selector_matches(const CssPageSelector* selector, const char* page_name,
                              uint8_t page_pseudos) {
    return selector && (!selector->name || (page_name && strcmp(selector->name, page_name) == 0)) &&
        (selector->pseudos & page_pseudos) == selector->pseudos;
}

int css_page_specificity_compare(const CssPageSelector* a, const CssPageSelector* b) {
    if (a->named_specificity != b->named_specificity) return a->named_specificity < b->named_specificity ? -1 : 1;
    if (a->state_specificity != b->state_specificity) return a->state_specificity < b->state_specificity ? -1 : 1;
    if (a->side_specificity != b->side_specificity) return a->side_specificity < b->side_specificity ? -1 : 1;
    return 0;
}
