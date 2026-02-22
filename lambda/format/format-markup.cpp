#include "format-markup.h"
#include "format-utils.h"
#include "format-utils.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// ==============================================================================
// MarkupEmitter — unified markup formatter driven by MarkupOutputRules
// ==============================================================================

class MarkupEmitter : public FormatterContextCpp {
public:
    MarkupEmitter(const MarkupOutputRules* rules, Pool* pool, StringBuf* output)
        : FormatterContextCpp(pool, output, 50)
        , rules_(rules)
        , list_depth_(0)
    {}

    void format_item(const ItemReader& item);
    void format_element(const ElementReader& elem);
    void format_children(const ElementReader& elem);
    void format_children_raw(const ElementReader& elem);
    void format_text(String* str);
    void format_text_cstr(const char* text);

    const MarkupOutputRules* rules() const { return rules_; }
    int list_depth() const { return list_depth_; }

private:
    const MarkupOutputRules* rules_;
    int list_depth_;

    // element handlers
    void emit_heading(const ElementReader& elem);
    void emit_inline(const ElementReader& elem, const char* open, const char* close);
    void emit_link(const ElementReader& elem);
    void emit_image(const ElementReader& elem);
    void emit_list(const ElementReader& elem, bool ordered, int depth);
    void emit_list_item(const ElementReader& elem, bool ordered, int depth, int index);
    void emit_code_block(const ElementReader& elem);
    void emit_blockquote(const ElementReader& elem);
    void emit_paragraph(const ElementReader& elem);
    void emit_hr();
    void emit_br();

    // tag matching helpers
    bool match_inline_tag(const char* tag, const char* open, const char* close);
    bool is_container_tag(const char* tag) const;
    bool is_skip_tag(const char* tag) const;
};

// ==============================================================================
// Text formatting
// ==============================================================================

void MarkupEmitter::format_text(String* str) {
    if (!str || str->len == 0) return;
    if (rules_->escape_config) {
        format_text_with_escape(output(), str, rules_->escape_config);
    } else {
        write_text(str);
    }
}

void MarkupEmitter::format_text_cstr(const char* text) {
    if (!text || text[0] == '\0') return;
    if (rules_->escape_config) {
        const TextEscapeConfig* config = rules_->escape_config;
        size_t len = strlen(text);
        for (size_t i = 0; i < len; i++) {
            char c = text[i];
            bool needs_escape = false;
            if (config->chars_to_escape) {
                for (const char* p = config->chars_to_escape; *p; p++) {
                    if (c == *p) { needs_escape = true; break; }
                }
            }
            if (needs_escape && config->use_backslash_escape) {
                write_char('\\');
                write_char(c);
            } else {
                write_char(c);
            }
        }
    } else {
        write_text(text);
    }
}

// ==============================================================================
// Tag matching helpers
// ==============================================================================

static bool match_tag_array(const char* tag, const char* const tags[], int count) {
    for (int i = 0; i < count; i++) {
        if (tags[i] && strcmp(tag, tags[i]) == 0) return true;
    }
    return false;
}

bool MarkupEmitter::is_container_tag(const char* tag) const {
    return match_tag_array(tag, rules_->container_tags, 8);
}

bool MarkupEmitter::is_skip_tag(const char* tag) const {
    return match_tag_array(tag, rules_->skip_tags, 4);
}

// ==============================================================================
// Children iteration
// ==============================================================================

void MarkupEmitter::format_children(const ElementReader& elem) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        format_item(child);
    }
}

void MarkupEmitter::format_children_raw(const ElementReader& elem) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isString()) {
            String* str = child.asString();
            if (str) format_raw_text_common(output(), str);
        } else {
            format_item(child);
        }
    }
}

// ==============================================================================
// ItemReader dispatch
// ==============================================================================

void MarkupEmitter::format_item(const ItemReader& item) {
    RecursionGuard guard(*this);
    if (guard.exceeded()) {
        log_debug("markup: maximum recursion depth reached");
        return;
    }

    if (item.isNull()) {
        // skip null items
    } else if (item.isString()) {
        String* str = item.asString();
        if (str) format_text(str);
    } else if (item.isElement()) {
        ElementReader elem = item.asElement();
        format_element(elem);
    } else if (item.isArray()) {
        ArrayReader arr = item.asArray();
        auto it = arr.items();
        ItemReader child;
        while (it.next(&child)) {
            format_item(child);
        }
    }
}

// ==============================================================================
// Heading
// ==============================================================================

void MarkupEmitter::emit_heading(const ElementReader& elem) {
    const char* tag_name = elem.tagName();
    int level = 1;

    // try "level" attribute first (used by Org)
    String* level_attr = elem.get_string_attr("level");
    if (level_attr && level_attr->len > 0) {
        level = (int)str_to_int64_default(level_attr->chars, strlen(level_attr->chars), 0);
    } else if (tag_name && tag_name[0] == 'h' && isdigit(tag_name[1])) {
        level = tag_name[1] - '0';
    }
    if (level < 1) level = 1;
    if (level > 6) level = 6;

    const auto& heading = rules_->heading;

    switch (heading.type) {
    case MarkupOutputRules::HeadingStyle::PREFIX:
        // "###" × level + " " + children + "\n"
        for (int i = 0; i < level; i++) write_char(heading.repeated_char);
        write_char(' ');
        format_children(elem);
        write_char('\n');
        break;

    case MarkupOutputRules::HeadingStyle::UNDERLINE: {
        // children + "\n" + underline_char × text_length + "\n\n"
        size_t start_pos = output()->length;
        format_children(elem);
        size_t end_pos = output()->length;

        // measure text length (count non-newline chars)
        int text_len = 0;
        for (size_t i = start_pos; i < end_pos; i++) {
            if (output()->str->chars[i] != '\n') text_len++;
        }

        char underline_char = heading.underline_chars[(level - 1) % 6];
        write_char('\n');
        for (int i = 0; i < text_len; i++) write_char(underline_char);
        write_text("\n\n");
        break;
    }

    case MarkupOutputRules::HeadingStyle::SURROUND:
        // "=" × level + " " + children + " " + "=" × level + "\n"
        for (int i = 0; i < level; i++) write_char(heading.repeated_char);
        write_char(' ');
        format_children(elem);
        write_char(' ');
        for (int i = 0; i < level; i++) write_char(heading.repeated_char);
        write_char('\n');
        break;

    case MarkupOutputRules::HeadingStyle::INDEXED_PREFIX:
        // "h1. " + children + "\n\n"
        if (heading.prefix[level - 1]) {
            write_text(heading.prefix[level - 1]);
        }
        format_children(elem);
        write_text("\n\n");
        break;
    }
}

// ==============================================================================
// Inline formatting
// ==============================================================================

void MarkupEmitter::emit_inline(const ElementReader& elem, const char* open, const char* close) {
    if (!open || !close) {
        // unsupported inline style — just emit children
        format_children(elem);
        return;
    }
    write_text(open);
    format_children(elem);
    write_text(close);
}

// ==============================================================================
// Links and images
// ==============================================================================

void MarkupEmitter::emit_link(const ElementReader& elem) {
    if (!rules_->emit_link) {
        format_children(elem);
        return;
    }

    // Org uses child elements "url" and "description" instead of attributes
    // Only use this path when the element IS actually a format-specific link element
    const char* elem_tag = elem.tagName();
    if (rules_->link_tag && strcmp(rules_->link_tag, "link") == 0 &&
        elem_tag && strcmp(elem_tag, "link") == 0) {
        // Org-style link: children are "url" and "description" elements
        const char* url = nullptr;
        const char* desc = nullptr;
        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader child = elem.childAt(i);
            if (child.isElement()) {
                ElementReader ce = child.asElement();
                const char* tag = ce.tagName();
                if (tag) {
                    if (strcmp(tag, "url") == 0) {
                        // get text content of url element
                        for (int64_t j = 0; j < ce.childCount(); j++) {
                            ItemReader uc = ce.childAt(j);
                            if (uc.isString()) { url = uc.cstring(); break; }
                        }
                    } else if (strcmp(tag, "description") == 0) {
                        for (int64_t j = 0; j < ce.childCount(); j++) {
                            ItemReader dc = ce.childAt(j);
                            if (dc.isString()) { desc = dc.cstring(); break; }
                        }
                    }
                }
            }
        }
        rules_->emit_link(output(), url, desc, nullptr);
        return;
    }

    // Standard link: attributes href, title
    const char* href = elem.get_attr_string("href");
    const char* title = elem.get_attr_string("title");

    // collect children text into a buffer for the link text
    StringBuf* link_text_buf = stringbuf_new(pool());
    MarkupEmitter child_emitter(rules_, pool(), link_text_buf);
    child_emitter.format_children(elem);
    // null-terminate and get c-string from the buffer
    stringbuf_append_char(link_text_buf, '\0');
    const char* link_text = link_text_buf->str ? link_text_buf->str->chars : "";

    rules_->emit_link(output(), href, link_text, title);
}

void MarkupEmitter::emit_image(const ElementReader& elem) {
    if (!rules_->emit_image) {
        // no image support — skip
        return;
    }

    const char* src = elem.get_attr_string("src");
    const char* alt = elem.get_attr_string("alt");

    rules_->emit_image(output(), src, alt);
}

// ==============================================================================
// Lists
// ==============================================================================

void MarkupEmitter::emit_list(const ElementReader& elem, bool ordered, int depth) {
    auto it = elem.children();
    ItemReader child;
    int index = 0;

    // get start number for ordered lists
    int start_num = 1;
    if (ordered) {
        String* start_attr = elem.get_string_attr("start");
        if (start_attr && start_attr->len > 0) {
            start_num = (int)str_to_int64_default(start_attr->chars, strlen(start_attr->chars), 0);
        }
    }

    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* child_tag = child_elem.tagName();

            if (child_tag && strcmp(child_tag, "li") == 0) {
                emit_list_item(child_elem, ordered, depth, start_num + index);
                index++;
            }
        }
    }

    if (depth == 0 && rules_->list.use_depth_repetition) {
        // Wiki/Textile: add a trailing newline after top-level list
        write_char('\n');
    }
}

void MarkupEmitter::emit_list_item(const ElementReader& elem, bool ordered, int depth, int index) {
    const auto& ls = rules_->list;

    if (ls.use_depth_repetition) {
        // Wiki/Textile style: repeat character per depth level
        char marker_char = ordered ? ls.ordered_repeat_char : ls.unordered_repeat_char;
        for (int i = 0; i <= depth; i++) {
            write_char(marker_char);
        }
        write_char(' ');
    } else {
        // MD/RST/Org style: indent + marker
        for (int i = 0; i < depth * ls.indent_spaces; i++) {
            write_char(' ');
        }
        if (ordered && ls.ordered_format) {
            char num_buf[32];
            snprintf(num_buf, sizeof(num_buf), ls.ordered_format, index);
            write_text(num_buf);
        } else if (!ordered && ls.unordered_marker) {
            write_text(ls.unordered_marker);
        }
    }

    // format list item children, watching for nested lists
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        if (child.isElement()) {
            ElementReader ce = child.asElement();
            const char* tag = ce.tagName();
            if (tag && (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0)) {
                write_char('\n');
                emit_list(ce, strcmp(tag, "ol") == 0, depth + 1);
                continue;
            }
        }
        format_item(child);
    }

    // newline after list item (unless nested list already added one)
    write_char('\n');
}

// ==============================================================================
// Code blocks
// ==============================================================================

void MarkupEmitter::emit_code_block(const ElementReader& elem) {
    String* lang_attr = elem.get_string_attr("language");
    bool has_lang = lang_attr && lang_attr->len > 0;

    const auto& cb = rules_->code_block;

    switch (cb.type) {
    case MarkupOutputRules::CodeBlockStyle::FENCE:
        // ```lang\n content \n```\n
        write_text(cb.open_prefix);
        if (has_lang && cb.lang_after_open) write_text(lang_attr);
        write_char('\n');
        format_children_raw(elem);
        write_char('\n');
        write_text(cb.close_text);
        break;

    case MarkupOutputRules::CodeBlockStyle::DIRECTIVE:
        // .. code-block:: lang\n\n   content\n\n
        write_text(cb.open_prefix);
        if (has_lang && cb.lang_after_open) write_text(lang_attr);
        write_text("\n\n   "); // 3-space indent for RST directive content
        format_children(elem);
        write_text(cb.close_text);
        break;

    case MarkupOutputRules::CodeBlockStyle::BEGIN_END:
        // #+BEGIN_SRC lang\n content \n#+END_SRC\n
        write_text(cb.open_prefix);
        if (has_lang && cb.lang_after_open) {
            write_char(' ');
            write_text(lang_attr);
        }
        write_char('\n');
        format_children_raw(elem);
        write_text(cb.close_text);
        break;

    case MarkupOutputRules::CodeBlockStyle::TAG:
        // <pre>\n content </pre>\n\n
        write_text(cb.open_prefix);
        write_char('\n');
        format_children_raw(elem);
        write_char('\n');
        write_text(cb.close_text);
        break;

    case MarkupOutputRules::CodeBlockStyle::DOT_PREFIX:
        // bc.(lang) content\n\n
        write_text(cb.open_prefix);
        if (has_lang && cb.lang_in_parens) {
            write_char('(');
            write_text(lang_attr);
            write_text(") ");
        } else {
            write_char(' ');
        }
        format_children_raw(elem);
        write_text(cb.close_text);
        break;
    }
}

// ==============================================================================
// Blockquote
// ==============================================================================

void MarkupEmitter::emit_blockquote(const ElementReader& elem) {
    if (!rules_->blockquote_open) {
        // no blockquote support — just emit children
        format_children(elem);
        return;
    }

    if (rules_->blockquote_prefix_each_line) {
        // MD style: prefix each line with "> "
        // Collect content into a temp buffer, then prefix each line
        StringBuf* temp = stringbuf_new(pool());
        MarkupEmitter child_emitter(rules_, pool(), temp);
        child_emitter.format_children(elem);
        // ensure null-terminated for char access
        stringbuf_append_char(temp, '\0');
        const char* content = temp->str ? temp->str->chars : "";
        size_t len = temp->length > 0 ? temp->length - 1 : 0; // exclude the null we added

        for (size_t i = 0; i < len; i++) {
            if (i == 0 || (i > 0 && content[i - 1] == '\n' && i < len)) {
                write_text(rules_->blockquote_open);
            }
            write_char(content[i]);
        }
        write_text(rules_->blockquote_close);
    } else {
        // Org/Textile style: wrap with open/close
        write_text(rules_->blockquote_open);
        format_children(elem);
        write_text(rules_->blockquote_close);
    }
}

// ==============================================================================
// Paragraph, HR, BR
// ==============================================================================

void MarkupEmitter::emit_paragraph(const ElementReader& elem) {
    format_children(elem);
    if (rules_->paragraph_suffix) {
        write_text(rules_->paragraph_suffix);
    }
}

void MarkupEmitter::emit_hr() {
    if (rules_->hr) {
        write_text(rules_->hr);
    }
}

void MarkupEmitter::emit_br() {
    write_char('\n');
}

// ==============================================================================
// Element dispatch
// ==============================================================================

void MarkupEmitter::format_element(const ElementReader& elem) {
    RecursionGuard guard(*this);
    if (guard.exceeded()) {
        log_debug("markup: maximum recursion depth reached");
        return;
    }

    const char* tag = elem.tagName();
    if (!tag) {
        format_children(elem);
        return;
    }

    // try custom handler first (format-specific overrides for Org blocks, Textile DL, etc.)
    if (rules_->custom_element_handler) {
        if (rules_->custom_element_handler((void*)this, output(), elem)) {
            return;
        }
    }

    // skip tags
    if (is_skip_tag(tag)) return;

    // container tags (pass-through)
    if (is_container_tag(tag)) {
        format_children(elem);
        return;
    }

    // headings (h1-h6 or "heading")
    if (is_heading_tag(tag) || strcmp(tag, "heading") == 0 || strcmp(tag, "header") == 0) {
        emit_heading(elem);
        return;
    }

    // paragraph
    if (strcmp(tag, "p") == 0 || strcmp(tag, "paragraph") == 0) {
        emit_paragraph(elem);
        return;
    }

    // inline formatting
    const auto& im = rules_->inline_markup;
    const auto& tn = rules_->tag_names;

    // bold
    if (match_tag_array(tag, tn.bold_tags, 4)) {
        emit_inline(elem, im.bold_open, im.bold_close);
        return;
    }
    // italic
    if (match_tag_array(tag, tn.italic_tags, 4)) {
        emit_inline(elem, im.italic_open, im.italic_close);
        return;
    }
    // code (inline)
    if (tn.code_tag && strcmp(tag, tn.code_tag) == 0) {
        // check if it's a code block (has "language" attribute) or inline code
        String* lang = elem.get_string_attr("language");
        if (lang && lang->len > 0) {
            emit_code_block(elem);
        } else {
            // inline code: emit raw children (no escaping inside backticks)
            if (im.code_open) write_text(im.code_open);
            format_children_raw(elem);
            if (im.code_close) write_text(im.code_close);
        }
        return;
    }
    // strikethrough
    if (match_tag_array(tag, tn.strike_tags, 4)) {
        emit_inline(elem, im.strikethrough_open, im.strikethrough_close);
        return;
    }
    // underline
    if (match_tag_array(tag, tn.underline_tags, 4)) {
        emit_inline(elem, im.underline_open, im.underline_close);
        return;
    }
    // superscript
    if (tn.sup_tag && strcmp(tag, tn.sup_tag) == 0) {
        emit_inline(elem, im.superscript_open, im.superscript_close);
        return;
    }
    // subscript
    if (tn.sub_tag && strcmp(tag, tn.sub_tag) == 0) {
        emit_inline(elem, im.subscript_open, im.subscript_close);
        return;
    }
    // verbatim (Org only)
    if (tn.verbatim_tag && strcmp(tag, tn.verbatim_tag) == 0) {
        emit_inline(elem, im.verbatim_open, im.verbatim_close);
        return;
    }

    // links
    if ((rules_->link_tag && strcmp(tag, rules_->link_tag) == 0) || strcmp(tag, "a") == 0) {
        emit_link(elem);
        return;
    }

    // images
    if (strcmp(tag, "img") == 0) {
        emit_image(elem);
        return;
    }

    // lists
    if (strcmp(tag, "ul") == 0) {
        emit_list(elem, false, 0);
        return;
    }
    if (strcmp(tag, "ol") == 0) {
        emit_list(elem, true, 0);
        return;
    }
    // standalone list_item (Org-style)
    if (strcmp(tag, "li") == 0 || strcmp(tag, "list_item") == 0) {
        format_children(elem);
        write_char('\n');
        return;
    }

    // code block / pre
    if (strcmp(tag, "pre") == 0 || strcmp(tag, "code_block") == 0) {
        emit_code_block(elem);
        return;
    }

    // blockquote
    if (strcmp(tag, "blockquote") == 0) {
        emit_blockquote(elem);
        return;
    }

    // table
    if (strcmp(tag, "table") == 0) {
        if (rules_->emit_table) {
            rules_->emit_table(output(), elem, (void*)this);
        }
        return;
    }

    // hr
    if (strcmp(tag, "hr") == 0) {
        emit_hr();
        return;
    }

    // br
    if (strcmp(tag, "br") == 0) {
        emit_br();
        return;
    }

    // table sub-elements (if encountered outside table context, just emit children)
    if (strcmp(tag, "tr") == 0 || strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0 ||
        strcmp(tag, "thead") == 0 || strcmp(tag, "tbody") == 0 || strcmp(tag, "tfoot") == 0) {
        format_children(elem);
        return;
    }

    // fallback: unknown tag — just emit children
    format_children(elem);
}

// ==============================================================================
// Format-Specific Custom Element Handlers
// ==============================================================================

// Utility: get text content of a named child element
static const char* get_child_text(const ElementReader& elem, const char* tag) {
    ElementReader child = elem.findChildElement(tag);
    if (!child.isValid()) return nullptr;
    for (int64_t i = 0; i < child.childCount(); i++) {
        ItemReader item = child.childAt(i);
        if (item.isString()) return item.cstring();
        if (item.isElement()) {
            ElementReader sub = item.asElement();
            for (int64_t j = 0; j < sub.childCount(); j++) {
                ItemReader sj = sub.childAt(j);
                if (sj.isString()) return sj.cstring();
            }
        }
    }
    return nullptr;
}

// Utility: format all string children (no recursion into child elements)
static void format_string_children(MarkupEmitter* em, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (child.isString()) {
            em->write_text(child.cstring());
        }
    }
}

// Utility: format all "content" child elements as lines (for Org blocks)
static void format_content_lines(MarkupEmitter* em, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        if (ce.tagName() && strcmp(ce.tagName(), "content") == 0) {
            for (int64_t j = 0; j < ce.childCount(); j++) {
                ItemReader cj = ce.childAt(j);
                if (cj.isString()) {
                    em->write_text(cj.cstring());
                    em->write_char('\n');
                }
            }
        }
    }
}

// Utility: format contained paragraphs (for Org blocks)
static void format_contained_paragraphs(MarkupEmitter* em, const ElementReader& elem) {
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        if (ce.tagName() && strcmp(ce.tagName(), "paragraph") == 0) {
            em->format_children(ce);
            em->write_char('\n');
        }
    }
}

// ---------------------------------------------------------------------------
// Org-mode custom element handler
// ---------------------------------------------------------------------------

bool org_custom_handler(void* ctx, StringBuf* sb, const ElementReader& elem) {
    MarkupEmitter* em = (MarkupEmitter*)ctx;
    const char* tag = elem.tagName();
    if (!tag) return false;

    // org_document: iterate children
    if (strcmp(tag, "org_document") == 0) {
        for (int64_t i = 0; i < elem.childCount(); i++) {
            em->format_item(elem.childAt(i));
        }
        return true;
    }

    // Org heading: has structured children (level, todo, title, tags)
    if (strcmp(tag, "heading") == 0) {
        int level = 1;
        const char* title = nullptr;
        const char* todo = nullptr;
        const char* tags = nullptr;

        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader child = elem.childAt(i);
            if (!child.isElement()) continue;
            ElementReader ce = child.asElement();
            const char* ct = ce.tagName();
            if (!ct) continue;
            if (strcmp(ct, "level") == 0) {
                const char* lstr = get_child_text(elem, "level");
                if (lstr) level = (int)str_to_int64_default(lstr, strlen(lstr), 0);
            } else if (strcmp(ct, "todo") == 0) {
                todo = get_child_text(elem, "todo");
            } else if (strcmp(ct, "title") == 0) {
                title = get_child_text(elem, "title");
            } else if (strcmp(ct, "tags") == 0) {
                tags = get_child_text(elem, "tags");
            }
        }
        for (int i = 0; i < level; i++) em->write_char('*');
        em->write_char(' ');
        if (todo)  { em->write_text(todo); em->write_char(' '); }
        if (title) em->write_text(title);
        if (tags)  { em->write_char(' '); em->write_text(tags); }
        em->write_char('\n');
        return true;
    }

    // example_block: #+BEGIN_EXAMPLE\n...\n#+END_EXAMPLE\n
    if (strcmp(tag, "example_block") == 0) {
        em->write_text("#+BEGIN_EXAMPLE\n");
        format_content_lines(em, elem);
        em->write_text("#+END_EXAMPLE\n");
        return true;
    }

    // verse_block: #+BEGIN_VERSE\n...\n#+END_VERSE\n
    if (strcmp(tag, "verse_block") == 0) {
        em->write_text("#+BEGIN_VERSE\n");
        format_content_lines(em, elem);
        em->write_text("#+END_VERSE\n");
        return true;
    }

    // center_block: #+BEGIN_CENTER\n...\n#+END_CENTER\n
    if (strcmp(tag, "center_block") == 0) {
        em->write_text("#+BEGIN_CENTER\n");
        format_contained_paragraphs(em, elem);
        em->write_text("#+END_CENTER\n");
        return true;
    }

    // drawer: :NAME:\n...\n:END:\n
    if (strcmp(tag, "drawer") == 0) {
        const char* name = get_child_text(elem, "name");
        em->write_char(':');
        if (name) em->write_text(name);
        em->write_text(":\n");
        format_content_lines(em, elem);
        em->write_text(":END:\n");
        return true;
    }

    // scheduling: SCHEDULED:/DEADLINE:/CLOSED: <timestamp>
    if (strcmp(tag, "scheduling") == 0) {
        const char* keyword   = get_child_text(elem, "keyword");
        const char* timestamp = get_child_text(elem, "timestamp");
        em->write_text("  ");
        if (keyword) {
            if (strcmp(keyword, "scheduled") == 0)    em->write_text("SCHEDULED: ");
            else if (strcmp(keyword, "deadline") == 0) em->write_text("DEADLINE: ");
            else if (strcmp(keyword, "closed") == 0)   em->write_text("CLOSED: ");
        }
        if (timestamp) em->write_text(timestamp);
        em->write_char('\n');
        return true;
    }

    // footnote_definition: [fn:name] content
    if (strcmp(tag, "footnote_definition") == 0) {
        const char* name = get_child_text(elem, "name");
        em->write_text("[fn:");
        if (name) em->write_text(name);
        em->write_text("] ");
        ElementReader content = elem.findChildElement("content");
        if (content.isValid()) em->format_children(content);
        em->write_char('\n');
        return true;
    }

    // footnote_reference: [fn:name]
    if (strcmp(tag, "footnote_reference") == 0) {
        em->write_text("[fn:");
        const char* name = get_child_text(elem, "name");
        if (name) em->write_text(name);
        em->write_text("]");
        return true;
    }

    // inline_footnote: [fn:name:definition]
    if (strcmp(tag, "inline_footnote") == 0) {
        em->write_text("[fn:");
        const char* name = get_child_text(elem, "name");
        if (name && strlen(name) > 0) em->write_text(name);
        em->write_text(":");
        ElementReader def_elem = elem.findChildElement("definition");
        if (def_elem.isValid()) em->format_children(def_elem);
        em->write_text("]");
        return true;
    }

    // inline_math: \(latex\) or $ascii$
    if (strcmp(tag, "inline_math") == 0) {
        const char* raw = get_child_text(elem, "raw_content");
        bool latex_style = raw && strchr(raw, '\\');
        if (latex_style) {
            em->write_text("\\("); if (raw) em->write_text(raw); em->write_text("\\)");
        } else {
            em->write_text("$"); if (raw) em->write_text(raw); em->write_text("$");
        }
        return true;
    }

    // display_math: \[latex\] or $$ascii$$
    if (strcmp(tag, "display_math") == 0) {
        const char* raw = get_child_text(elem, "raw_content");
        bool latex_style = raw && (strchr(raw, '\\') || strlen(raw) > 20);
        if (latex_style) {
            em->write_text("\\["); if (raw) em->write_text(raw); em->write_text("\\]");
        } else {
            em->write_text("$$"); if (raw) em->write_text(raw); em->write_text("$$");
        }
        em->write_char('\n');
        return true;
    }

    // timestamp: extract text from children
    if (strcmp(tag, "timestamp") == 0) {
        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader c = elem.childAt(i);
            if (c.isString()) { em->write_text(c.cstring()); return true; }
        }
        for (int64_t i = 0; i < elem.childCount(); i++) {
            ItemReader c = elem.childAt(i);
            if (c.isElement()) {
                ElementReader sub = c.asElement();
                for (int64_t j = 0; j < sub.childCount(); j++) {
                    ItemReader sj = sub.childAt(j);
                    if (sj.isString()) { em->write_text(sj.cstring()); return true; }
                }
            }
        }
        return true;
    }

    // directive: #+KEYWORD: value
    if (strcmp(tag, "directive") == 0) {
        format_string_children(em, elem);
        em->write_char('\n');
        return true;
    }

    // plain_text / text: just string children
    if (strcmp(tag, "plain_text") == 0 || strcmp(tag, "text") == 0) {
        format_string_children(em, elem);
        return true;
    }

    // Org table sub-elements handled by emit_table_org
    if (strcmp(tag, "table_row") == 0 || strcmp(tag, "table_header_row") == 0 ||
        strcmp(tag, "table_cell") == 0) {
        format_string_children(em, elem);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Textile custom element handler
// ---------------------------------------------------------------------------

bool textile_custom_handler(void* ctx, StringBuf* sb, const ElementReader& elem) {
    MarkupEmitter* em = (MarkupEmitter*)ctx;
    const char* tag = elem.tagName();
    if (!tag) return false;

    // cite: ??text??
    if (strcmp(tag, "cite") == 0) {
        em->write_text("??");
        em->format_children(elem);
        em->write_text("??");
        return true;
    }

    // span: %text%
    if (strcmp(tag, "span") == 0) {
        em->write_char('%');
        em->format_children(elem);
        em->write_char('%');
        return true;
    }

    // dl/dt/dd: definition list
    if (strcmp(tag, "dl") == 0) {
        auto it = elem.children();
        ItemReader child;
        while (it.next(&child)) {
            if (child.isElement()) {
                ElementReader ce = child.asElement();
                const char* ct = ce.tagName();
                if (ct && strcmp(ct, "dt") == 0) {
                    em->write_text("- ");
                    em->format_children(ce);
                } else if (ct && strcmp(ct, "dd") == 0) {
                    em->write_text(" := ");
                    em->format_children(ce);
                    em->write_char('\n');
                }
            }
        }
        em->write_char('\n');
        return true;
    }

    return false;
}

// ==============================================================================
// Table Handlers — one per table style
// ==============================================================================

// context struct so table row callbacks can reach the emitter
typedef struct {
    MarkupEmitter* emitter;
    int header_row_done;
} MarkupTableContext;

// Pipe-style table row handler (MD, Org)
static void pipe_table_row(
    StringBuf* sb, const ElementReader& row,
    int row_idx, bool is_header, void* ctx
) {
    MarkupTableContext* mc = (MarkupTableContext*)ctx;
    MarkupEmitter* em = mc->emitter;

    stringbuf_append_char(sb, '|');
    auto it = row.children();
    ItemReader cell;
    while (it.next(&cell)) {
        stringbuf_append_char(sb, ' ');
        if (cell.isElement()) {
            em->format_children(cell.asElement());
        }
        stringbuf_append_str(sb, " |");
    }
    stringbuf_append_char(sb, '\n');

    // add separator after first header row
    if (is_header && row_idx == 0 && !mc->header_row_done) {
        mc->header_row_done = 1;
        stringbuf_append_char(sb, '|');
        auto sep_it = row.children();
        ItemReader sep_cell;
        while (sep_it.next(&sep_cell)) {
            stringbuf_append_str(sb, "---|");
        }
        stringbuf_append_char(sb, '\n');
    }
}

// emit_table callback for pipe-style tables (MD)
void emit_table_pipe(StringBuf* sb, const ElementReader& elem, void* emitter_ctx) {
    MarkupEmitter* em = (MarkupEmitter*)emitter_ctx;
    MarkupTableContext mc = {em, 0};
    iterate_table_rows(elem, sb, pipe_table_row, &mc);
}

// RST table row handler
static void rst_table_row(
    StringBuf* sb, const ElementReader& row,
    int row_idx, bool is_header, void* ctx
) {
    MarkupTableContext* mc = (MarkupTableContext*)ctx;
    MarkupEmitter* em = mc->emitter;

    stringbuf_append_str(sb, "   "); // RST directive indent
    auto it = row.children();
    ItemReader cell;
    bool first = true;
    while (it.next(&cell)) {
        if (!first) stringbuf_append_str(sb, " | ");
        first = false;
        if (cell.isElement()) {
            em->format_children(cell.asElement());
        }
    }
    stringbuf_append_char(sb, '\n');

    // header separator
    if (is_header && row_idx == 0 && !mc->header_row_done) {
        mc->header_row_done = 1;
        stringbuf_append_str(sb, "   ");
        auto sep_it = row.children();
        ItemReader sep_cell;
        bool sep_first = true;
        while (sep_it.next(&sep_cell)) {
            if (!sep_first) stringbuf_append_str(sb, " + ");
            sep_first = false;
            stringbuf_append_str(sb, "===");
        }
        stringbuf_append_char(sb, '\n');
    }
}

// emit_table callback for RST
void emit_table_rst(StringBuf* sb, const ElementReader& elem, void* emitter_ctx) {
    MarkupEmitter* em = (MarkupEmitter*)emitter_ctx;
    stringbuf_append_str(sb, ".. table::\n\n");
    MarkupTableContext mc = {em, 0};
    iterate_table_rows(elem, sb, rst_table_row, &mc);
    stringbuf_append_char(sb, '\n');
}

// Wiki table row handler
static void wiki_table_row(
    StringBuf* sb, const ElementReader& row,
    int row_idx, bool is_header, void* ctx
) {
    MarkupTableContext* mc = (MarkupTableContext*)ctx;
    MarkupEmitter* em = mc->emitter;

    // start table on first row
    if (row_idx == 0 && !mc->header_row_done) {
        stringbuf_append_str(sb, "{| class=\"wikitable\"\n");
    }

    stringbuf_append_str(sb, "|-\n");

    auto it = row.children();
    ItemReader cell;
    while (it.next(&cell)) {
        if (cell.isElement()) {
            if (is_header) {
                stringbuf_append_str(sb, "! ");
            } else {
                stringbuf_append_str(sb, "| ");
            }
            em->format_children(cell.asElement());
            stringbuf_append_char(sb, '\n');
        }
    }

    (void)mc; // header_row_done not needed for wiki style
}

// emit_table callback for Wiki
void emit_table_wiki(StringBuf* sb, const ElementReader& elem, void* emitter_ctx) {
    MarkupEmitter* em = (MarkupEmitter*)emitter_ctx;
    MarkupTableContext mc = {em, 0};
    iterate_table_rows(elem, sb, wiki_table_row, &mc);
    stringbuf_append_str(sb, "|}\n\n");
}

// Textile table row handler
static void textile_table_row(
    StringBuf* sb, const ElementReader& row,
    int row_idx, bool is_header, void* ctx
) {
    MarkupTableContext* mc = (MarkupTableContext*)ctx;
    MarkupEmitter* em = mc->emitter;
    (void)row_idx;

    auto it = row.children();
    ItemReader cell;
    while (it.next(&cell)) {
        if (cell.isElement()) {
            ElementReader ce = cell.asElement();
            const char* tag = ce.tagName();
            bool cell_is_header = is_header || (tag && strcmp(tag, "th") == 0);
            if (cell_is_header) {
                stringbuf_append_str(sb, "|_. ");
            } else {
                stringbuf_append_char(sb, '|');
            }
            em->format_children(ce);
        }
    }
    stringbuf_append_str(sb, "|\n");
}

// emit_table callback for Textile
void emit_table_textile(StringBuf* sb, const ElementReader& elem, void* emitter_ctx) {
    MarkupEmitter* em = (MarkupEmitter*)emitter_ctx;
    MarkupTableContext mc = {em, 0};
    iterate_table_rows(elem, sb, textile_table_row, &mc);
    stringbuf_append_char(sb, '\n');
}

// Org table handler (uses child elements directly, not iterate_table_rows)
void emit_table_org(StringBuf* sb, const ElementReader& elem, void* emitter_ctx) {
    MarkupEmitter* em = (MarkupEmitter*)emitter_ctx;

    bool first_row = true;
    for (int64_t i = 0; i < elem.childCount(); i++) {
        ItemReader child = elem.childAt(i);
        if (!child.isElement()) continue;
        ElementReader ce = child.asElement();
        const char* tag = ce.tagName();
        if (!tag) continue;

        if (strcmp(tag, "table_row") == 0 || strcmp(tag, "table_header_row") == 0) {
            bool is_header = (strcmp(tag, "table_header_row") == 0) || first_row;

            // emit row: | cell1 | cell2 |
            stringbuf_append_char(sb, '|');
            for (int64_t j = 0; j < ce.childCount(); j++) {
                ItemReader rc = ce.childAt(j);
                if (!rc.isElement()) continue;
                ElementReader re = rc.asElement();
                if (re.tagName() && strcmp(re.tagName(), "table_cell") == 0) {
                    stringbuf_append_char(sb, ' ');
                    em->format_children(re);
                    stringbuf_append_str(sb, " |");
                }
            }
            stringbuf_append_char(sb, '\n');

            // separator after header
            if (is_header) {
                stringbuf_append_char(sb, '|');
                for (int64_t j = 0; j < ce.childCount(); j++) {
                    ItemReader rc = ce.childAt(j);
                    if (rc.isElement()) {
                        stringbuf_append_str(sb, "---------|");
                    }
                }
                stringbuf_append_char(sb, '\n');
            }
            first_row = false;
        }
    }
}

// ==============================================================================
// Public API
// ==============================================================================

void format_markup(StringBuf* sb, Item root_item, const MarkupOutputRules* rules) {
    if (!sb || !rules) return;
    if (root_item.item == ITEM_NULL) return;

    Pool* pool = pool_create();
    MarkupEmitter emitter(rules, pool, sb);

    ItemReader root(root_item.to_const());
    emitter.format_item(root);

    pool_destroy(pool);
}

String* format_markup_string(Pool* pool, Item root_item, const MarkupOutputRules* rules) {
    if (!pool || !rules) return nullptr;
    StringBuf* sb = stringbuf_new(pool);
    format_markup(sb, root_item, rules);
    return stringbuf_to_string(sb);
}
