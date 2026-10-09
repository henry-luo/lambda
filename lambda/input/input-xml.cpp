#include "input.hpp"
#include "input-parsers.h"
#include "../io/mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "../../lib/html_entities.h"
#include "../../lib/str.h"
#include "../../lib/arraylist.h"
#include "../../lib/hashmap_helpers.h"
#include "input-utils.h"

using namespace lambda;

static const int XML_MAX_DEPTH = 512;

struct XmlNamespaceFrame;
struct XmlInputContext : InputContext {
    const XmlParseOptions* options;
    const char* begin;
    const char* end;
    const char* document_start;
    XmlNamespaceFrame* namespaces = nullptr;
    XmlInputContext(Input* input, const char* source, const XmlParseOptions* opts)
        : InputContext(input, source), options(opts), begin(source), end(source + strlen(source)), document_start(source) {}
    bool preserving() const { return options && options->preserve_whitespace; }
    bool strict() const { return options && (options->require_well_formed || options->require_namespaces); }
    bool namespaced() const { return options && options->require_namespaces; }
};

static bool xml_error(XmlInputContext& ctx, const char* at, const char* code, const char* message) {
    ctx.syncTo(at); ctx.addErrorCode(ctx.tracker.location(), code, "%s", message); return false;
}

static bool xml_character(uint32_t cp) {
    return cp == 9 || cp == 10 || cp == 13 || (cp >= 0x20 && cp <= 0xD7FF) ||
        (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
}

static void xml_append_literal(XmlInputContext& ctx, StringBuf* buffer, const char* start,
        const char* end, bool attribute = false) {
    if (!ctx.strict()) { stringbuf_append_str_n(buffer, start, (size_t)(end - start)); return; }
    // normalize physical line ends before XML attribute whitespace; character references bypass this step.
    while (start < end) {
        const char* run = start;
        while (start < end && *start != '\r' && (!attribute || (*start != '\n' && *start != '\t'))) start++;
        stringbuf_append_str_n(buffer, run, (size_t)(start - run));
        if (start < end) {
            if (*start++ == '\r' && start < end && *start == '\n') start++;
            stringbuf_append_char(buffer, attribute ? ' ' : '\n');
        }
    }
}

static Item parse_element(XmlInputContext& ctx, const char **xml, int depth = 0);
static Item parse_comment(XmlInputContext& ctx, const char **xml);
static Item parse_cdata(XmlInputContext& ctx, const char **xml);
static Item parse_entity(XmlInputContext& ctx, const char **xml);
static Item parse_doctype(XmlInputContext& ctx, const char **xml, int depth = 0);
static Item parse_dtd_declaration(XmlInputContext& ctx, const char **xml);
static String* parse_string_content(XmlInputContext& ctx, const char **xml, char end_char);

static inline bool xml_ref_at_limit(const char* xml, const char* limit) {
    return !xml || !*xml || (limit && xml >= limit);
}

static bool xml_ref_is_entity_name_char(char ch, bool stop_on_xml_delims) {
    if (!ch || ch == ';' || ch == ' ' || ch == '\t' || ch == '\n') return false;
    if (stop_on_xml_delims && (ch == '<' || ch == '&')) return false;
    return true;
}

static bool append_xml_numeric_reference(XmlInputContext& ctx, StringBuf* sb, const char** xml, const char* limit) {
    if (xml_ref_at_limit(*xml, limit) || **xml != '#') return false;
    const char* start = *xml;
    (*xml)++; // skip #
    uint32_t value = 0;
    bool is_hex = false;
    if (!xml_ref_at_limit(*xml, limit) && (**xml == 'x' || (!ctx.strict() && **xml == 'X'))) {
        is_hex = true;
        (*xml)++; // skip x
    }

    size_t digits = 0; bool overflow = false;
    while (!xml_ref_at_limit(*xml, limit) && **xml != ';') {
        uint32_t digit = 0;
        if (is_hex) {
            if (**xml >= '0' && **xml <= '9') {
                digit = **xml - '0';
            } else if (**xml >= 'a' && **xml <= 'f') {
                digit = **xml - 'a' + 10;
            } else if (**xml >= 'A' && **xml <= 'F') {
                digit = **xml - 'A' + 10;
            } else break;
        } else {
            if (**xml >= '0' && **xml <= '9') {
                digit = **xml - '0';
            } else break;
        }
        uint32_t radix = is_hex ? 16 : 10;
        overflow |= value > (0x10FFFFu - digit) / radix;
        value = value * radix + digit;
        (*xml)++; digits++;
    }

    if (ctx.strict() && (!digits || overflow || !xml_character(value) || xml_ref_at_limit(*xml, limit) || **xml != ';')) {
        xml_error(ctx, start, "XML_CHARACTER_REFERENCE", "Invalid XML character reference"); return true;
    }

    if (!xml_ref_at_limit(*xml, limit) && **xml == ';') {
        (*xml)++; // skip ;
        char utf8_buf[5];
        int utf8_len = codepoint_to_utf8(value, utf8_buf);
        if (utf8_len > 0) {
            stringbuf_append_str_n(sb, utf8_buf, (size_t)utf8_len);
        } else {
            stringbuf_append_char(sb, '?'); // invalid codepoint
        }
    } else {
        stringbuf_append_char(sb, '&');
        stringbuf_append_char(sb, '#');
    }
    return true;
}

static void append_xml_reference(XmlInputContext& ctx, StringBuf* sb, const char** xml, const char* limit,
                                 bool stop_on_xml_delims) {
    if (xml_ref_at_limit(*xml, limit) || **xml != '&') return;
    const char* reference_start = *xml;
    (*xml)++; // skip &

    if (append_xml_numeric_reference(ctx, sb, xml, limit)) return;

    const char* entity_start = *xml;
    while (!xml_ref_at_limit(*xml, limit) &&
           xml_ref_is_entity_name_char(**xml, stop_on_xml_delims)) {
        (*xml)++;
    }

    if (!xml_ref_at_limit(*xml, limit) && **xml == ';') {
        size_t entity_len = (size_t)(*xml - entity_start);
        const char* replacement = nullptr;
        if (ctx.strict()) {
            const char* names[] = {"lt", "gt", "amp", "apos", "quot"};
            const char* values[] = {"<", ">", "&", "'", "\""};
            for (size_t i = 0; i < 5; i++)
                if (strlen(names[i]) == entity_len && !memcmp(names[i], entity_start, entity_len)) replacement = values[i];
        } else replacement = html_entity_lookup(entity_start, entity_len);
        (*xml)++; // skip ;

        if (ctx.strict() && !replacement) {
            xml_error(ctx, reference_start, "XML_ENTITY_REFERENCE", "XML entity is not declared or predefined"); return;
        }

        if (replacement) {
            stringbuf_append_str(sb, replacement);
        } else {
            // Unknown entity references are preserved for roundtrip compatibility.
            stringbuf_append_char(sb, '&');
            stringbuf_append_str_n(sb, entity_start, (size_t)(*xml - entity_start));
        }
    } else {
        if (ctx.strict()) { xml_error(ctx, reference_start, "XML_ENTITY_REFERENCE", "Unterminated XML entity reference"); return; }
        stringbuf_append_char(sb, '&');
        *xml = entity_start;
    }
}

// Position of the first three-byte terminator ("-->", "]]>") at or after p,
// or of the NUL if there is none -- where the strncmp-at-every-offset loops
// stopped. strchr jumps to each candidate first byte (libc vectorizes it).
static const char* xml_find_terminator(const char* p, const char* term) {
    for (;;) {
        const char* hit = strchr(p, term[0]);
        if (!hit) return p + strlen(p);
        if (hit[1] == term[1] && hit[2] == term[2]) return hit;
        p = hit + 1;
    }
}

static String* parse_string_content(XmlInputContext& ctx, const char **xml, char end_char) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (**xml && **xml != end_char) {
        if (**xml == '&') {
            append_xml_reference(ctx, sb, xml, nullptr, true);
            if (ctx.strict() && ctx.hasErrors()) return nullptr;
            continue;
        }
        // append the plain run up to the quote, '&' or NUL in one call
        const char* run = *xml;
        while (*run && *run != end_char && *run != '&' && (!ctx.strict() || *run != '<')) run++;
        if (ctx.strict() && *run == '<') {
            xml_error(ctx, run, "XML_ATTRIBUTE_VALUE", "Literal '<' is forbidden in an XML attribute value"); return nullptr;
        }
        xml_append_literal(ctx, sb, *xml, run, true);
        *xml = run;
    }

    return builder.createString(sb->str->chars, sb->length);
}

static bool xml_name_start(uint32_t cp) {
    // XML 1.0 fifth edition productions [4]/[4a]; ASCII punctuation is handled separately.
    return cp == ':' || cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= 0xC0 && cp <= 0xD6) || (cp >= 0xD8 && cp <= 0xF6) || (cp >= 0xF8 && cp <= 0x2FF) ||
        (cp >= 0x370 && cp <= 0x37D) || (cp >= 0x37F && cp <= 0x1FFF) || (cp >= 0x200C && cp <= 0x200D) ||
        (cp >= 0x2070 && cp <= 0x218F) || (cp >= 0x2C00 && cp <= 0x2FEF) || (cp >= 0x3001 && cp <= 0xD7FF) ||
        (cp >= 0xF900 && cp <= 0xFDCF) || (cp >= 0xFDF0 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0xEFFFF);
}

static String* parse_tag_name(XmlInputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (**xml) {
        uint32_t cp = (uint8_t)**xml;
        int bytes = cp < 0x80 ? 1 : str_utf8_decode(*xml, (size_t)(ctx.end - *xml), &cp);
        if (bytes <= 0) break;
        bool admitted = xml_name_start(cp) || (sb->length && (cp == '-' || cp == '.' || (cp >= '0' && cp <= '9') ||
            cp == 0xB7 || (cp >= 0x300 && cp <= 0x36F) || (cp >= 0x203F && cp <= 0x2040)));
        if (!admitted) break;
        stringbuf_append_str_n(sb, *xml, (size_t)bytes);
        *xml += bytes;
    }

    if (sb->length == 0) return NULL; // empty tag name

    return builder.createString(sb->str->chars, sb->length);
}

static bool xml_closing_tag_matches(const char* xml, const char* tag_name, uint32_t tag_len) {
    if (!xml || !tag_name || tag_len == 0) return false;
    if (xml[0] != '<' || xml[1] != '/') return false;

    const char* p = xml + 2;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (strncmp(p, tag_name, tag_len) != 0) return false;
    p += tag_len;
    return *p == '>' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r';
}

static bool xml_skip_closing_tag(XmlInputContext& ctx, const char** xml, const String* name) {
    if (ctx.strict()) {
        if (!xml_closing_tag_matches(*xml, name->chars, name->len))
            return xml_error(ctx, *xml, "XML_CLOSING_TAG", "Invalid XML closing tag");
        const char* end = *xml + 2;
        if (strncmp(end, name->chars, name->len))
            return xml_error(ctx, *xml, "XML_CLOSING_TAG", "Invalid XML closing tag");
        end += name->len;
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
        if (*end != '>') return xml_error(ctx, end, "XML_CLOSING_TAG", "Invalid XML closing tag terminator");
        *xml = end + 1; return true;
    }
    if (!xml || !*xml) return false;
    if (**xml == '<' && *(*xml + 1) == '/') {
        *xml += 2;
        while (**xml && **xml != '>') (*xml)++;
        if (**xml == '>') (*xml)++;
    }
    return true;
}

struct XmlAttributeName { const char* name; const char* value; };
HASHMAP_DEFINE_STRKEY(xml_attribute_names, XmlAttributeName, name)
struct XmlAttributeSet {
    HashMap* names = nullptr;
    ~XmlAttributeSet() { if (names) hashmap_free(names); }
};

struct XmlNamespaceFrame {
    XmlInputContext& context;
    XmlNamespaceFrame* parent;
    XmlAttributeSet attributes;
    explicit XmlNamespaceFrame(XmlInputContext& ctx) : context(ctx), parent(ctx.namespaces) {
        if (ctx.namespaced()) ctx.namespaces = this;
    }
    ~XmlNamespaceFrame() { if (context.namespaced()) context.namespaces = parent; }
};

static bool parse_attributes(XmlInputContext& ctx, ElementBuilder& element, const char **xml,
        bool declaration = false, XmlNamespaceFrame* frame = nullptr) {
    XmlAttributeSet local_attributes;
    XmlAttributeSet& attributes = frame ? frame->attributes : local_attributes;
    if (ctx.strict()) {
        attributes.names = xml_attribute_names_new(0);
        if (!attributes.names) return xml_error(ctx, *xml, "XML_ALLOCATION", "XML attribute allocation failed");
    }
    size_t count = 0; bool standalone = false;
    const char* separator = *xml;
    skip_whitespace(xml);
    while (**xml && **xml != '>' && **xml != '/' && **xml != '?') {
        if (ctx.strict() && *xml == separator)
            return xml_error(ctx, *xml, "XML_ATTRIBUTE_SEPARATOR", "XML attributes require separating whitespace");
        // parse attribute name
        String* attr_name = parse_tag_name(ctx, xml);
        if (!attr_name) return false;
        if (ctx.strict()) {
            XmlAttributeName key = {attr_name->chars, nullptr};
            if (hashmap_get(attributes.names, &key))
                return xml_error(ctx, *xml, "XML_DUPLICATE_ATTRIBUTE", "Duplicate XML attribute name");
            hashmap_set(attributes.names, &key);
            if (hashmap_oom(attributes.names)) return xml_error(ctx, *xml, "XML_ALLOCATION", "XML attribute allocation failed");
        }

        skip_whitespace(xml);
        if (**xml != '=') return false;
        (*xml)++; // skip =

        skip_whitespace(xml);
        if (**xml != '"' && **xml != '\'') return false;

        char quote_char = **xml;
        (*xml)++; // skip opening quote
        const char* value_start = *xml;
        String* attr_value = parse_string_content(ctx, xml, quote_char);
        if (!attr_value) return false;
        if (ctx.strict() && **xml != quote_char)
            return xml_error(ctx, *xml, "XML_ATTRIBUTE_VALUE", "Unterminated XML attribute value");
        if (frame) {
            XmlAttributeName key = {attr_name->chars, attr_value->chars}; hashmap_set(attributes.names, &key);
            if (hashmap_oom(attributes.names)) return xml_error(ctx, *xml, "XML_ALLOCATION", "XML attribute allocation failed");
        }
        if (declaration) {
            bool valid = !memchr(value_start, '&', (size_t)(*xml - value_start));
            if (!count) valid &= !strcmp(attr_name->chars, "version") && !strcmp(attr_value->chars, "1.0");
            else if (!strcmp(attr_name->chars, "encoding")) valid &= count == 1 && str_ieq_cstr(attr_value->chars, "UTF-8");
            else if (!strcmp(attr_name->chars, "standalone")) {
                valid &= !standalone && (!strcmp(attr_value->chars, "yes") || !strcmp(attr_value->chars, "no")); standalone = true;
            } else valid = false;
            if (!valid) return xml_error(ctx, value_start, "XML_DECLARATION", "Invalid or unsupported XML 1.0 UTF-8 declaration");
        }
        if (**xml == quote_char) { (*xml)++; } // skip closing quote

        // Add attribute to element (wrap String* in Item)
        element.attr(attr_name->chars, Item{.item = s2it(attr_value)});

        count++; separator = *xml; skip_whitespace(xml);
    }
    if (declaration && !count) return xml_error(ctx, *xml, "XML_DECLARATION", "XML declaration requires a version");
    return true;
}

static const char* xml_namespace_uri(XmlInputContext& ctx, const char* prefix, size_t length) {
    if (length == 3 && !memcmp(prefix, "xml", 3)) return "http://www.w3.org/XML/1998/namespace";
    stringbuf_reset(ctx.sb); stringbuf_append_str(ctx.sb, "xmlns");
    if (length) { stringbuf_append_char(ctx.sb, ':'); stringbuf_append_str_n(ctx.sb, prefix, length); }
    XmlAttributeName key = {ctx.sb->str->chars, nullptr};
    for (XmlNamespaceFrame* frame = ctx.namespaces; frame; frame = frame->parent) {
        const XmlAttributeName* binding = (const XmlAttributeName*)hashmap_get(frame->attributes.names, &key);
        if (binding) return binding->value;
    }
    return nullptr;
}

struct XmlExpandedAttribute { const char* uri; const char* local; };
static uint64_t xml_expanded_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const XmlExpandedAttribute* key = (const XmlExpandedAttribute*)item;
    return hashmap_sip(key->uri, strlen(key->uri), seed0, seed1) ^ hashmap_sip(key->local, strlen(key->local), seed0, seed1);
}
static int xml_expanded_compare(const void* left, const void* right, void*) {
    const XmlExpandedAttribute* a = (const XmlExpandedAttribute*)left;
    const XmlExpandedAttribute* b = (const XmlExpandedAttribute*)right;
    int result = strcmp(a->uri, b->uri); return result ? result : strcmp(a->local, b->local);
}

static bool xml_resolve_qname(XmlInputContext& ctx, const char* name, bool attribute,
        const char* at, XmlExpandedAttribute* result) {
    const char* colon = strchr(name, ':'); result->local = colon ? colon + 1 : name;
    uint32_t cp = 0;
    if ((colon && (colon == name || strchr(colon + 1, ':'))) ||
        str_utf8_decode(result->local, strlen(result->local), &cp) <= 0 || !xml_name_start(cp) || cp == ':')
        return xml_error(ctx, at, "XML_QNAME", "Invalid namespace-qualified XML name");
    if (colon && (size_t)(colon - name) == 5 && !memcmp(name, "xmlns", 5))
        return xml_error(ctx, at, "XML_NAMESPACE", "The xmlns prefix is reserved for namespace declarations");
    result->uri = !colon && attribute ? "" : xml_namespace_uri(ctx, name, colon ? (size_t)(colon - name) : 0);
    if (colon && (!result->uri || !*result->uri)) return xml_error(ctx, at, "XML_NAMESPACE", "XML namespace prefix has no binding");
    if (!result->uri) result->uri = "";
    return true;
}

static bool xml_validate_namespaces(XmlInputContext& ctx, XmlNamespaceFrame& frame, const char* name, const char* at) {
    size_t cursor = 0; void* item = nullptr;
    while (hashmap_iter(frame.attributes.names, &cursor, &item)) {
        const XmlAttributeName* attribute = (const XmlAttributeName*)item;
        bool default_binding = !strcmp(attribute->name, "xmlns");
        if (!default_binding && strncmp(attribute->name, "xmlns:", 6)) continue;
        const char* prefix = default_binding ? "" : attribute->name + 6;
        uint32_t cp = 0;
        bool invalid_prefix = !default_binding && (!*prefix || strchr(prefix, ':') ||
            str_utf8_decode(prefix, strlen(prefix), &cp) <= 0 || !xml_name_start(cp));
        bool xml_prefix = !strcmp(prefix, "xml");
        bool xml_uri = !strcmp(attribute->value, "http://www.w3.org/XML/1998/namespace");
        if (invalid_prefix || !strcmp(prefix, "xmlns") || xml_prefix != xml_uri ||
            !strcmp(attribute->value, "http://www.w3.org/2000/xmlns/") || (!default_binding && !*attribute->value))
            return xml_error(ctx, at, "XML_NAMESPACE", "Invalid or reserved XML namespace binding");
    }
    XmlExpandedAttribute expanded = {};
    if (!xml_resolve_qname(ctx, name, false, at, &expanded)) return false;
    XmlAttributeSet expanded_names;
    expanded_names.names = hashmap_new(sizeof(XmlExpandedAttribute), 0, 0, 0, xml_expanded_hash, xml_expanded_compare, nullptr, nullptr);
    if (!expanded_names.names) return xml_error(ctx, at, "XML_ALLOCATION", "XML namespace allocation failed");
    cursor = 0;
    while (hashmap_iter(frame.attributes.names, &cursor, &item)) {
        const XmlAttributeName* attribute = (const XmlAttributeName*)item;
        if (!strcmp(attribute->name, "xmlns") || !strncmp(attribute->name, "xmlns:", 6)) continue;
        if (!xml_resolve_qname(ctx, attribute->name, true, at, &expanded)) return false;
        if (hashmap_get(expanded_names.names, &expanded))
            return xml_error(ctx, at, "XML_DUPLICATE_ATTRIBUTE", "Duplicate expanded XML attribute name");
        hashmap_set(expanded_names.names, &expanded);
        if (hashmap_oom(expanded_names.names)) return xml_error(ctx, at, "XML_ALLOCATION", "XML namespace allocation failed");
    }
    return true;
}

static Item parse_comment(XmlInputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    // Skip past the "!--" part (already consumed by caller)

    // Find comment content
    const char* comment_start = *xml;
    const char* comment_end = comment_start;

    comment_end = xml_find_terminator(comment_start, "-->");
    if (ctx.strict() && (!*comment_end || strstr(comment_start, "--") != comment_end)) {
        xml_error(ctx, comment_start, "XML_COMMENT", "Unterminated XML comment or forbidden '--' in comment text");
        return {.item = ITEM_ERROR};
    }

    // Create comment element
    ElementBuilder element = builder.element("!--");

    // Add comment content as text
    if (comment_end > comment_start) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        xml_append_literal(ctx, sb, comment_start, comment_end);
        String* comment_text = builder.createString(sb->str->chars, sb->length);
        if (comment_text && comment_text->len > 0) {
            element.child(Item{.item = s2it(comment_text)});
        }
    }

    // Skip closing -->
    if (*comment_end) {
        *xml = comment_end + 3;
    } else {
        *xml = comment_end;
    }
    return element.final();
}

static Item parse_cdata(XmlInputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    // Skip past the "![CDATA[" part (already consumed by caller)

    const char* cdata_start = *xml;

    // Find CDATA end
    *xml = xml_find_terminator(*xml, "]]>");
    if (ctx.strict() && !**xml) {
        xml_error(ctx, cdata_start, "XML_CDATA", "Unterminated XML CDATA section"); return {.item = ITEM_ERROR};
    }

    // Create CDATA content string
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    xml_append_literal(ctx, sb, cdata_start, *xml);

    if (**xml && strncmp(*xml, "]]>", 3) == 0) {
        *xml += 3; // skip ]]>
    }

    String* cdata_text = builder.createString(sb->str->chars, sb->length);
    return Item{.item = s2it(cdata_text)};
}

static Item parse_entity(XmlInputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    // Skip past the "!ENTITY" part (already consumed by caller)
    skip_whitespace(xml);

    // Parse entity name
    const char* entity_name_start = *xml;
    while (**xml && **xml != ' ' && **xml != '\t' && **xml != '\n' && **xml != '\r') {
        (*xml)++;
    }
    const char* entity_name_end = *xml;

    skip_whitespace(xml);

    // Parse entity value (quoted string or external reference)
    const char* entity_value_start = NULL;
    const char* entity_value_end = NULL;
    char quote_char = 0;
    bool is_external = false;

    if (**xml == '"' || **xml == '\'') {
        quote_char = **xml;
        (*xml)++; // skip opening quote
        entity_value_start = *xml;

        while (**xml && **xml != quote_char) {
            (*xml)++;
        }
        entity_value_end = *xml;

        if (**xml == quote_char) {
            (*xml)++; // skip closing quote
        }
    } else if (strncmp(*xml, "SYSTEM", 6) == 0 || strncmp(*xml, "PUBLIC", 6) == 0) {
        // External entity reference
        is_external = true;
        entity_value_start = *xml;
        while (**xml && **xml != '>') {
            (*xml)++;
        }
        entity_value_end = *xml;
    }

    // Skip to end of declaration
    while (**xml && **xml != '>') {
        (*xml)++;
    }
    if (**xml == '>') {
        (*xml)++; // skip >
    }

    // Create entity element
    ElementBuilder element = builder.element("!ENTITY");

    // Add entity name as "name" attribute
    if (entity_name_end > entity_name_start) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        stringbuf_append_str_n(sb, entity_name_start,
                               (size_t)(entity_name_end - entity_name_start));
        String* name_str = builder.createString(sb->str->chars, sb->length);
        element.attr("name", Item{.item = s2it(name_str)});
    }

    // Add entity value/reference as "value" attribute
    if (entity_value_end > entity_value_start) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        stringbuf_append_str_n(sb, entity_value_start,
                               (size_t)(entity_value_end - entity_value_start));
        String* value_str = builder.createString(sb->str->chars, sb->length);
        element.attr("value", Item{.item = s2it(value_str)});
    }

    // Add type attribute (internal/external)
    element.attr("type", is_external ? "external" : "internal");

    return element.final();
}

static Item parse_dtd_declaration(XmlInputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    // Parse DTD declarations like ELEMENT, ATTLIST, NOTATION
    const char* decl_start = *xml;
    const char* decl_name_end = decl_start;

    // Find end of declaration name
    while (**xml && **xml != ' ' && **xml != '\t' && **xml != '\n' && **xml != '\r') {
        (*xml)++;
        decl_name_end = *xml;
    }

    // Extract declaration name
    size_t decl_name_len = decl_name_end - decl_start;
    if (decl_name_len == 0) return {.item = ITEM_ERROR};

    // Create declaration element name with "!" prefix
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    stringbuf_append_char(sb, '!');
    stringbuf_append_str_n(sb, decl_start, (size_t)(decl_name_end - decl_start));
    String* decl_element_name = builder.createString(sb->str->chars, sb->length);

    skip_whitespace(xml);

    // Parse declaration content until >
    const char* content_start = *xml;
    int paren_count = 0;
    while (**xml && (**xml != '>' || paren_count > 0)) {
        if (**xml == '(') paren_count++;
        else if (**xml == ')') paren_count--;
        (*xml)++;
    }
    const char* content_end = *xml;

    if (**xml == '>') {
        (*xml)++; // skip >
    }

    // Create DTD declaration element
    ElementBuilder element = builder.element(decl_element_name->chars);

    // Add declaration content as text
    if (content_end > content_start) {
        stringbuf_reset(sb);
        stringbuf_append_str_n(sb, content_start, (size_t)(content_end - content_start));
        String* content_text = builder.createString(sb->str->chars, sb->length);
        if (content_text && content_text->len > 0) {
            element.child(Item{.item = s2it(content_text)});
        }
    }
    return element.final();
}

static Item parse_doctype(XmlInputContext& ctx, const char **xml, int depth) {
    MarkBuilder& builder = ctx.builder;
    // Skip past the "!DOCTYPE" part (already consumed by caller)
    skip_whitespace(xml);

    // Skip DOCTYPE name and external ID
    while (**xml && **xml != '[' && **xml != '>') {
        (*xml)++;
    }

    // If there's an internal subset [...]
    if (**xml == '[') {
        (*xml)++; // skip [

        // Create a document fragment to hold DTD declarations
        ElementBuilder dt_elmt = builder.element("!DOCTYPE");

        // Parse internal subset content
        while (**xml && **xml != ']') {
            skip_whitespace(xml);
            // the subset may end after white space; stepping over its `]`
            // made the rest of the document part of the DOCTYPE
            if (!**xml || **xml == ']') break;
            if (**xml == '<') {
                (*xml)++; // skip <
                if (**xml == '!') {
                    (*xml)++; // skip !
                    // Check for specific DTD declarations
                    if (strncmp(*xml, "ENTITY", 6) == 0) {
                        *xml += 6;
                        Item entity = parse_entity(ctx, xml);
                        if (entity.item != ITEM_ERROR) {
                            dt_elmt.child(entity);
                        }
                    } else if (strncmp(*xml, "ELEMENT", 7) == 0 ||
                               strncmp(*xml, "ATTLIST", 7) == 0 ||
                               strncmp(*xml, "NOTATION", 8) == 0) {
                        Item decl = parse_dtd_declaration(ctx, xml);
                        if (decl.item != ITEM_ERROR) {
                            dt_elmt.child(decl);
                        }
                    } else {
                        // Generic DTD declaration
                        Item decl = parse_dtd_declaration(ctx, xml);
                        if (decl.item != ITEM_ERROR) {
                            dt_elmt.child(decl);
                        }
                    }
                } else {
                    // Other elements (shouldn't happen in DTD, but handle gracefully)
                    (*xml)--; // back up to <
                    Item element = parse_element(ctx, xml, depth + 1);
                    if (element.item != ITEM_ERROR) {
                        dt_elmt.child(element);
                    }
                }
            } else {
                (*xml)++; // skip any other characters
            }
        }

        if (**xml == ']') {
            (*xml)++; // skip ]
        }

        // Skip to end of DOCTYPE
        while (**xml && **xml != '>') {
            (*xml)++;
        }
        if (**xml == '>') {
            (*xml)++; // skip >
        }

        return dt_elmt.final();
    } else {
        // No internal subset, just skip to end
        while (**xml && **xml != '>') {
            (*xml)++;
        }
        if (**xml == '>') {
            (*xml)++; // skip >
        }
        return parse_element(ctx, xml, depth); // parse next element
    }
}

static inline bool xml_is_space_char(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

static bool xml_text_is_space(const char* chars, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!xml_is_space_char(chars[i])) return false;
    }
    return true;
}

// Character data between markup, with entity and character references decoded.
static String* xml_decode_text(XmlInputContext& ctx, const char* start, const char* end) {
    if (end <= start) return nullptr;
    if (ctx.strict()) {
        const char* forbidden = strstr(start, "]]>");
        if (forbidden && forbidden < end) {
            xml_error(ctx, forbidden, "XML_CHARACTER_DATA", "']]>' is forbidden in XML character data"); return nullptr;
        }
    }
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    while (start < end) {
        if (*start == '&') {
            append_xml_reference(ctx, sb, &start, end, false);
            if (ctx.strict() && ctx.hasErrors()) return nullptr;
            continue;
        }
        // one append per run between entity references
        const char* amp = (const char*)memchr(start, '&', (size_t)(end - start));
        const char* run_end = amp ? amp : end;
        xml_append_literal(ctx, sb, start, run_end);
        start = run_end;
    }
    return ctx.builder.createString(sb->str->chars, sb->length);
}

static String* xml_trim_text(MarkBuilder& builder, String* text) {
    size_t start = 0, end = text->len;
    while (start < end && xml_is_space_char(text->chars[start])) start++;
    while (end > start && xml_is_space_char(text->chars[end - 1])) end--;
    if (start == 0 && end == text->len) return text;
    return builder.createString(text->chars + start, end - start);
}

// A comment ("!--"), DTD declaration, or processing instruction ("?target").
static bool xml_is_markup_declaration(Item child) {
    if (get_type_id(child) != LMD_TYPE_ELEMENT || !child.element) return false;
    TypeElmt* type = (TypeElmt*)child.element->type;
    return type && type->name.length > 0 &&
        (type->name.str[0] == '!' || type->name.str[0] == '?');
}

static Item parse_element(XmlInputContext& ctx, const char **xml, int depth) {
    MarkBuilder& builder = ctx.builder;
    skip_whitespace(xml);
    const char* element_start = *xml;

    if (depth >= XML_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum XML nesting depth (%d) exceeded", XML_MAX_DEPTH);
        return {.item = ITEM_ERROR};
    }

    if (**xml != '<') return {.item = ITEM_ERROR};
    (*xml)++; // skip <

    // Handle comments - create element with name "!--"
    if (strncmp(*xml, "!--", 3) == 0) {
        *xml += 3; // skip !--
        return parse_comment(ctx, xml);
    }

    // Handle CDATA sections
    if (strncmp(*xml, "![CDATA[", 8) == 0) {
        if (ctx.strict() && !depth) {
            xml_error(ctx, element_start, "XML_DOCUMENT_CONTENT", "CDATA requires a document element"); return {.item = ITEM_ERROR};
        }
        *xml += 8;
        return parse_cdata(ctx, xml);
    }

    if (ctx.strict() && **xml == '!') {
        xml_error(ctx, element_start, "XML_DECLARATION_PROFILE", "DTD declarations require an explicit XML entity policy");
        return {.item = ITEM_ERROR};
    }

    // Handle ENTITY declarations - create element with name "!ENTITY"
    if (strncmp(*xml, "!ENTITY", 7) == 0) {
        *xml += 7; // skip !ENTITY
        return parse_entity(ctx, xml);
    }

    // Handle DOCTYPE declarations - parse internal subset for entities
    if (strncmp(*xml, "!DOCTYPE", 8) == 0) {
        *xml += 8; // skip !DOCTYPE
        return parse_doctype(ctx, xml);
    }

    // Handle other DTD declarations (ELEMENT, ATTLIST, NOTATION, etc.)
    if (**xml == '!' && (strncmp(*xml + 1, "ELEMENT", 7) == 0 ||
                        strncmp(*xml + 1, "ATTLIST", 7) == 0 ||
                        strncmp(*xml + 1, "NOTATION", 8) == 0)) {
        (*xml)++; // skip !
        return parse_dtd_declaration(ctx, xml);
    }

    // Handle processing instructions - create element with name "?target"
    bool is_processing = (**xml == '?');
    if (is_processing) {
        (*xml)++; // skip ?

        // Parse target name
        String* target_name = parse_tag_name(ctx, xml);
        if (!target_name) return {.item = ITEM_ERROR};
        bool declaration = ctx.strict() && str_ieq_cstr(target_name->chars, "xml");
        if (declaration && (strcmp(target_name->chars, "xml") || depth || element_start != ctx.document_start)) {
            xml_error(ctx, element_start, "XML_DECLARATION", "XML declaration must occur once at the beginning of the document");
            return {.item = ITEM_ERROR};
        }
        if (ctx.strict() && **xml && **xml != ' ' && **xml != '\t' && **xml != '\n' && **xml != '\r' &&
            !(**xml == '?' && *(*xml + 1) == '>')) {
            xml_error(ctx, *xml, "XML_PROCESSING_INSTRUCTION", "Processing instruction data requires separating whitespace");
            return {.item = ITEM_ERROR};
        }
        if (declaration) {
            const char* attributes = *xml;
            ElementBuilder values = builder.element("xml-declaration");
            if (!parse_attributes(ctx, values, &attributes, true) || attributes[0] != '?' || attributes[1] != '>') {
                if (!ctx.hasErrors()) xml_error(ctx, attributes, "XML_DECLARATION", "Invalid XML declaration terminator");
                return {.item = ITEM_ERROR};
            }
        }

        // Create processing instruction element name "?target"
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        stringbuf_append_all(sb, 2, "?", target_name->chars);
        String* pi_name = builder.createString(sb->str->chars, sb->length);

        // Parse PI data (everything until ?>)
        skip_whitespace(xml);
        const char* pi_data_start = *xml;
        while (**xml && !(**xml == '?' && *(*xml + 1) == '>')) {
            (*xml)++;
        }
        const char* pi_data_end = *xml;
        if (ctx.strict() && !**xml) {
            xml_error(ctx, element_start, "XML_PROCESSING_INSTRUCTION", "Unterminated XML processing instruction");
            return {.item = ITEM_ERROR};
        }

        // Extract stylesheet href if this is xml-stylesheet processing instruction
        if (strcmp(target_name->chars, "xml-stylesheet") == 0) {
            // Parse the pseudo-attributes in the PI data
            const char* href_start = strstr(pi_data_start, "href=");
            if (href_start && href_start < pi_data_end) {
                href_start += 5; // skip "href="
                skip_whitespace(&href_start);

                // Extract quoted value
                char quote = *href_start;
                if (quote == '"' || quote == '\'') {
                    href_start++; // skip opening quote
                    const char* href_end = strchr(href_start, quote);
                    if (href_end && href_end < pi_data_end) {
                        size_t href_len = href_end - href_start;
                        // Allocate from pool and store in input
                        Input* input = ctx.input();
                        input->xml_stylesheet_href = pool_dup_n(input->pool, href_start, href_len);
                        if (input->xml_stylesheet_href) {
                            log_debug("[XML Parser] Found xml-stylesheet href: %s", input->xml_stylesheet_href);
                        }
                    }
                }
            }
        }

        // Skip ?>
        if (**xml == '?' && *(*xml + 1) == '>') {
            *xml += 2;
        }

        // Create processing instruction element
        ElementBuilder element = builder.element(pi_name->chars);

        // Add PI data as text content
        if (pi_data_end > pi_data_start) {
            stringbuf_reset(sb);
            xml_append_literal(ctx, sb, pi_data_start, pi_data_end);
            String* pi_data = builder.createString(sb->str->chars, sb->length);
            if (pi_data && pi_data->len > 0) {
                element.child(Item{.item = s2it(pi_data)});
            }
        }
        return element.final();
    }

    // parse tag name
    String* tag_name = parse_tag_name(ctx, xml);
    if (!tag_name) return {.item = ITEM_ERROR};

    // Create element
    ElementBuilder element = builder.element(tag_name->chars);
    XmlNamespaceFrame namespaces(ctx);

    // parse attributes
    if (!parse_attributes(ctx, element, xml, false, ctx.namespaced() ? &namespaces : nullptr) ||
        (ctx.namespaced() && !xml_validate_namespaces(ctx, namespaces, tag_name->chars, element_start))) return {.item = ITEM_ERROR};

    skip_whitespace(xml);

    // check for self-closing tag
    bool self_closing = false;
    if (**xml == '/') {
        self_closing = true;
        (*xml)++; // skip /
    }

    if (**xml != '>') return {.item = ITEM_ERROR};
    (*xml)++; // skip >

    if (!self_closing) {
        // Children are collected before they are added, because whether text
        // is significant depends on its siblings. In mixed content (text
        // beside child elements) every character is kept as written: the space
        // in `Hello <b>world</b>` separates words. Only white space between the
        // elements of element-only content and the edges of a text-only
        // element are insignificant, as data-oriented XML expects.
        ArrayList* children = arraylist_new(8);
        // parallel to `children`: 1 for character data (text or CDATA) that
        // may be trimmed, 0 for CDATA and elements, which are kept as they are
        ArrayList* trimmable = arraylist_new(8);
        bool has_element_child = false;
        bool has_text = false;

        while (**xml && !xml_closing_tag_matches(*xml, tag_name->chars, tag_name->len)) {
            if (**xml == '<') {
                if (*(*xml + 1) == '/') {
                    ctx.syncTo(*xml);
                    if (ctx.strict()) ctx.addErrorCode(ctx.tracker.location(), "XML_MISMATCHED_TAG",
                        "Mismatched XML closing tag while parsing <%s>", tag_name->chars);
                    ctx.addWarning(ctx.tracker.location(), "Mismatched XML closing tag while parsing <%s>", tag_name->chars);
                    break;
                }
                // Child element (could be regular element, comment, PI, or CDATA)
                Item child = parse_element(ctx, xml, depth + 1);
                if (child.item == ITEM_ERROR) {
                    // strict formatting input cannot recover by accepting a partial subtree.
                    if (ctx.strict()) { arraylist_free(trimmable); arraylist_free(children); return child; }
                    continue;
                }
                if (get_type_id(child) == LMD_TYPE_STRING) {
                    // CDATA is character data, never trimmed
                    has_text = true;
                } else if (!xml_is_markup_declaration(child)) {
                    // comments and processing instructions do not make content mixed
                    has_element_child = true;
                }
                arraylist_append(children, (ArrayListValue)child.item);
                arraylist_append(trimmable, (ArrayListValue)0);
            } else {
                const char* text_start = *xml;
                const char* next_tag = strchr(*xml, '<');
                *xml = next_tag ? next_tag : *xml + strlen(*xml);
                String* text = xml_decode_text(ctx, text_start, *xml);
                if (ctx.strict() && ctx.hasErrors()) {
                    arraylist_free(trimmable); arraylist_free(children); return {.item = ITEM_ERROR};
                }
                if (text && text->len > 0) {
                    if (!xml_text_is_space(text->chars, text->len)) has_text = true;
                    arraylist_append(children, (ArrayListValue)s2it(text));
                    arraylist_append(trimmable, (ArrayListValue)1);
                }
            }
        }

        bool mixed = has_element_child && has_text;
        for (int i = 0; i < arraylist_size(children); i++) {
            Item child = {.item = (uint64_t)arraylist_get(children, i)};
            if (!ctx.preserving() && !mixed && arraylist_get(trimmable, i)) {
                String* text = child.get_string();
                if (xml_text_is_space(text->chars, text->len)) continue;
                child = Item{.item = s2it(xml_trim_text(builder, text))};
            }
            element.child(child);
        }
        arraylist_free(trimmable);
        arraylist_free(children);

        // Skip matching closing tag
        if (ctx.strict() && !xml_closing_tag_matches(*xml, tag_name->chars, tag_name->len)) {
            ctx.syncTo(*xml);
            ctx.addErrorCode(ctx.tracker.location(), "XML_MISSING_TAG", "Missing XML closing tag for <%s>", tag_name->chars);
        }
        if (!xml_skip_closing_tag(ctx, xml, tag_name) && ctx.strict()) return {.item = ITEM_ERROR};
    }
    Item result = element.final();
    if (ctx.options && ctx.options->element_span && get_type_id(result) == LMD_TYPE_ELEMENT)
        ctx.options->element_span(ctx.options->context, result.element,
            (size_t)(element_start - ctx.begin), (size_t)(*xml - ctx.begin));
    return result;
}

void parse_xml_with_options(Input* input, const char* xml_string, const XmlParseOptions* options) {
    if (!xml_string || !*xml_string) {
        input->root = {.item = ITEM_NULL};
        if (options && (options->require_well_formed || options->require_namespaces)) {
            XmlInputContext ctx(input, xml_string ? xml_string : "", options);
            xml_error(ctx, ctx.begin, "XML_DOCUMENT_ELEMENT", "XML requires exactly one document element"); ctx.logErrors();
        }
        return;
    }
    XmlInputContext ctx(input, xml_string, options);
    MarkBuilder& builder = ctx.builder;

    if (ctx.strict()) for (const char* ch = ctx.begin; ch < ctx.end;) {
        uint32_t cp = 0;
        int count = str_utf8_decode(ch, (size_t)(ctx.end - ch), &cp);
        if (count <= 0 || !xml_character(cp)) {
            xml_error(ctx, ch, "XML_CHARACTER", "Invalid UTF-8 or forbidden XML character");
            ctx.logErrors(); input->root = {.item = ITEM_ERROR}; return;
        }
        ch += count;
    }

    const char* xml = xml_string;
    if (ctx.strict() && (size_t)(ctx.end - xml) >= 3 && !memcmp(xml, "\xef\xbb\xbf", 3)) xml += 3;
    ctx.document_start = xml;
    skip_whitespace(&xml);

    // Create a document root element to contain all top-level elements
    ElementBuilder doc_element = builder.element("document");

    size_t document_elements = 0;

    // Parse all top-level elements (including XML declaration, comments, PIs, and the main element)
    while (*xml) {
        skip_whitespace(&xml);
        if (!*xml) break;

        const char* old_xml = xml; // Save position to detect infinite loops

        if (*xml == '<') {
            Item element = parse_element(ctx, &xml, 0);
            if (ctx.strict() && (element.item == ITEM_ERROR || ctx.hasErrors())) {
                if (!ctx.hasErrors()) xml_error(ctx, xml, "XML_ELEMENT_SYNTAX", "Invalid XML element syntax");
                break;
            }
            if (element.item != ITEM_ERROR) {
                doc_element.child(element);

                // Check if this is an actual XML element (not processing instruction, comment, DTD, etc.)
                Element* elem = get_type_id(element) == LMD_TYPE_ELEMENT ? element.element : nullptr;
                if (elem && elem->type) {
                    TypeElmt* elmt_type = (TypeElmt*)elem->type;
                    // Count as actual element if it doesn't start with ?, !, or --
                    if (elmt_type->name.length > 0 &&
                        elmt_type->name.str[0] != '?' &&
                        elmt_type->name.str[0] != '!' &&
                        !(elmt_type->name.length >= 3 && strncmp(elmt_type->name.str, "!--", 3) == 0)) {
                        document_elements++;
                        if (ctx.strict() && document_elements > 1) {
                            xml_error(ctx, old_xml, "XML_DOCUMENT_ELEMENT", "XML requires exactly one document element"); break;
                        }
                    }
                }
            }
        } else {
            if (ctx.strict()) {
                xml_error(ctx, xml, "XML_DOCUMENT_CONTENT", "Character content outside the XML document element"); break;
            }
            // Skip any stray text content at document level
            while (*xml && *xml != '<') {
                xml++;
            }
        }

        // Safety check: ensure we always advance to prevent infinite loops
        if (xml == old_xml) {
            ctx.addWarning(ctx.tracker.location(), "Possible infinite loop detected in XML parsing, forcing advance");
            xml++; // Force advance by at least one character
        }
    }

    if (ctx.strict() && !ctx.hasErrors() && document_elements != 1)
        xml_error(ctx, xml, "XML_DOCUMENT_ELEMENT", "XML requires exactly one document element");

    // Report errors if any
    if (ctx.hasErrors()) {
        ctx.logErrors();
    }

    // Always return the document wrapper to maintain consistent structure
    // This ensures all XML content is wrapped in a <document> element
    input->root = ctx.strict() && ctx.hasErrors() ? Item{.item = ITEM_ERROR} : doc_element.final();
}

void parse_xml(Input* input, const char* xml_string) {
    parse_xml_with_options(input, xml_string, nullptr);
}

Element* parse_svg_document(Input* input, const char* svg_source) {
    if (!input || !svg_source) return nullptr;
    if (strlen(svg_source) >= 3 && (unsigned char)svg_source[0] == 0xEF &&
        (unsigned char)svg_source[1] == 0xBB && (unsigned char)svg_source[2] == 0xBF) svg_source += 3;
    // XML preserves namespace declarations, case and self-closing foreign-content boundaries.
    parse_xml(input, svg_source);
    if (get_type_id(input->root) != LMD_TYPE_ELEMENT) return nullptr;
    Element* document = input->root.element;
    for (int64_t i = 0; i < document->length; i++) {
        Item child = document->items[i];
        if (get_type_id(child) != LMD_TYPE_ELEMENT) continue;
        TypeElmt* type = (TypeElmt*)child.element->type;
        if (type && type->name.str && strcmp(type->name.str, "svg") == 0) return child.element;
    }
    log_error("svg_document: no SVG root element found in external XML document");
    return nullptr;
}
