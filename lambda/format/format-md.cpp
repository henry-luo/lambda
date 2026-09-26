// format-md.cpp — Markdown output: the unified markup emitter's Markdown rules
// plus a CommonMark block layer, so that parsing the output yields the same
// document structure (Radiant_Design_Edit_Mode §5: headings, marks,
// destinations, lists, code fences and images must survive save and reopen).
//
// Block layout: blocks are written without a trailing newline and separated
// by one blank line (one newline inside a tight list item). A container's
// content — list item, block quote — is laid out into its own buffer first and
// then prefixed line by line. Inline content goes through the shared emitter;
// this file overrides hard breaks, images, code spans, links and raw HTML, and
// escapes text by position (block markers only matter at a line start).

#include "format.h"
#include "format-markup.h"
#include "../../lib/stringbuf.h"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

void format_markdown(StringBuf* sb, Item root_item) {
    format_markup(sb, root_item, &MARKDOWN_RULES);
}

String* format_markdown_string(Pool* pool, Item root_item) {
    return format_markup_string(pool, root_item, &MARKDOWN_RULES);
}

// ==============================================================================
// Helpers
// ==============================================================================

static bool md_tag_is(const char* tag, const char* name) {
    return tag && strcmp(tag, name) == 0;
}

static bool md_attr_is(const ElementReader& elem, const char* name, const char* value) {
    const char* actual = elem.get_attr_string(name);
    return actual && strcmp(actual, value) == 0;
}

static const char* md_chars(const StringBuf* sb) {
    return sb->str ? sb->str->chars : "";
}

static bool md_is_blank(const char* s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)s[i])) return false;
    }
    return true;
}

static bool md_item_is_blank_string(const ItemReader& item) {
    if (!item.isString()) return false;
    String* str = item.asString();
    return !str || md_is_blank(str->chars, str->len);
}

// Drop trailing blanks written since `floor`. A hard break left at the end of
// a block has no meaning in Markdown, so its backslash goes with the newline.
static void md_trim_trailing_blanks(StringBuf* sb, size_t floor) {
    while (sb->length > floor) {
        char c = md_chars(sb)[sb->length - 1];
        if (c != ' ' && c != '\t' && c != '\n') break;
        size_t len = sb->length - 1;
        if (c == '\n') {
            size_t slashes = 0;
            while (len - slashes > floor && md_chars(sb)[len - slashes - 1] == '\\') slashes++;
            if (slashes % 2 == 1) len--;
        }
        stringbuf_truncate(sb, len);
    }
}

// The child emitter a nested layout writes into; it shares the rules and pool.
static StringBuf* md_new_buffer(MarkupEmitter* em) {
    return stringbuf_new(em->pool());
}

// Collect the literal text of a code element, including nested code children.
static void md_collect_text(const ElementReader& elem, StringBuf* out) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            String* str = child.asString();
            if (str) stringbuf_append_str_n(out, str->chars, str->len);
        } else if (child.isElement()) {
            md_collect_text(child.asElement(), out);
        }
    }
}

static int md_longest_run(const char* s, size_t len, char c) {
    int longest = 0, run = 0;
    for (size_t i = 0; i < len; i++) {
        run = s[i] == c ? run + 1 : 0;
        if (run > longest) longest = run;
    }
    return longest;
}

// ==============================================================================
// Text escaping
// ==============================================================================

static bool md_at_line_start(const StringBuf* sb) {
    return sb->length == 0 || md_chars(sb)[sb->length - 1] == '\n';
}

// Escape a character that would open a block at the start of a line. Returns
// true when it consumed s[*i] (and possibly an ordered-list number run).
static bool md_escape_line_start(StringBuf* sb, const char* s, size_t len, size_t* i) {
    char c = s[*i];
    if (c == '#' || c == '>' || c == '-' || c == '+' || c == '=') {
        stringbuf_append_char(sb, '\\');
        stringbuf_append_char(sb, c);
        return true;
    }
    if (isdigit((unsigned char)c)) {
        size_t j = *i;
        while (j < len && j - *i < 9 && isdigit((unsigned char)s[j])) j++;
        bool marker = j < len && (s[j] == '.' || s[j] == ')') &&
            (j + 1 == len || s[j + 1] == ' ' || s[j + 1] == '\t' || s[j + 1] == '\n');
        if (!marker) return false;
        stringbuf_append_str_n(sb, s + *i, j - *i);
        stringbuf_append_char(sb, '\\');
        stringbuf_append_char(sb, s[j]);
        *i = j;
        return true;
    }
    return false;
}

void markdown_escape_text(StringBuf* sb, const char* s, size_t len) {
    if (!sb || !s) return;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        bool line_start = md_at_line_start(sb);
        if (c == ' ' || c == '\t') {
            // Markdown drops a line's indentation, and blanks before a line
            // break would turn it into a hard break: neither is content.
            if (line_start) continue;
            size_t j = i;
            while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j < len && s[j] == '\n') {
                i = j - 1;
                continue;
            }
            stringbuf_append_char(sb, c);
            continue;
        }
        if (line_start && md_escape_line_start(sb, s, len, &i)) continue;
        switch (c) {
        case '\\': case '`': case '*': case '_': case '[': case ']': case '~': case '$': case '^':
            stringbuf_append_char(sb, '\\');
            stringbuf_append_char(sb, c);
            break;
        case '<':
            // only what could open a tag, comment, or autolink
            if (i + 1 < len && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '/' ||
                                s[i + 1] == '!' || s[i + 1] == '?')) {
                stringbuf_append_char(sb, '\\');
            }
            stringbuf_append_char(sb, c);
            break;
        case '&':
            // only what could open an entity or numeric character reference
            if (i + 1 < len && (isalnum((unsigned char)s[i + 1]) || s[i + 1] == '#')) {
                stringbuf_append_char(sb, '\\');
            }
            stringbuf_append_char(sb, c);
            break;
        default:
            stringbuf_append_char(sb, c);
            break;
        }
    }
}

// ==============================================================================
// Block classification
// ==============================================================================

enum MdBlockKind {
    MD_INLINE = 0,   // laid out inside a paragraph
    MD_SKIP,         // no Markdown source (parser companion data, head metadata)
    MD_CONTAINER,    // children are blocks
    MD_PARAGRAPH,
    MD_HEADING,
    MD_LIST,
    MD_BLOCKQUOTE,
    MD_CODE,
    MD_HR,
    MD_TABLE,
    MD_HTML_BLOCK,
    MD_MATH_BLOCK,
};

static bool md_is_code_block(const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (md_tag_is(tag, "pre") || md_tag_is(tag, "code_block")) return true;
    if (!md_tag_is(tag, "code")) return false;
    const char* language = elem.get_attr_string("language");
    return md_attr_is(elem, "type", "block") || (language && language[0]);
}

static MdBlockKind md_block_kind(const ElementReader& elem) {
    const char* tag = elem.tagName();
    if (!tag) return MD_INLINE;
    // html-dom is the parser's DOM projection of raw HTML that html-block and
    // raw-html already keep verbatim; emitting it would duplicate the content.
    if (md_tag_is(tag, "html-dom") || md_tag_is(tag, "meta") || md_tag_is(tag, "head") ||
        md_tag_is(tag, "script") || md_tag_is(tag, "style") || md_tag_is(tag, "input")) {
        return MD_SKIP;
    }
    if (md_tag_is(tag, "doc") || md_tag_is(tag, "document") || md_tag_is(tag, "body") ||
        md_tag_is(tag, "html") || md_tag_is(tag, "div") || md_tag_is(tag, "section") ||
        md_tag_is(tag, "article") || md_tag_is(tag, "main") || md_tag_is(tag, "header") ||
        md_tag_is(tag, "footer") || md_tag_is(tag, "nav") || md_tag_is(tag, "aside") ||
        md_tag_is(tag, "figure") || md_tag_is(tag, "li") || md_tag_is(tag, "list_item")) {
        return MD_CONTAINER;
    }
    if (md_tag_is(tag, "p") || md_tag_is(tag, "paragraph") || md_tag_is(tag, "figcaption")) {
        return MD_PARAGRAPH;
    }
    if (is_heading_tag(tag)) return MD_HEADING;
    if (md_tag_is(tag, "ul") || md_tag_is(tag, "ol")) return MD_LIST;
    if (md_tag_is(tag, "blockquote")) return MD_BLOCKQUOTE;
    if (md_is_code_block(elem)) return MD_CODE;
    if (md_tag_is(tag, "hr")) return MD_HR;
    if (md_tag_is(tag, "table")) return MD_TABLE;
    if (md_tag_is(tag, "html-block")) return MD_HTML_BLOCK;
    if (md_tag_is(tag, "math") && md_attr_is(elem, "type", "block")) return MD_MATH_BLOCK;
    return MD_INLINE;
}

static void md_emit_block(MarkupEmitter* em, const ElementReader& elem, MdBlockKind kind,
                          bool alternate_marker);

// ==============================================================================
// Block sequences
// ==============================================================================

// Lay out `container`'s children as blocks. Consecutive inline children form
// one implicit paragraph; a block or run that writes nothing takes its
// separator back, so empty paragraphs leave no stray blank lines.
static void md_emit_blocks(MarkupEmitter* em, const ElementReader& container, bool tight) {
    StringBuf* sb = em->output();
    const char* separator = tight ? "\n" : "\n\n";
    bool have_block = false;
    bool in_run = false;
    size_t run_sep_at = 0, run_start = 0;
    const char* prev_list = nullptr;
    bool prev_alternate = false;
    em->block_nesting++;

    auto finish_run = [&]() {
        md_trim_trailing_blanks(sb, run_start);
        if (sb->length == run_start) stringbuf_truncate(sb, run_sep_at);
        else have_block = true;
        in_run = false;
        prev_list = nullptr;
    };

    auto it = container.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isNull()) continue;
        ElementReader elem;
        MdBlockKind kind = MD_INLINE;
        if (child.isElement()) {
            elem = child.asElement();
            kind = md_block_kind(elem);
        }
        if (kind == MD_SKIP) continue;
        if (kind == MD_INLINE) {
            // whitespace between blocks carries no content
            if (!in_run && md_item_is_blank_string(child)) continue;
            if (!in_run) {
                run_sep_at = sb->length;
                if (have_block) stringbuf_append_str(sb, separator);
                run_start = sb->length;
                in_run = true;
            }
            em->format_item(child);
            continue;
        }
        if (in_run) finish_run();
        size_t sep_at = sb->length;
        if (have_block) stringbuf_append_str(sb, separator);
        size_t block_start = sb->length;
        // Adjacent lists of one kind merge unless the second changes marker.
        bool alternate = kind == MD_LIST && prev_list &&
            md_tag_is(prev_list, elem.tagName()) && !prev_alternate;
        md_emit_block(em, elem, kind, alternate);
        md_trim_trailing_blanks(sb, block_start);
        if (sb->length == block_start) {
            stringbuf_truncate(sb, sep_at);
        } else {
            have_block = true;
            prev_list = kind == MD_LIST ? elem.tagName() : nullptr;
            prev_alternate = alternate;
        }
    }
    if (in_run) finish_run();
    em->block_nesting--;
}

// Write `body` after `marker`, indenting continuation lines by the marker
// width so they stay inside the list item. Blank lines stay empty.
static void md_append_list_item(StringBuf* sb, const char* marker, const StringBuf* body) {
    size_t indent = strlen(marker);
    if (body->length == 0) {
        // an empty item is its marker alone
        stringbuf_append_str_n(sb, marker, indent > 0 ? indent - 1 : 0);
        return;
    }
    stringbuf_append_str(sb, marker);
    const char* s = md_chars(body);
    bool line_start = false;
    for (size_t i = 0; i < body->length; i++) {
        if (line_start && s[i] != '\n') stringbuf_append_char_n(sb, ' ', indent);
        stringbuf_append_char(sb, s[i]);
        line_start = s[i] == '\n';
    }
}

// Prefix every line of `body` with "> " (a blank line gets ">" alone).
static void md_append_quoted(StringBuf* sb, const StringBuf* body) {
    const char* s = md_chars(body);
    bool line_start = true;
    for (size_t i = 0; i < body->length; i++) {
        if (line_start) stringbuf_append_str(sb, s[i] == '\n' ? ">" : "> ");
        stringbuf_append_char(sb, s[i]);
        line_start = s[i] == '\n';
    }
    if (body->length == 0) stringbuf_append_char(sb, '>');
}

// ==============================================================================
// Lists
// ==============================================================================

static bool md_is_task_item(const ElementReader& item) {
    if (item.has_attr("data-checked")) return true;
    const char* cls = item.get_attr_string("class");
    return cls && strstr(cls, "task-list-item");
}

static bool md_task_checked(const ElementReader& item) {
    if (md_attr_is(item, "data-checked", "true")) return true;
    ElementReader box = item.findChildElement("input");
    return box.isValid() && box.has_attr("checked");
}

// A tight list cannot keep two adjacent paragraphs in one item apart, so a
// list is written loose when the parser said so or any item needs it.
static bool md_list_is_loose(const ElementReader& list) {
    if (md_attr_is(list, "loose", "true")) return true;
    enum { UNIT_NONE, UNIT_RUN, UNIT_PARA, UNIT_OTHER };
    auto items = list.childElements();
    ElementReader item;
    while (items.next(&item)) {
        int prev = UNIT_NONE;
        auto it = item.children();
        ItemReader child;
        while (it.next(&child)) {
            if (child.isNull() || md_item_is_blank_string(child)) continue;
            MdBlockKind kind = child.isElement() ? md_block_kind(child.asElement()) : MD_INLINE;
            if (kind == MD_SKIP) continue;
            if (kind == MD_INLINE) {
                if (prev == UNIT_PARA) return true;
                prev = UNIT_RUN;
            } else if (kind == MD_PARAGRAPH) {
                if (prev == UNIT_RUN || prev == UNIT_PARA) return true;
                prev = UNIT_PARA;
            } else {
                prev = UNIT_OTHER;
            }
        }
    }
    return false;
}

static void md_emit_list(MarkupEmitter* em, const ElementReader& list, bool alternate_marker) {
    StringBuf* sb = em->output();
    bool ordered = md_tag_is(list.tagName(), "ol");
    long number = 1;
    const char* start = list.get_attr_string("start");
    if (ordered && start && start[0]) {
        number = strtol(start, nullptr, 10);
        if (number < 0) number = 0;
    }
    bool loose = md_list_is_loose(list);
    bool first = true;
    auto items = list.childElements();
    ElementReader item;
    while (items.next(&item)) {
        const char* tag = item.tagName();
        if (!md_tag_is(tag, "li") && !md_tag_is(tag, "list_item")) continue;
        if (!first) stringbuf_append_str(sb, loose ? "\n\n" : "\n");
        first = false;
        char marker[32];
        if (ordered) {
            snprintf(marker, sizeof(marker), "%ld%c ", number++, alternate_marker ? ')' : '.');
        } else {
            snprintf(marker, sizeof(marker), "%c ", alternate_marker ? '*' : '-');
        }
        StringBuf* body = md_new_buffer(em);
        MarkupEmitter item_emitter(em->rules(), em->pool(), body);
        item_emitter.block_nesting = em->block_nesting;
        if (md_is_task_item(item)) {
            stringbuf_append_str(body, md_task_checked(item) ? "[x] " : "[ ] ");
        }
        md_emit_blocks(&item_emitter, item, !loose);
        md_trim_trailing_blanks(body, 0);
        md_append_list_item(sb, marker, body);
    }
}

// ==============================================================================
// Headings, quotes, code, math, raw HTML
// ==============================================================================

// Render inline children into a fresh buffer (for single-line constructs).
static StringBuf* md_render_inline(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* body = md_new_buffer(em);
    MarkupEmitter inline_emitter(em->rules(), em->pool(), body);
    inline_emitter.block_nesting = em->block_nesting + 1;
    // A leading "|" marks the buffer as mid-line, so text is escaped as
    // inline content rather than as the start of a block.
    stringbuf_append_char(body, '|');
    inline_emitter.format_children(elem);
    return body;
}

// Copy an inline rendering as one line: breaks become spaces (a hard break's
// backslash goes too), and `escape_pipes` escapes table-cell delimiters.
static void md_append_single_line(StringBuf* sb, const StringBuf* body, bool escape_pipes) {
    const char* s = md_chars(body) + 1;  // skip the mid-line marker
    size_t len = body->length > 0 ? body->length - 1 : 0;
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    size_t i = 0;
    while (i < len && isspace((unsigned char)s[i])) i++;
    size_t slashes = 0;
    for (; i < len; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < len && s[i + 1] == '\n' && slashes % 2 == 0) {
            slashes = 0;
            continue;  // hard break marker
        }
        if (c == '\n') c = ' ';
        if (c == '|' && escape_pipes && slashes % 2 == 0) stringbuf_append_char(sb, '\\');
        stringbuf_append_char(sb, c);
        slashes = c == '\\' ? slashes + 1 : 0;
    }
}

static void md_emit_heading(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* sb = em->output();
    int level = get_heading_level(elem, 1);
    stringbuf_append_char_n(sb, '#', (size_t)level);
    size_t text_start = sb->length + 1;
    stringbuf_append_char(sb, ' ');
    StringBuf* body = md_render_inline(em, elem);
    md_append_single_line(sb, body, false);
    if (sb->length == text_start) {
        stringbuf_truncate(sb, text_start - 1);  // an empty heading is "#" alone
        return;
    }
    // A closing run of '#' after a space would be read as the closing sequence.
    size_t end = sb->length;
    size_t run = end;
    while (run > text_start && md_chars(sb)[run - 1] == '#') run--;
    if (run < end && (run == text_start || md_chars(sb)[run - 1] == ' ')) {
        StringBuf* tail = md_new_buffer(em);
        stringbuf_append_str_n(tail, md_chars(sb) + run, end - run);
        stringbuf_truncate(sb, run);
        stringbuf_append_char(sb, '\\');
        stringbuf_append_str_n(sb, md_chars(tail), tail->length);
    }
}

static void md_emit_blockquote(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* body = md_new_buffer(em);
    MarkupEmitter quote_emitter(em->rules(), em->pool(), body);
    quote_emitter.block_nesting = em->block_nesting;
    md_emit_blocks(&quote_emitter, elem, false);
    md_trim_trailing_blanks(body, 0);
    md_append_quoted(em->output(), body);
}

static const char* md_code_language(const ElementReader& elem) {
    const char* language = elem.get_attr_string("language");
    if (language && language[0]) return language;
    language = elem.get_attr_string("lang");
    if (language && language[0]) return language;
    // <pre><code class="language-x"> from HTML
    ElementReader code = elem.findChildElement("code");
    const char* cls = code.isValid() ? code.get_attr_string("class") : nullptr;
    if (cls && strncmp(cls, "language-", 9) == 0 && cls[9]) return cls + 9;
    return nullptr;
}

// Fence with more backticks than any run in the content; the closing fence
// line supplies the final newline, so one trailing newline is not repeated.
static void md_emit_code_block(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* sb = em->output();
    StringBuf* text = md_new_buffer(em);
    md_collect_text(elem, text);
    const char* s = md_chars(text);
    size_t len = text->length;
    if (len > 0 && s[len - 1] == '\n') len--;
    int fence = md_longest_run(s, len, '`') + 1;
    if (fence < 3) fence = 3;
    stringbuf_append_char_n(sb, '`', (size_t)fence);
    const char* language = md_code_language(elem);
    if (language) stringbuf_append_str(sb, language);
    stringbuf_append_char(sb, '\n');
    if (len > 0) {
        stringbuf_append_str_n(sb, s, len);
        stringbuf_append_char(sb, '\n');
    }
    stringbuf_append_char_n(sb, '`', (size_t)fence);
}

static void md_emit_math_block(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* sb = em->output();
    StringBuf* text = md_new_buffer(em);
    md_collect_text(elem, text);
    const char* s = md_chars(text);
    size_t start = 0, len = text->length;
    while (start < len && s[start] == '\n') start++;
    while (len > start && s[len - 1] == '\n') len--;
    // one-line display math keeps the usual `$$x$$` spelling; only
    // multi-line content needs the fences on lines of their own
    bool one_line = len > start && memchr(s + start, '\n', len - start) == NULL;
    stringbuf_append_str(sb, one_line ? "$$" : "$$\n");
    stringbuf_append_str_n(sb, s + start, len - start);
    stringbuf_append_str(sb, one_line ? "$$" : "\n$$");
}

static void md_emit_raw(StringBuf* sb, const ElementReader& elem) {
    StringBuf* text = stringbuf_new(sb->pool);
    md_collect_text(elem, text);
    stringbuf_append_str_n(sb, md_chars(text), text->length);
}

// ==============================================================================
// Tables (GFM pipe tables)
// ==============================================================================

struct MdTableContext {
    MarkupEmitter* em;
    int columns;
    char aligns[64];  // 'l', 'r', 'c' or '\0' per column
};

static char md_cell_align(const ElementReader& cell) {
    const char* align = cell.get_attr_string("align");
    if (!align) return '\0';
    if (strcmp(align, "left") == 0) return 'l';
    if (strcmp(align, "right") == 0) return 'r';
    if (strcmp(align, "center") == 0) return 'c';
    return '\0';
}

static void md_table_measure_row(StringBuf*, const ElementReader& row, int row_index,
                                 bool, void* ctx) {
    MdTableContext* table = (MdTableContext*)ctx;
    int column = 0;
    auto cells = row.childElements();
    ElementReader cell;
    while (cells.next(&cell)) {
        if (column < 64 && (row_index == 0 || !table->aligns[column])) {
            table->aligns[column] = md_cell_align(cell);
        }
        column++;
    }
    if (column > table->columns) table->columns = column;
}

static void md_table_emit_row(StringBuf* sb, const ElementReader& row, int row_index,
                              bool, void* ctx) {
    MdTableContext* table = (MdTableContext*)ctx;
    if (row_index > 0) stringbuf_append_char(sb, '\n');
    stringbuf_append_char(sb, '|');
    int column = 0;
    auto cells = row.childElements();
    ElementReader cell;
    while (cells.next(&cell)) {
        stringbuf_append_char(sb, ' ');
        md_append_single_line(sb, md_render_inline(table->em, cell), true);
        stringbuf_append_str(sb, " |");
        column++;
    }
    // GFM drops cells beyond the header, so every row carries every column
    for (; column < table->columns; column++) stringbuf_append_str(sb, "  |");
    if (row_index == 0) {
        stringbuf_append_str(sb, "\n|");
        for (int i = 0; i < table->columns; i++) {
            char align = i < 64 ? table->aligns[i] : '\0';
            stringbuf_append_str(sb, align == 'l' ? " :-- |" : align == 'r' ? " --: |"
                                   : align == 'c' ? " :-: |" : " --- |");
        }
    }
}

// The first row is the header row, as GFM requires of every table.
static void md_emit_table(MarkupEmitter* em, const ElementReader& elem) {
    MdTableContext table = {};
    table.em = em;
    iterate_table_rows(elem, em->output(), md_table_measure_row, &table);
    if (table.columns == 0) return;
    iterate_table_rows(elem, em->output(), md_table_emit_row, &table);
}

// ==============================================================================
// Block dispatch
// ==============================================================================

static void md_emit_block(MarkupEmitter* em, const ElementReader& elem, MdBlockKind kind,
                          bool alternate_marker) {
    StringBuf* sb = em->output();
    switch (kind) {
    case MD_CONTAINER: md_emit_blocks(em, elem, false); break;
    case MD_PARAGRAPH: em->format_children(elem); break;
    case MD_HEADING: md_emit_heading(em, elem); break;
    case MD_LIST: md_emit_list(em, elem, alternate_marker); break;
    case MD_BLOCKQUOTE: md_emit_blockquote(em, elem); break;
    case MD_CODE: md_emit_code_block(em, elem); break;
    case MD_HR: stringbuf_append_str(sb, "---"); break;
    case MD_TABLE: md_emit_table(em, elem); break;
    case MD_HTML_BLOCK: md_emit_raw(sb, elem); break;
    case MD_MATH_BLOCK: md_emit_math_block(em, elem); break;
    default: break;
    }
}

// ==============================================================================
// Inline overrides
// ==============================================================================

// A link or image destination; pointy brackets when it has blanks or parens.
static void md_append_destination(StringBuf* sb, const char* url) {
    url = url ? url : "";
    bool pointy = !url[0] || strpbrk(url, " \t()<>") != nullptr;
    if (!pointy) {
        stringbuf_append_str(sb, url);
        return;
    }
    stringbuf_append_char(sb, '<');
    for (const char* p = url; *p; p++) {
        if (*p == '<' || *p == '>' || *p == '\\') stringbuf_append_char(sb, '\\');
        stringbuf_append_char(sb, *p == '\n' ? ' ' : *p);
    }
    stringbuf_append_char(sb, '>');
}

static void md_append_title(StringBuf* sb, const char* title) {
    if (!title || !title[0]) return;
    stringbuf_append_str(sb, " \"");
    for (const char* p = title; *p; p++) {
        if (*p == '"' || *p == '\\') stringbuf_append_char(sb, '\\');
        stringbuf_append_char(sb, *p);
    }
    stringbuf_append_char(sb, '"');
}

// A link whose only content is its own URL (or the address of a mailto:)
// was an autolink; writing it back as one keeps its text a single run.
static const char* md_autolink_target(const ElementReader& elem, const char* href) {
    if (!href || !href[0] || elem.childCount() != 1 || strpbrk(href, " \t<>")) return nullptr;
    ItemReader only = elem.childAt(0);
    const char* text = only.isString() ? only.cstring() : nullptr;
    if (!text) return nullptr;
    if (strcmp(text, href) == 0 && strchr(href, ':')) return href;
    if (strncmp(href, "mailto:", 7) == 0 && strcmp(text, href + 7) == 0) return text;
    return nullptr;
}

static void md_emit_link(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* sb = em->output();
    const char* autolink = md_autolink_target(elem, elem.get_attr_string("href"));
    if (autolink && !elem.has_attr("title")) {
        stringbuf_append_char(sb, '<');
        stringbuf_append_str(sb, autolink);
        stringbuf_append_char(sb, '>');
        return;
    }
    stringbuf_append_char(sb, '[');
    em->format_children(elem);
    stringbuf_append_str(sb, "](");
    md_append_destination(sb, elem.get_attr_string("href"));
    md_append_title(sb, elem.get_attr_string("title"));
    stringbuf_append_char(sb, ')');
}

static void md_emit_image(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* sb = em->output();
    stringbuf_append_str(sb, "![");
    const char* alt = elem.get_attr_string("alt");
    if (alt) markdown_escape_text(sb, alt, strlen(alt));
    stringbuf_append_str(sb, "](");
    md_append_destination(sb, elem.get_attr_string("src"));
    md_append_title(sb, elem.get_attr_string("title"));
    stringbuf_append_char(sb, ')');
}

// A code span fenced by one more backtick than its longest run; padding
// keeps edge backticks and edge spaces that the parser would otherwise strip.
static void md_emit_code_span(MarkupEmitter* em, const ElementReader& elem) {
    StringBuf* sb = em->output();
    StringBuf* text = md_new_buffer(em);
    md_collect_text(elem, text);
    const char* s = md_chars(text);
    size_t len = text->length;
    if (len == 0) return;
    int fence = md_longest_run(s, len, '`') + 1;
    bool all_spaces = md_is_blank(s, len);
    bool pad = s[0] == '`' || s[len - 1] == '`' ||
        (!all_spaces && s[0] == ' ' && s[len - 1] == ' ');
    stringbuf_append_char_n(sb, '`', (size_t)fence);
    if (pad) stringbuf_append_char(sb, ' ');
    for (size_t i = 0; i < len; i++) stringbuf_append_char(sb, s[i] == '\n' ? ' ' : s[i]);
    if (pad) stringbuf_append_char(sb, ' ');
    stringbuf_append_char_n(sb, '`', (size_t)fence);
}

bool markdown_custom_handler(void* ctx, StringBuf* sb, const ElementReader& elem) {
    MarkupEmitter* em = (MarkupEmitter*)ctx;
    MdBlockKind kind = md_block_kind(elem);
    if (kind == MD_SKIP) return true;
    if (kind != MD_INLINE) {
        // Reached through the generic dispatch: the output root, or a block
        // inside inline content. The root owns the document's final newline.
        bool root = em->block_nesting == 0;
        size_t start = sb->length;
        em->block_nesting++;
        md_emit_block(em, elem, kind, false);
        em->block_nesting--;
        if (root) {
            md_trim_trailing_blanks(sb, start);
            if (sb->length > start) stringbuf_append_char(sb, '\n');
        }
        return true;
    }
    const char* tag = elem.tagName();
    if (md_tag_is(tag, "br")) {
        stringbuf_append_str(sb, "\\\n");
        return true;
    }
    if (md_tag_is(tag, "img")) {
        md_emit_image(em, elem);
        return true;
    }
    if (md_tag_is(tag, "code")) {
        md_emit_code_span(em, elem);
        return true;
    }
    if (md_tag_is(tag, "a")) {
        md_emit_link(em, elem);
        return true;
    }
    if (md_tag_is(tag, "raw-html")) {
        md_emit_raw(sb, elem);
        return true;
    }
    if (md_tag_is(tag, "footnote-ref")) {
        const char* ref = elem.get_attr_string("ref");
        stringbuf_append_str(sb, "[^");
        if (ref) stringbuf_append_str(sb, ref);
        stringbuf_append_char(sb, ']');
        return true;
    }
    return false;
}
