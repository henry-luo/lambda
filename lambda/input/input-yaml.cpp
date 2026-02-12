#include "input.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "../mark_builder.hpp"
#include "../../lib/memtrack.h"
#include "../../lib/strbuf.h"
#include "../../lib/log.h"

using namespace lambda;

// ============================================================================
// YAML Parser - Full YAML 1.2 conformant parser
// ============================================================================

// forward declarations
struct YamlParser;
static Item parse_block_node(YamlParser* p, int min_indent);
static Item parse_inline_block_node(YamlParser* p, int parent_indent);
static Item parse_flow_node(YamlParser* p);
static Item parse_flow_mapping(YamlParser* p);
static Item parse_flow_sequence(YamlParser* p);
static Item parse_block_scalar(YamlParser* p, int base_indent);
static Item parse_plain_scalar(YamlParser* p, int min_indent, bool in_flow);
static Item parse_block_mapping(YamlParser* p, int map_indent);
static Item parse_block_mapping_inline(YamlParser* p, int map_indent);
static Item parse_block_sequence(YamlParser* p, int seq_indent);

// tag constants
#define TAG_NONE 0
#define TAG_STR  1
#define TAG_INT  2
#define TAG_FLOAT 3
#define TAG_BOOL 4
#define TAG_NULL 5
#define TAG_SEQ  6
#define TAG_MAP  7
#define TAG_NON_SPECIFIC 8

// anchor entry
struct AnchorEntry {
    char name[256];
    Item value;
};

// parser state
struct YamlParser {
    InputContext* ctx;
    const char* src;
    int pos;
    int len;
    int line;
    int col;

    AnchorEntry anchors[256];
    int anchor_count;
    int tag;
};

// ============================================================================
// Character utilities
// ============================================================================

static inline bool at_end(YamlParser* p) {
    return p->pos >= p->len;
}

static inline char peek(YamlParser* p) {
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static inline char peek_at(YamlParser* p, int offset) {
    int pos = p->pos + offset;
    if (pos < 0 || pos >= p->len) return '\0';
    return p->src[pos];
}

static inline void advance(YamlParser* p) {
    if (p->pos < p->len) {
        if (p->src[p->pos] == '\n') {
            p->line++;
            p->col = 0;
        } else {
            p->col++;
        }
        p->pos++;
    }
}

static inline void advance_n(YamlParser* p, int n) {
    for (int i = 0; i < n; i++) advance(p);
}

static void skip_spaces(YamlParser* p) {
    while (!at_end(p)) {
        char c = peek(p);
        if (c == ' ' || c == '\t') advance(p);
        else break;
    }
}

static void skip_comment(YamlParser* p) {
    if (peek(p) == '#') {
        while (!at_end(p) && peek(p) != '\n') advance(p);
    }
}

static void skip_spaces_and_comments(YamlParser* p) {
    skip_spaces(p);
    if (peek(p) == '#') skip_comment(p);
}

static bool skip_blank_lines(YamlParser* p) {
    while (!at_end(p)) {
        int saved = p->pos;
        int saved_line = p->line;
        int saved_col = p->col;
        skip_spaces(p);
        if (at_end(p)) return false;
        if (peek(p) == '#') {
            skip_comment(p);
            if (!at_end(p) && peek(p) == '\n') { advance(p); continue; }
            return !at_end(p);
        }
        if (peek(p) == '\n') {
            advance(p);
            continue;
        }
        p->pos = saved;
        p->line = saved_line;
        p->col = saved_col;
        return true;
    }
    return false;
}

static int current_indent(YamlParser* p) {
    int line_start = p->pos;
    while (line_start > 0 && p->src[line_start - 1] != '\n') line_start--;
    int indent = 0;
    while (line_start + indent < p->len && p->src[line_start + indent] == ' ') indent++;
    return indent;
}

static void skip_line(YamlParser* p) {
    while (!at_end(p) && peek(p) != '\n') advance(p);
    if (!at_end(p)) advance(p);
}

// get start of current line
static int line_start_pos(YamlParser* p) {
    int ls = p->pos;
    while (ls > 0 && p->src[ls - 1] != '\n') ls--;
    return ls;
}

// ============================================================================
// Document boundary detection
// ============================================================================

static bool is_doc_marker_at(YamlParser* p, int pos, char marker_char) {
    if (pos > 0 && p->src[pos - 1] != '\n') return false;
    if (pos + 3 > p->len) return false;
    if (p->src[pos] != marker_char || p->src[pos + 1] != marker_char || p->src[pos + 2] != marker_char) return false;
    if (pos + 3 == p->len) return true;
    char after = p->src[pos + 3];
    return after == '\n' || after == ' ' || after == '\t' || after == '\r';
}

static bool is_doc_start(YamlParser* p) {
    int ls = line_start_pos(p);
    if (p->pos != ls) return false;
    return is_doc_marker_at(p, p->pos, '-');
}

static bool is_doc_end(YamlParser* p) {
    int ls = line_start_pos(p);
    if (p->pos != ls) return false;
    return is_doc_marker_at(p, p->pos, '.');
}

static bool is_doc_start_at(YamlParser* p, int pos) {
    return is_doc_marker_at(p, pos, '-');
}

static bool is_doc_end_at(YamlParser* p, int pos) {
    return is_doc_marker_at(p, pos, '.');
}

// ============================================================================
// Anchor management
// ============================================================================

static void store_anchor(YamlParser* p, const char* name, Item value) {
    for (int i = 0; i < p->anchor_count; i++) {
        if (strcmp(p->anchors[i].name, name) == 0) {
            p->anchors[i].value = value;
            return;
        }
    }
    if (p->anchor_count >= 256) return;
    AnchorEntry& a = p->anchors[p->anchor_count++];
    strncpy(a.name, name, sizeof(a.name) - 1);
    a.name[sizeof(a.name) - 1] = '\0';
    a.value = value;
}

static Item resolve_alias(YamlParser* p, const char* name) {
    for (int i = p->anchor_count - 1; i >= 0; i--) {
        if (strcmp(p->anchors[i].name, name) == 0) {
            return p->anchors[i].value;
        }
    }
    log_debug("yaml: unresolved alias *%s", name);
    return p->ctx->builder.createNull();
}

// ============================================================================
// Scalar value parsing
// ============================================================================

// empty string maps to null in Lambda data model
static Item make_empty_string(YamlParser* p) {
    return p->ctx->builder.createNull();
}

// put a key-value pair into a map, using composite key encoding for non-string keys.
// non-string keys (null, int, bool, etc.) are encoded as: {"?": {"key": key_item, "value": value_item}}
// empty string keys are treated as null keys and also go through composite encoding.
static void put_key_value(YamlParser* p, MapBuilder& map, Item key_item, Item value_item) {
    TypeId ktid = get_type_id(key_item);
    if (ktid == LMD_TYPE_STRING) {
        String* s = (String*)(key_item.item & 0x00FFFFFFFFFFFFFFULL);
        if (s->len > 0) {
            map.put(p->ctx->builder.createName(s->chars), value_item);
            return;
        }
        // empty string key → treat as null key
        key_item = p->ctx->builder.createNull();
    }
    // composite key: {"?": {"key": key_item, "value": value_item}}
    MapBuilder inner = p->ctx->builder.map();
    inner.put(p->ctx->builder.createName("key"), key_item);
    inner.put(p->ctx->builder.createName("value"), value_item);
    map.put(p->ctx->builder.createName("?"), inner.final());
}

static Item make_scalar(YamlParser* p, const char* str, bool quoted) {
    if (!str) return p->ctx->builder.createNull();

    int tag = p->tag;

    if (tag == TAG_STR || tag == TAG_NON_SPECIFIC) {
        if (str[0] == '\0') return p->ctx->builder.createNull(); // empty string → null
        return p->ctx->builder.createStringItem(str);
    }
    if (tag == TAG_INT) {
        char* end;
        int64_t val = strtoll(str, &end, 10);
        if (*end == '\0') return p->ctx->builder.createInt(val);
        return p->ctx->builder.createStringItem(str);
    }
    if (tag == TAG_FLOAT) {
        char* end;
        double val = strtod(str, &end);
        if (*end == '\0') return p->ctx->builder.createFloat(val);
        return p->ctx->builder.createStringItem(str);
    }
    if (tag == TAG_NULL) return p->ctx->builder.createNull();
    if (tag == TAG_BOOL) {
        if (strcmp(str, "true") == 0 || strcmp(str, "True") == 0 || strcmp(str, "TRUE") == 0)
            return p->ctx->builder.createBool(true);
        return p->ctx->builder.createBool(false);
    }

    if (quoted) {
        if (str[0] == '\0') return p->ctx->builder.createNull(); // empty quoted string → null
        return p->ctx->builder.createStringItem(str);
    }

    const char* start = str;
    while (*start == ' ' || *start == '\t') start++;
    size_t slen = strlen(start);
    while (slen > 0 && (start[slen - 1] == ' ' || start[slen - 1] == '\t')) slen--;

    if (slen == 0) return p->ctx->builder.createNull();

    if ((slen == 4 && strncmp(start, "null", 4) == 0) ||
        (slen == 1 && start[0] == '~') ||
        (slen == 4 && strncmp(start, "Null", 4) == 0) ||
        (slen == 4 && strncmp(start, "NULL", 4) == 0)) {
        return p->ctx->builder.createNull();
    }

    if ((slen == 4 && strncmp(start, "true", 4) == 0) ||
        (slen == 4 && strncmp(start, "True", 4) == 0) ||
        (slen == 4 && strncmp(start, "TRUE", 4) == 0)) {
        return p->ctx->builder.createBool(true);
    }
    if ((slen == 5 && strncmp(start, "false", 5) == 0) ||
        (slen == 5 && strncmp(start, "False", 5) == 0) ||
        (slen == 5 && strncmp(start, "FALSE", 5) == 0)) {
        return p->ctx->builder.createBool(false);
    }

    char buf[256];
    if (slen >= sizeof(buf)) {
        char* tmp = (char*)malloc(slen + 1);
        memcpy(tmp, start, slen);
        tmp[slen] = '\0';
        Item result = p->ctx->builder.createStringItem(tmp);
        free(tmp);
        return result;
    }
    memcpy(buf, start, slen);
    buf[slen] = '\0';

    if (strcmp(buf, ".inf") == 0 || strcmp(buf, ".Inf") == 0 || strcmp(buf, ".INF") == 0) {
        return p->ctx->builder.createFloat(1.0 / 0.0);
    }
    if (strcmp(buf, "-.inf") == 0 || strcmp(buf, "-.Inf") == 0 || strcmp(buf, "-.INF") == 0) {
        return p->ctx->builder.createFloat(-1.0 / 0.0);
    }
    if (strcmp(buf, ".nan") == 0 || strcmp(buf, ".NaN") == 0 || strcmp(buf, ".NAN") == 0) {
        return p->ctx->builder.createFloat(0.0 / 0.0);
    }

    char* end;
    if (slen > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        int64_t val = strtoll(buf, &end, 16);
        if (*end == '\0') return p->ctx->builder.createInt(val);
    } else if (slen > 2 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) {
        int64_t val = strtoll(buf + 2, &end, 8);
        if (*end == '\0') return p->ctx->builder.createInt(val);
    } else {
        int64_t int_val = strtoll(buf, &end, 10);
        if (*end == '\0') return p->ctx->builder.createInt(int_val);
    }

    double float_val = strtod(buf, &end);
    if (*end == '\0' && buf[0] != '\0') {
        bool has_dot = strchr(buf, '.') != NULL;
        bool has_exp = strchr(buf, 'e') != NULL || strchr(buf, 'E') != NULL;
        if (has_dot || has_exp) {
            return p->ctx->builder.createFloat(float_val);
        }
    }

    return p->ctx->builder.createStringItem(buf);
}

// ============================================================================
// Double-quoted string parsing
// ============================================================================

static Item parse_double_quoted(YamlParser* p) {
    advance(p); // skip opening "
    StrBuf* sb = strbuf_new_cap(64);

    while (!at_end(p) && peek(p) != '"') {
        if (peek(p) == '\\') {
            advance(p);
            if (at_end(p)) break;
            char c = peek(p);
            advance(p);
            switch (c) {
                case '0': strbuf_append_char(sb, '\0'); break;
                case 'a': strbuf_append_char(sb, '\a'); break;
                case 'b': strbuf_append_char(sb, '\b'); break;
                case 't': strbuf_append_char(sb, '\t'); break;
                case '\t': strbuf_append_char(sb, '\t'); break;
                case 'n': strbuf_append_char(sb, '\n'); break;
                case 'v': strbuf_append_char(sb, '\v'); break;
                case 'f': strbuf_append_char(sb, '\f'); break;
                case 'r': strbuf_append_char(sb, '\r'); break;
                case 'e': strbuf_append_char(sb, '\x1b'); break;
                case ' ': strbuf_append_char(sb, ' '); break;
                case '"': strbuf_append_char(sb, '"'); break;
                case '/': strbuf_append_char(sb, '/'); break;
                case '\\': strbuf_append_char(sb, '\\'); break;
                case 'N': strbuf_append_char(sb, '\xc2'); strbuf_append_char(sb, '\x85'); break;
                case '_': strbuf_append_char(sb, '\xc2'); strbuf_append_char(sb, '\xa0'); break;
                case 'L':
                    strbuf_append_char(sb, '\xe2');
                    strbuf_append_char(sb, '\x80');
                    strbuf_append_char(sb, '\xa8');
                    break;
                case 'P':
                    strbuf_append_char(sb, '\xe2');
                    strbuf_append_char(sb, '\x80');
                    strbuf_append_char(sb, '\xa9');
                    break;
                case 'x': {
                    char hex[3] = {0};
                    for (int i = 0; i < 2 && !at_end(p); i++) { hex[i] = peek(p); advance(p); }
                    unsigned int val = strtoul(hex, NULL, 16);
                    append_codepoint_utf8_strbuf(sb, val);
                    break;
                }
                case 'u': {
                    char hex[5] = {0};
                    for (int i = 0; i < 4 && !at_end(p); i++) { hex[i] = peek(p); advance(p); }
                    unsigned int val = strtoul(hex, NULL, 16);
                    append_codepoint_utf8_strbuf(sb, val);
                    break;
                }
                case 'U': {
                    char hex[9] = {0};
                    for (int i = 0; i < 8 && !at_end(p); i++) { hex[i] = peek(p); advance(p); }
                    unsigned int val = strtoul(hex, NULL, 16);
                    append_codepoint_utf8_strbuf(sb, val);
                    break;
                }
                case '\n': {
                    // escaped newline - skip leading whitespace on next line
                    while (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
                    break;
                }
                default:
                    strbuf_append_char(sb, '\\');
                    strbuf_append_char(sb, c);
                    break;
            }
        } else if (peek(p) == '\n') {
            // line folding in double-quoted: trim trailing ws, fold
            {
                int blen = sb->length;
                while (blen > 0 && (sb->str[blen - 1] == ' ' || sb->str[blen - 1] == '\t')) blen--;
                sb->length = blen; sb->str[blen] = '\0';
            }
            advance(p);
            int empty_lines = 0;
            while (!at_end(p)) {
                int start = p->pos;
                while (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
                if (!at_end(p) && peek(p) == '\n') {
                    empty_lines++;
                    advance(p);
                } else {
                    break;
                }
            }
            if (empty_lines > 0) {
                for (int i = 0; i < empty_lines; i++) strbuf_append_char(sb, '\n');
            } else {
                strbuf_append_char(sb, ' ');
            }
        } else {
            strbuf_append_char(sb, peek(p));
            advance(p);
        }
    }

    if (!at_end(p) && peek(p) == '"') advance(p);
    Item result = p->ctx->builder.createStringItem(sb->str);
    strbuf_free(sb);
    return result;
}

// ============================================================================
// Single-quoted string parsing
// ============================================================================

static Item parse_single_quoted(YamlParser* p) {
    advance(p); // skip opening '
    StrBuf* sb = strbuf_new_cap(64);

    while (!at_end(p)) {
        if (peek(p) == '\'') {
            if (peek_at(p, 1) == '\'') {
                strbuf_append_char(sb, '\'');
                advance(p);
                advance(p);
            } else {
                advance(p);
                break;
            }
        } else if (peek(p) == '\n') {
            // line folding
            {
                int blen = sb->length;
                while (blen > 0 && (sb->str[blen - 1] == ' ' || sb->str[blen - 1] == '\t')) blen--;
                sb->length = blen; sb->str[blen] = '\0';
            }
            advance(p);
            int empty_lines = 0;
            while (!at_end(p)) {
                while (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
                if (!at_end(p) && peek(p) == '\n') {
                    empty_lines++;
                    advance(p);
                } else {
                    break;
                }
            }
            if (empty_lines > 0) {
                for (int i = 0; i < empty_lines; i++) strbuf_append_char(sb, '\n');
            } else {
                strbuf_append_char(sb, ' ');
            }
        } else {
            strbuf_append_char(sb, peek(p));
            advance(p);
        }
    }

    Item result = p->ctx->builder.createStringItem(sb->str);
    strbuf_free(sb);
    return result;
}

// ============================================================================
// Tag parsing
// ============================================================================

static int parse_tag(YamlParser* p) {
    if (peek(p) != '!') return TAG_NONE;
    advance(p);

    if (peek(p) == '!') {
        advance(p);
        char tag_name[64];
        int i = 0;
        while (!at_end(p) && peek(p) != ' ' && peek(p) != '\t' &&
               peek(p) != '\n' && peek(p) != ',' && peek(p) != ']' &&
               peek(p) != '}' && i < 62) {
            tag_name[i++] = peek(p);
            advance(p);
        }
        tag_name[i] = '\0';
        skip_spaces(p);

        if (strcmp(tag_name, "str") == 0) return TAG_STR;
        if (strcmp(tag_name, "int") == 0) return TAG_INT;
        if (strcmp(tag_name, "float") == 0) return TAG_FLOAT;
        if (strcmp(tag_name, "bool") == 0) return TAG_BOOL;
        if (strcmp(tag_name, "null") == 0) return TAG_NULL;
        if (strcmp(tag_name, "seq") == 0) return TAG_SEQ;
        if (strcmp(tag_name, "map") == 0) return TAG_MAP;
        // unknown tags like !!omap, !!python/... - ignore
        return TAG_NONE;
    } else if (peek(p) == '<') {
        advance(p);
        while (!at_end(p) && peek(p) != '>') advance(p);
        if (!at_end(p)) advance(p);
        skip_spaces(p);
        return TAG_NONE;
    } else if (peek(p) == ' ' || peek(p) == '\t' || peek(p) == '\n' || peek(p) == '\0' ||
               peek(p) == ',' || peek(p) == ']' || peek(p) == '}') {
        skip_spaces(p);
        return TAG_NON_SPECIFIC;
    } else {
        // local tag like !foo
        while (!at_end(p) && peek(p) != ' ' && peek(p) != '\t' &&
               peek(p) != '\n' && peek(p) != ',' && peek(p) != ']' &&
               peek(p) != '}') advance(p);
        skip_spaces(p);
        return TAG_NONE;
    }
}

// ============================================================================
// Anchor/alias parsing
// ============================================================================

static bool is_anchor_char(char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0') return false;
    if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') return false;
    return true;
}

static const char* parse_anchor(YamlParser* p) {
    if (peek(p) != '&') return NULL;
    advance(p);
    static char name[256];
    int i = 0;
    while (!at_end(p) && is_anchor_char(peek(p)) && i < 254) {
        name[i++] = peek(p);
        advance(p);
    }
    name[i] = '\0';
    skip_spaces(p);
    return name;
}

static Item parse_alias(YamlParser* p) {
    advance(p); // skip *
    char name[256];
    int i = 0;
    while (!at_end(p) && is_anchor_char(peek(p)) && i < 254) {
        name[i++] = peek(p);
        advance(p);
    }
    name[i] = '\0';
    return resolve_alias(p, name);
}

// ============================================================================
// Node property parsing (tags and anchors)
// ============================================================================

static const char* parse_node_properties(YamlParser* p) {
    const char* anchor_name = NULL;
    p->tag = TAG_NONE;

    while (!at_end(p) && (peek(p) == '!' || peek(p) == '&')) {
        if (peek(p) == '!') {
            int tag = parse_tag(p);
            if (tag != TAG_NONE) p->tag = tag;
        } else if (peek(p) == '&') {
            anchor_name = parse_anchor(p);
        }
        skip_spaces(p);
    }

    return anchor_name;
}

// ============================================================================
// Mapping indicator checks
// ============================================================================

static bool is_mapping_indicator(YamlParser* p) {
    if (peek(p) != ':') return false;
    char next = peek_at(p, 1);
    return next == ' ' || next == '\t' || next == '\n' || next == '\0';
}

static bool is_flow_mapping_indicator(YamlParser* p) {
    if (peek(p) != ':') return false;
    char next = peek_at(p, 1);
    return next == ' ' || next == '\t' || next == '\n' || next == '\0' ||
           next == ',' || next == ']' || next == '}';
}

// check if current line (from offset) contains a mapping key (key: value)
// this must handle all printable ASCII chars in keys without infinite loops
static bool is_block_mapping_key(YamlParser* p, int offset) {
    int pos = p->pos + offset;
    int start_pos = pos;

    // check for explicit key indicator
    if (pos < p->len && p->src[pos] == '?') {
        char next = (pos + 1 < p->len) ? p->src[pos + 1] : '\0';
        if (next == ' ' || next == '\t' || next == '\n' || next == '\0') return true;
    }

    // only treat quotes as quoted strings if they START the key (at position 0 of scan)
    if (pos < p->len && p->src[pos] == '"') {
        // skip double-quoted string at start
        pos++;
        while (pos < p->len && p->src[pos] != '"' && p->src[pos] != '\n') {
            if (p->src[pos] == '\\' && pos + 1 < p->len && p->src[pos + 1] != '\n') pos++;
            pos++;
        }
        if (pos < p->len && p->src[pos] == '"') pos++;
        // after quoted key, look for ': '
        while (pos < p->len && p->src[pos] != '\n') {
            if (p->src[pos] == ':') {
                char next = (pos + 1 < p->len) ? p->src[pos + 1] : '\0';
                if (next == ' ' || next == '\t' || next == '\n' || next == '\0') return true;
            }
            if (p->src[pos] == '#') {
                if (pos > start_pos) {
                    char prev = p->src[pos - 1];
                    if (prev == ' ' || prev == '\t') return false;
                }
            }
            pos++;
        }
        return false;
    }
    if (pos < p->len && p->src[pos] == '\'') {
        // skip single-quoted string at start
        pos++;
        while (pos < p->len && p->src[pos] != '\n') {
            if (p->src[pos] == '\'') {
                if (pos + 1 < p->len && p->src[pos + 1] == '\'') {
                    pos += 2;
                    continue;
                }
                break;
            }
            pos++;
        }
        if (pos < p->len && p->src[pos] == '\'') pos++;
        // after quoted key, look for ': '
        while (pos < p->len && p->src[pos] != '\n') {
            if (p->src[pos] == ':') {
                char next = (pos + 1 < p->len) ? p->src[pos + 1] : '\0';
                if (next == ' ' || next == '\t' || next == '\n' || next == '\0') return true;
            }
            if (p->src[pos] == '#') {
                if (pos > start_pos) {
                    char prev = p->src[pos - 1];
                    if (prev == ' ' || prev == '\t') return false;
                }
            }
            pos++;
        }
        return false;
    }

    // for plain scalar keys, just scan for ': ' (colon followed by space)
    // treat quotes in the middle as regular characters
    while (pos < p->len && p->src[pos] != '\n') {
        char c = p->src[pos];
        if (c == '#' && pos > start_pos) {
            char prev = p->src[pos - 1];
            if (prev == ' ' || prev == '\t') return false; // comment before any colon found
        } else if (c == ':') {
            char next = (pos + 1 < p->len) ? p->src[pos + 1] : '\0';
            if (next == ' ' || next == '\t' || next == '\n' || next == '\0') return true;
        }
        pos++;
    }
    return false;
}

// ============================================================================
// Plain scalar parsing
// ============================================================================

static Item parse_plain_scalar(YamlParser* p, int min_indent, bool in_flow) {
    StrBuf* sb = strbuf_new_cap(64);

    while (!at_end(p)) {
        bool got_content = false;
        while (!at_end(p) && peek(p) != '\n') {
            char c = peek(p);
            if (in_flow && (c == ',' || c == ']' || c == '}' || c == '{' || c == '[')) break;
            if (in_flow ? is_flow_mapping_indicator(p) : is_mapping_indicator(p)) break;
            if (c == '#' && p->pos > 0 && (p->src[p->pos - 1] == ' ' || p->src[p->pos - 1] == '\t')) break;
            // doc boundaries
            {
                int lp = line_start_pos(p);
                if (p->pos == lp) {
                    if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) goto done_plain;
                }
            }
            strbuf_append_char(sb, c);
            advance(p);
            got_content = true;
        }

        if (!got_content && sb->length == 0) break;

        // trim trailing spaces
        {
            int blen = sb->length;
            while (blen > 0 && (sb->str[blen - 1] == ' ' || sb->str[blen - 1] == '\t')) blen--;
            sb->length = blen; sb->str[blen] = '\0';
        }

        if (at_end(p) || peek(p) != '\n') break;

        int saved_pos = p->pos;
        int saved_line = p->line;
        int saved_col = p->col;
        advance(p);

        int empty_lines = 0;
        while (!at_end(p)) {
            int spos = p->pos;
            while (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
            if (!at_end(p) && peek(p) == '\n') {
                empty_lines++;
                advance(p);
                continue;
            }
            break;
        }

        if (at_end(p)) { p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break; }

        // doc boundary
        {
            int lp = line_start_pos(p);
            if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) {
                p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break;
            }
        }

        int next_indent = current_indent(p);

        if (in_flow) {
            int check_pos = p->pos;
            while (check_pos < p->len && (p->src[check_pos] == ' ' || p->src[check_pos] == '\t')) check_pos++;
            if (check_pos < p->len) {
                char nc = p->src[check_pos];
                if (nc == '#' || nc == ']' || nc == '}' || nc == ',') {
                    p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break;
                }
            }
            if (empty_lines > 0) {
                for (int i = 0; i < empty_lines; i++) strbuf_append_char(sb, '\n');
            } else {
                strbuf_append_char(sb, ' ');
            }
            while (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
            continue;
        }

        if (next_indent <= min_indent) {
            p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break;
        }

        // check for block indicators on next line
        {
            int check_pos = p->pos;
            while (check_pos < p->len && p->src[check_pos] == ' ') check_pos++;
            if (check_pos < p->len) {
                char nc = p->src[check_pos];
                if (nc == '-' && check_pos + 1 < p->len &&
                    (p->src[check_pos + 1] == ' ' || p->src[check_pos + 1] == '\n' || p->src[check_pos + 1] == '\0')) {
                    // only treat as block sequence indicator if at parent indent or below
                    if (next_indent <= min_indent) {
                        p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break;
                    }
                }
                if (nc == '#') { p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break; }
                if (nc == '?' && check_pos + 1 < p->len &&
                    (p->src[check_pos + 1] == ' ' || p->src[check_pos + 1] == '\n')) {
                    p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break;
                }
                // check for mapping key on next line
                bool has_map_colon = false;
                int cpos = check_pos;
                bool in_dq = false, in_sq = false;
                while (cpos < p->len && p->src[cpos] != '\n') {
                    if (p->src[cpos] == '"' && !in_sq) {
                        in_dq = !in_dq;
                    } else if (p->src[cpos] == '\'' && !in_dq) {
                        in_sq = !in_sq;
                    } else if (!in_dq && !in_sq && p->src[cpos] == ':') {
                        char nx = (cpos + 1 < p->len) ? p->src[cpos + 1] : '\0';
                        if (nx == ' ' || nx == '\t' || nx == '\n' || nx == '\0') {
                            has_map_colon = true; break;
                        }
                    }
                    cpos++;
                }
                if (has_map_colon) {
                    p->pos = saved_pos; p->line = saved_line; p->col = saved_col; break;
                }
            }
        }

        if (empty_lines > 0) {
            for (int i = 0; i < empty_lines; i++) strbuf_append_char(sb, '\n');
        } else {
            strbuf_append_char(sb, ' ');
        }
        while (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
    }
done_plain:

    Item item = make_scalar(p, sb->str, false);
    strbuf_free(sb);
    return item;
}

// ============================================================================
// Block scalar parsing (| and >)
// ============================================================================

static Item parse_block_scalar(YamlParser* p, int base_indent) {
    char indicator = peek(p);
    advance(p);

    int chomp = 0; // 0=clip, 1=strip, 2=keep
    int explicit_indent = 0;

    // parse header modifiers (1-2 chars: chomp and/or indent indicator)
    for (int i = 0; i < 2 && !at_end(p); i++) {
        char c = peek(p);
        if (c == '-') { chomp = 1; advance(p); }
        else if (c == '+') { chomp = 2; advance(p); }
        else if (c >= '1' && c <= '9') { explicit_indent = c - '0'; advance(p); }
        else break;
    }

    skip_spaces_and_comments(p);
    if (!at_end(p) && peek(p) == '\n') advance(p);
    else if (at_end(p)) {
        // empty block scalar at end of file
        if (chomp == 2) return make_empty_string(p);
        return make_empty_string(p);
    }

    // determine content indent
    int content_indent = 0;
    if (explicit_indent > 0) {
        content_indent = base_indent + explicit_indent;
    } else {
        // auto-detect: find first non-empty line
        int saved = p->pos;
        int saved_line = p->line;
        int saved_col = p->col;
        while (!at_end(p)) {
            int spaces = 0;
            while (!at_end(p) && peek(p) == ' ') { advance(p); spaces++; }
            if (at_end(p) || peek(p) == '\n') {
                if (!at_end(p)) advance(p);
                continue;
            }
            // handle tab-started lines
            if (peek(p) == '\t' && spaces > base_indent) {
                content_indent = spaces;
                break;
            }
            content_indent = spaces;
            break;
        }
        p->pos = saved;
        p->line = saved_line;
        p->col = saved_col;
        if (content_indent <= base_indent) content_indent = base_indent + 1;
    }

    StrBuf* sb = strbuf_new_cap(64);
    int trailing_newlines = 0;
    bool last_content_more = false; // whether last non-empty content line was more-indented
    bool had_content = false; // whether we've had any non-empty content line

    while (!at_end(p)) {
        // doc boundary
        {
            int lp = line_start_pos(p);
            if (p->pos == lp && (is_doc_start_at(p, lp) || is_doc_end_at(p, lp))) break;
        }

        int spaces = 0;
        int line_start = p->pos;
        while (!at_end(p) && peek(p) == ' ') { advance(p); spaces++; }

        if (at_end(p)) {
            // end of file: trailing whitespace-only content
            // if this line had whitespace beyond content_indent, it's content
            if (spaces >= content_indent && spaces > 0) {
                // a trailing line of spaces - counts as content line
                for (int i = 0; i < trailing_newlines; i++) strbuf_append_char(sb, '\n');
                trailing_newlines = 0;
                if (indicator == '|') {
                    for (int i = content_indent; i < spaces; i++) strbuf_append_char(sb, ' ');
                }
                strbuf_append_char(sb, '\n');
                trailing_newlines = 0;
            } else {
                trailing_newlines++;
            }
            break;
        }

        if (peek(p) == '\n') {
            // empty line: check if line had spaces BEYOND content_indent (whitespace content)
            if (spaces > content_indent) {
                // whitespace-only content line (spaces beyond indent are content)
                for (int i = 0; i < trailing_newlines; i++) strbuf_append_char(sb, '\n');
                trailing_newlines = 0;
                if (indicator == '|') {
                    for (int i = content_indent; i < spaces; i++) strbuf_append_char(sb, ' ');
                    strbuf_append_char(sb, '\n');
                } else {
                    // folded: whitespace-only line
                    if (sb->length > 0 && sb->str[sb->length - 1] == ' ') {
                        sb->str[sb->length - 1] = '\n';
                    }
                    for (int i = content_indent; i < spaces; i++) strbuf_append_char(sb, ' ');
                    strbuf_append_char(sb, '\n');
                    trailing_newlines = 0;
                }
            } else {
                trailing_newlines++;
            }
            advance(p);
            continue;
        }

        // check for tab character at or after content indent
        bool has_tab_content = false;
        if (peek(p) == '\t' && spaces >= content_indent) {
            has_tab_content = true;
        }

        if (!has_tab_content && spaces < content_indent) {
            // less indent: end of block scalar
            if (peek(p) == '#' && spaces < content_indent) {
                // comment at reduced indent
            }
            p->pos = line_start;
            p->col = 0;
            int ls = line_start;
            while (ls > 0 && p->src[ls - 1] != '\n') ls--;
            break;
        }

        // output buffered newlines
        int flushed_newlines = trailing_newlines;
        for (int i = 0; i < trailing_newlines; i++) strbuf_append_char(sb, '\n');
        trailing_newlines = 0;

        if (indicator == '|') {
            // literal: preserve extra indentation
            for (int i = content_indent; i < spaces; i++) strbuf_append_char(sb, ' ');
            // also output any tabs
            if (has_tab_content) {
                while (!at_end(p) && peek(p) == '\t') {
                    strbuf_append_char(sb, '\t');
                    advance(p);
                }
            }
        } else {
            // folded: more-indented lines preserve structure
            if (spaces > content_indent || has_tab_content) {
                // more-indented: start new line, preserve extra indentation
                if (sb->length > 0) {
                    char last = sb->str[sb->length - 1];
                    if (last == ' ') {
                        sb->str[sb->length - 1] = '\n';
                    } else if (last == '\n' && flushed_newlines > 0 && !last_content_more && had_content) {
                        // same-indent → empty lines → more-indented
                        // empty lines already flushed, but the fold line break
                        // (same→empty) was absorbed — need transition newline
                        strbuf_append_char(sb, '\n');
                    } else if (last != '\n') {
                        // transition from normal content to more-indented
                        strbuf_append_char(sb, '\n');
                    }
                }
                last_content_more = true;
                for (int i = content_indent; i < spaces; i++) strbuf_append_char(sb, ' ');
                if (has_tab_content) {
                    while (!at_end(p) && peek(p) == '\t') {
                        strbuf_append_char(sb, '\t');
                        advance(p);
                    }
                }
            } else {
                last_content_more = false;
            }
        }

        // read content of line
        while (!at_end(p) && peek(p) != '\n') {
            strbuf_append_char(sb, peek(p));
            advance(p);
        }
        had_content = true;

        if (!at_end(p) && peek(p) == '\n') {
            advance(p);
            if (indicator == '|') {
                strbuf_append_char(sb, '\n');
            } else {
                // folded: check next line
                // look ahead to determine how to join
                int saved = p->pos;
                int next_spaces = 0;
                while (saved + next_spaces < p->len && p->src[saved + next_spaces] == ' ') next_spaces++;

                bool next_empty = false;
                if (saved + next_spaces >= p->len) {
                    next_empty = true;
                } else {
                    char nc = p->src[saved + next_spaces];
                    next_empty = (nc == '\n');
                }

                bool next_tab = false;
                if (!next_empty && saved + next_spaces < p->len) {
                    next_tab = (p->src[saved + next_spaces] == '\t' && next_spaces >= content_indent);
                }

                bool next_more = (next_spaces > content_indent && !next_empty) || next_tab;
                bool curr_more = (spaces > content_indent) || has_tab_content;

                if (next_empty) {
                    if (curr_more) {
                        // more-indented line keeps its line break
                        strbuf_append_char(sb, '\n');
                    }
                    // else: same-indent → line break absorbed by empty line
                } else if (next_more || curr_more) {
                    strbuf_append_char(sb, '\n');
                } else {
                    strbuf_append_char(sb, ' ');
                }
            }
        }
    }

    // apply chomping
    if (indicator == '>') {
        // remove trailing spaces from folding
        int blen = sb->length;
        while (blen > 0 && sb->str[blen - 1] == ' ') blen--;
        sb->length = blen; sb->str[blen] = '\0';
    }

    bool has_content = (sb->length > 0);

    // remove the final newline that we always add (we'll re-add based on chomping)
    {
        int blen = sb->length;
        if (blen > 0 && sb->str[blen - 1] == '\n') {
            blen--;
            sb->length = blen; sb->str[blen] = '\0';
        }
    }

    if (chomp == 0) {
        // clip: single trailing newline (only if there was content)
        if (has_content) strbuf_append_char(sb, '\n');
    } else if (chomp == 1) {
        // strip: no trailing newline
    } else if (chomp == 2) {
        // keep: preserve all trailing newlines
        if (has_content) strbuf_append_char(sb, '\n');
        for (int i = 0; i < trailing_newlines; i++) strbuf_append_char(sb, '\n');
    }

    Item result;
    if (sb->length == 0) {
        result = make_empty_string(p);
    } else {
        result = p->ctx->builder.createStringItem(sb->str);
    }
    strbuf_free(sb);
    return result;
}

// ============================================================================
// Flow sequence parsing [ ... ]
// ============================================================================

static Item parse_flow_sequence(YamlParser* p) {
    advance(p); // skip [

    ArrayBuilder arr = p->ctx->builder.array();

    while (!at_end(p)) {
        int loop_guard = p->pos;

        skip_spaces(p);
        while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }
        skip_spaces_and_comments(p);

        if (at_end(p)) break;
        if (peek(p) == ']') { advance(p); break; }

        // handle explicit key in flow sequence: ? key : value
        bool explicit_key = false;
        if (peek(p) == '?') {
            char nx = peek_at(p, 1);
            if (nx == ' ' || nx == '\t' || nx == '\n') {
                explicit_key = true;
                advance(p);
                skip_spaces(p);
                while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }
            }
        }

        Item value = parse_flow_node(p);

        skip_spaces(p);
        while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }
        skip_spaces_and_comments(p);

        if (!at_end(p) && is_flow_mapping_indicator(p)) {
            advance(p); // skip :
            skip_spaces(p);
            while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }

            Item map_value;
            if (at_end(p) || peek(p) == ',' || peek(p) == ']') {
                map_value = p->ctx->builder.createNull();
            } else {
                map_value = parse_flow_node(p);
            }

            MapBuilder map = p->ctx->builder.map();
            put_key_value(p, map, value, map_value);
            value = map.final();
        } else if (explicit_key) {
            // explicit key with no value
            MapBuilder map = p->ctx->builder.map();
            put_key_value(p, map, value, p->ctx->builder.createNull());
            value = map.final();
        }

        arr.append(value);

        while (!at_end(p)) {
            skip_spaces_and_comments(p);
            if (peek(p) == '\n') { advance(p); continue; }
            break;
        }

        if (!at_end(p) && peek(p) == ',') advance(p);

        if (p->pos == loop_guard && !at_end(p)) advance(p);
    }

    return arr.final();
}

// ============================================================================
// Flow mapping parsing { ... }
// ============================================================================

static Item parse_flow_mapping(YamlParser* p) {
    advance(p); // skip {

    MapBuilder map = p->ctx->builder.map();

    while (!at_end(p)) {
        int loop_guard = p->pos;

        skip_spaces(p);
        while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }
        skip_spaces_and_comments(p);

        if (at_end(p)) break;
        if (peek(p) == '}') { advance(p); break; }

        bool explicit_key = false;
        if (peek(p) == '?') {
            char next = peek_at(p, 1);
            if (next == ' ' || next == '\t' || next == '\n') {
                explicit_key = true;
                advance(p);
                skip_spaces(p);
                while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }
            }
        }

        const char* anchor_name = parse_node_properties(p);
        int key_tag = p->tag;

        // skip whitespace after tag/anchor
        skip_spaces(p);
        while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }

        Item key_item;

        // check if tag/anchor is the key itself (empty key with tag)
        if (key_tag != TAG_NONE && !at_end(p) && peek(p) == ':' && is_flow_mapping_indicator(p)) {
            // tag like !!str acts as an empty key → null
            key_item = p->ctx->builder.createNull();
        } else if (peek(p) == '"') {
            key_item = parse_double_quoted(p);
        } else if (peek(p) == '\'') {
            key_item = parse_single_quoted(p);
        } else if (peek(p) == '[') {
            key_item = parse_flow_sequence(p);
        } else if (peek(p) == '{') {
            key_item = parse_flow_mapping(p);
        } else if (!at_end(p) && peek(p) == ':' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\t' ||
                   peek_at(p, 1) == ',' || peek_at(p, 1) == '}' || peek_at(p, 1) == '\n' || peek_at(p, 1) == '\0')) {
            // empty key → null
            key_item = p->ctx->builder.createNull();
        } else {
            key_item = parse_plain_scalar(p, -1, true);
        }

        if (anchor_name) store_anchor(p, anchor_name, key_item);

        // skip whitespace/comments/newlines before colon
        while (!at_end(p)) {
            skip_spaces(p);
            if (peek(p) == '#') { skip_comment(p); continue; }
            if (peek(p) == '\n') { advance(p); continue; }
            break;
        }

        Item value;
        if (!at_end(p) && peek(p) == ':') {
            char nc = peek_at(p, 1);
            if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\0' ||
                nc == ',' || nc == '}') {
                advance(p);
                skip_spaces(p);
                while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }
                if (at_end(p) || peek(p) == ',' || peek(p) == '}') {
                    value = p->ctx->builder.createNull();
                } else {
                    value = parse_flow_node(p);
                }
            } else {
                // adjacent value: "key":value  (no space after colon)
                advance(p); // skip :
                value = parse_flow_node(p);
            }
        } else {
            value = p->ctx->builder.createNull();
        }

        put_key_value(p, map, key_item, value);

        while (!at_end(p)) {
            skip_spaces_and_comments(p);
            if (peek(p) == '\n') { advance(p); continue; }
            break;
        }

        if (!at_end(p) && peek(p) == ',') advance(p);

        if (p->pos == loop_guard && !at_end(p)) advance(p);
    }

    return map.final();
}

// ============================================================================
// Flow node parsing
// ============================================================================

static Item parse_flow_node(YamlParser* p) {
    skip_spaces(p);
    while (!at_end(p) && peek(p) == '\n') { advance(p); skip_spaces(p); }

    if (at_end(p)) return p->ctx->builder.createNull();

    const char* anchor_name = parse_node_properties(p);
    int tag = p->tag;

    // skip whitespace/newlines after tag/anchor (tag and content can be on separate lines in flow)
    while (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t' || peek(p) == '\n')) advance(p);

    if (at_end(p)) {
        if (tag == TAG_STR || tag == TAG_NON_SPECIFIC) {
            Item result = make_empty_string(p);
            if (anchor_name) store_anchor(p, anchor_name, result);
            return result;
        }
        return p->ctx->builder.createNull();
    }

    char c = peek(p);
    Item result;

    if (c == '[') {
        result = parse_flow_sequence(p);
    } else if (c == '{') {
        result = parse_flow_mapping(p);
    } else if (c == '"') {
        result = parse_double_quoted(p);
    } else if (c == '\'') {
        result = parse_single_quoted(p);
    } else if (c == '*') {
        result = parse_alias(p);
    } else if (c == ',' || c == ']' || c == '}') {
        if (tag == TAG_STR || tag == TAG_NON_SPECIFIC) {
            result = make_empty_string(p);
        } else {
            result = p->ctx->builder.createNull();
        }
    } else {
        result = parse_plain_scalar(p, -1, true);
    }

    if (anchor_name) store_anchor(p, anchor_name, result);
    return result;
}

// ============================================================================
// Block sequence parsing
// ============================================================================

static Item parse_block_sequence(YamlParser* p, int seq_indent) {
    ArrayBuilder arr = p->ctx->builder.array();
    int max_entries = p->len;

    while (!at_end(p)) {
        if (--max_entries < 0) { log_error("yaml: block sequence max entries at pos %d", p->pos); break; }
        int loop_guard = p->pos;

        if (!skip_blank_lines(p)) break;

        // doc boundary
        {
            int lp = line_start_pos(p);
            if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) break;
        }

        int indent = current_indent(p);
        if (indent != seq_indent) break;

        int lstart = line_start_pos(p);
        int dash_pos = lstart + indent;

        if (dash_pos >= p->len || p->src[dash_pos] != '-') break;
        char after_dash = (dash_pos + 1 < p->len) ? p->src[dash_pos + 1] : '\0';
        if (after_dash != ' ' && after_dash != '\t' && after_dash != '\n' && after_dash != '\0') break;

        p->pos = lstart;
        p->col = 0;
        advance_n(p, indent);
        advance(p); // skip -
        if (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);

        Item value = parse_inline_block_node(p, seq_indent);
        arr.append(value);

        if (p->pos == loop_guard) break;
    }

    return arr.final();
}

// ============================================================================
// Block mapping parsing
// ============================================================================

static Item parse_block_mapping(YamlParser* p, int map_indent) {
    MapBuilder map = p->ctx->builder.map();
    int max_entries = p->len;

    while (!at_end(p)) {
        if (--max_entries < 0) { log_error("yaml: block mapping max entries at pos %d", p->pos); break; }
        int loop_guard = p->pos;

        if (!skip_blank_lines(p)) break;

        // doc boundary
        {
            int lp = line_start_pos(p);
            if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) break;
        }

        int indent = current_indent(p);
        if (indent != map_indent) break;

        int lstart = line_start_pos(p);
        p->pos = lstart;
        p->col = 0;
        advance_n(p, indent);

        const char* key_anchor = parse_node_properties(p);
        int key_tag = p->tag;

        // handle case where anchor/tag is alone on line (value on next line)
        if (key_anchor && (at_end(p) || peek(p) == '\n')) {
            if (!at_end(p)) advance(p);
            // look ahead for what follows
            int saved = p->pos;
            int saved_line = p->line;
            int saved_col = p->col;
            skip_blank_lines(p);
            if (!at_end(p)) {
                int ni = current_indent(p);
                int nlp = line_start_pos(p);
                char nc = (nlp + ni < p->len) ? p->src[nlp + ni] : '\0';
                if (nc == '-' && ni >= map_indent) {
                    char ad = (nlp + ni + 1 < p->len) ? p->src[nlp + ni + 1] : '\0';
                    if (ad == ' ' || ad == '\t' || ad == '\n' || ad == '\0') {
                        // sequence follows anchor
                        p->pos = nlp; p->col = 0;
                        Item seq = parse_block_sequence(p, ni);
                        store_anchor(p, key_anchor, seq);
                        // now continue to check if this is a mapping entry
                        // (there might be a key after the sequence)
                        continue;
                    }
                }
            }
            p->pos = saved; p->line = saved_line; p->col = saved_col;
            // the anchor might be a value itself, try to read block node
            Item val = parse_block_node(p, map_indent + 1);
            store_anchor(p, key_anchor, val);
            continue;
        }

        bool explicit_key = false;
        if (peek(p) == '?') {
            char next = peek_at(p, 1);
            if (next == ' ' || next == '\t' || next == '\n' || next == '\0') {
                explicit_key = true;
                advance(p);
                if (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
            }
        }

        Item key_item;

        if (explicit_key) {
            skip_spaces(p);
            // parse tags/anchors after ? (e.g., "? !!str a")
            const char* ek_anchor = parse_node_properties(p);
            if (p->tag == TAG_NONE) p->tag = key_tag;
            key_tag = p->tag;
            skip_spaces(p);
            if (at_end(p) || peek(p) == '\n' || (peek(p) == ':' && is_mapping_indicator(p))) {
                if (!at_end(p) && peek(p) == ':') {
                    // ? : value  (empty key → null)
                    key_item = p->ctx->builder.createNull();
                } else if (!at_end(p) && peek(p) == '\n') {
                    advance(p);
                    p->tag = key_tag;
                    key_item = parse_block_node(p, map_indent + 1);
                } else {
                    key_item = p->ctx->builder.createNull();
                }
            } else if (peek(p) == '"') {
                p->tag = key_tag;
                key_item = parse_double_quoted(p);
            } else if (peek(p) == '\'') {
                p->tag = key_tag;
                key_item = parse_single_quoted(p);
            } else if (peek(p) == '|' || peek(p) == '>') {
                p->tag = key_tag;
                key_item = parse_block_scalar(p, map_indent);
            } else if (peek(p) == '[') {
                key_item = parse_flow_sequence(p);
            } else if (peek(p) == '{') {
                key_item = parse_flow_mapping(p);
            } else {
                p->tag = key_tag;
                key_item = parse_plain_scalar(p, map_indent, false);
            }
        } else {
            p->tag = key_tag;
            if (peek(p) == '"') {
                key_item = parse_double_quoted(p);
            } else if (peek(p) == '\'') {
                key_item = parse_single_quoted(p);
            } else if (peek(p) == '*') {
                // alias as mapping key
                key_item = parse_alias(p);
            } else {
                StrBuf* ksb = strbuf_new_cap(32);
                while (!at_end(p) && peek(p) != '\n') {
                    if (is_mapping_indicator(p)) break;
                    // stop at comment if preceded by space
                    if (peek(p) == '#' && ksb->length > 0) {
                        char prev = ksb->str[ksb->length - 1];
                        if (prev == ' ' || prev == '\t') break;
                    }
                    strbuf_append_char(ksb, peek(p));
                    advance(p);
                }
                int klen = ksb->length;
                while (klen > 0 && (ksb->str[klen-1] == ' ' || ksb->str[klen-1] == '\t')) klen--;
                ksb->length = klen; ksb->str[klen] = '\0';

                if (key_tag == TAG_STR || key_tag == TAG_NON_SPECIFIC) {
                    key_item = p->ctx->builder.createStringItem(ksb->str);
                } else {
                    p->tag = TAG_NONE;
                    key_item = make_scalar(p, ksb->str, false);
                }
                strbuf_free(ksb);
            }
        }

        if (key_anchor) store_anchor(p, key_anchor, key_item);

        skip_spaces(p);

        Item value;
        if (!at_end(p) && peek(p) == ':') {
            char nc = peek_at(p, 1);
            if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\0') {
                advance(p);
                value = parse_inline_block_node(p, map_indent);
            } else {
                value = p->ctx->builder.createNull();
                skip_line(p);
            }
        } else if (explicit_key) {
            // look for ": " on next line at same indent
            if (!at_end(p) && peek(p) == '\n') advance(p);
            skip_blank_lines(p);
            int vi = current_indent(p);
            if (vi == map_indent) {
                int vlp = line_start_pos(p);
                int vpos = vlp + vi;
                if (vpos < p->len && p->src[vpos] == ':') {
                    char vnc = (vpos + 1 < p->len) ? p->src[vpos + 1] : '\0';
                    if (vnc == ' ' || vnc == '\t' || vnc == '\n' || vnc == '\0') {
                        p->pos = vlp; p->col = 0;
                        advance_n(p, vi);
                        advance(p); // skip :
                        value = parse_inline_block_node(p, map_indent);
                    } else {
                        value = p->ctx->builder.createNull();
                    }
                } else {
                    value = p->ctx->builder.createNull();
                }
            } else {
                value = p->ctx->builder.createNull();
            }
        } else {
            value = p->ctx->builder.createNull();
            skip_line(p);
        }

        put_key_value(p, map, key_item, value);

        if (p->pos == loop_guard) break;
    }

    return map.final();
}

// ============================================================================
// Block node parsing - dispatch
// ============================================================================

static Item parse_block_node(YamlParser* p, int min_indent) {
    if (!skip_blank_lines(p)) {
        return p->ctx->builder.createNull();
    }

    int indent = current_indent(p);
    if (indent < min_indent) {
        return p->ctx->builder.createNull();
    }

    // doc boundary
    {
        int lp = line_start_pos(p);
        if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) {
            return p->ctx->builder.createNull();
        }
    }

    int lstart = line_start_pos(p);
    p->pos = lstart;
    p->col = 0;

    // skip to actual indent position
    advance_n(p, indent);

    // skip tabs after indent spaces (tabs are valid whitespace before content)
    while (!at_end(p) && peek(p) == '\t') advance(p);

    const char* anchor_name = parse_node_properties(p);
    int tag = p->tag;

    if (at_end(p) || peek(p) == '\n') {
        // anchor/tag on own line - value on next line(s)
        if (!at_end(p)) advance(p);

        // look ahead to see what follows at same or greater indent
        int saved = p->pos; int saved_line = p->line; int saved_col = p->col;
        skip_blank_lines(p);
        if (!at_end(p)) {
            int ni = current_indent(p);
            int nlp = line_start_pos(p);

            // check for sequence at same or greater indent (or even parent indent for anchors)
            if (ni >= min_indent || (anchor_name && ni >= 0)) {
                char nc = (nlp + ni < p->len) ? p->src[nlp + ni] : '\0';
                if (nc == '-') {
                    char ad = (nlp + ni + 1 < p->len) ? p->src[nlp + ni + 1] : '\0';
                    if (ad == ' ' || ad == '\t' || ad == '\n' || ad == '\0') {
                        p->pos = nlp; p->col = 0;
                        Item seq = parse_block_sequence(p, ni);
                        if (anchor_name) store_anchor(p, anchor_name, seq);
                        return seq;
                    }
                }
                // check for block scalar indicator after tag/anchor on own line
                if (nc == '|' || nc == '>') {
                    p->pos = nlp + ni; p->col = ni;
                    // use min_indent - 1 as base so explicit indent is relative to parent
                    int scalar_base = (min_indent > 0) ? min_indent - 1 : -1;
                    Item val = parse_block_scalar(p, scalar_base);
                    if (anchor_name) store_anchor(p, anchor_name, val);
                    return val;
                }
            }

            // check for mapping or other content at same indent as anchor
            if (ni >= indent) {
                p->pos = saved; p->line = saved_line; p->col = saved_col;
                Item value = parse_block_node(p, indent);
                if (anchor_name) store_anchor(p, anchor_name, value);
                return value;
            }
        }
        p->pos = saved; p->line = saved_line; p->col = saved_col;

        Item value = parse_block_node(p, indent + 1);
        if (anchor_name) store_anchor(p, anchor_name, value);
        return value;
    }

    char first = peek(p);
    Item result;

    if (first == '[') {
        result = parse_flow_sequence(p);
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);
    } else if (first == '{') {
        result = parse_flow_mapping(p);
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);
    } else if (first == '|' || first == '>') {
        result = parse_block_scalar(p, indent);
    } else if (first == '"') {
        result = parse_double_quoted(p);
        skip_spaces(p);
        if (!at_end(p) && is_mapping_indicator(p)) {
            p->pos = lstart; p->col = 0;
            result = parse_block_mapping(p, indent);
        } else {
            skip_spaces_and_comments(p);
            if (!at_end(p) && peek(p) == '\n') advance(p);
        }
    } else if (first == '\'') {
        result = parse_single_quoted(p);
        skip_spaces(p);
        if (!at_end(p) && is_mapping_indicator(p)) {
            p->pos = lstart; p->col = 0;
            result = parse_block_mapping(p, indent);
        } else {
            skip_spaces_and_comments(p);
            if (!at_end(p) && peek(p) == '\n') advance(p);
        }
    } else if (first == '*') {
        result = parse_alias(p);
        skip_spaces(p);
        if (!at_end(p) && is_mapping_indicator(p)) {
            p->pos = lstart; p->col = 0;
            result = parse_block_mapping(p, indent);
        } else {
            skip_spaces_and_comments(p);
            if (!at_end(p) && peek(p) == '\n') advance(p);
        }
    } else if (first == '-' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\t' || peek_at(p, 1) == '\n' || peek_at(p, 1) == '\0')) {
        p->pos = lstart;
        p->col = 0;
        result = parse_block_sequence(p, indent);
    } else if (first == '?' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\t' || peek_at(p, 1) == '\n' || peek_at(p, 1) == '\0')) {
        p->pos = lstart;
        p->col = 0;
        result = parse_block_mapping(p, indent);
    } else {
        if (is_block_mapping_key(p, 0)) {
            p->pos = lstart;
            p->col = 0;
            result = parse_block_mapping(p, indent);
        } else {
            result = parse_plain_scalar(p, indent - 1, false);
            skip_spaces_and_comments(p);
            if (!at_end(p) && peek(p) == '\n') advance(p);
        }
    }

    if (anchor_name) store_anchor(p, anchor_name, result);
    return result;
}

// ============================================================================
// Inline block mapping - for mapping entries starting mid-line
// ============================================================================

static Item parse_block_mapping_inline(YamlParser* p, int map_indent) {
    MapBuilder map = p->ctx->builder.map();
    int max_entries = p->len;
    bool first_entry = true;

    while (!at_end(p)) {
        if (--max_entries < 0) { log_error("yaml: inline block mapping max entries at pos %d", p->pos); break; }
        int loop_guard = p->pos;

        if (!first_entry) {
            if (!skip_blank_lines(p)) break;
            int lp = line_start_pos(p);
            if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) break;
            int indent = current_indent(p);
            if (indent != map_indent) break;
            p->pos = lp; p->col = 0;
            advance_n(p, indent);
        }
        first_entry = false;

        const char* key_anchor = parse_node_properties(p);
        int key_tag = p->tag;

        bool explicit_key = false;
        if (!at_end(p) && peek(p) == '?') {
            char next = peek_at(p, 1);
            if (next == ' ' || next == '\t' || next == '\n' || next == '\0') {
                explicit_key = true;
                advance(p);
                if (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
            }
        }

        Item key_item;

        if (explicit_key) {
            skip_spaces(p);
            // parse tags/anchors after ? (e.g., "? !!str a")
            const char* ek_anchor = parse_node_properties(p);
            if (p->tag == TAG_NONE) p->tag = key_tag;
            key_tag = p->tag;
            skip_spaces(p);
            if (at_end(p) || peek(p) == '\n' || (peek(p) == ':' && is_mapping_indicator(p))) {
                if (!at_end(p) && peek(p) == ':') {
                    key_item = p->ctx->builder.createNull();
                } else if (!at_end(p) && peek(p) == '\n') {
                    advance(p);
                    p->tag = key_tag;
                    key_item = parse_block_node(p, map_indent + 1);
                } else {
                    key_item = p->ctx->builder.createNull();
                }
            } else if (peek(p) == '"') {
                p->tag = key_tag;
                key_item = parse_double_quoted(p);
            } else if (peek(p) == '\'') {
                p->tag = key_tag;
                key_item = parse_single_quoted(p);
            } else if (peek(p) == '[') {
                key_item = parse_flow_sequence(p);
            } else if (peek(p) == '{') {
                key_item = parse_flow_mapping(p);
            } else {
                p->tag = key_tag;
                key_item = parse_plain_scalar(p, map_indent, false);
            }
        } else {
            p->tag = key_tag;
            if (!at_end(p) && peek(p) == '"') {
                key_item = parse_double_quoted(p);
            } else if (!at_end(p) && peek(p) == '\'') {
                key_item = parse_single_quoted(p);
            } else if (!at_end(p) && peek(p) == '*') {
                key_item = parse_alias(p);
            } else {
                StrBuf* ksb = strbuf_new_cap(32);
                while (!at_end(p) && peek(p) != '\n') {
                    if (is_mapping_indicator(p)) break;
                    if (peek(p) == '#' && ksb->length > 0) {
                        char prev = ksb->str[ksb->length - 1];
                        if (prev == ' ' || prev == '\t') break;
                    }
                    strbuf_append_char(ksb, peek(p));
                    advance(p);
                }
                int klen = ksb->length;
                while (klen > 0 && (ksb->str[klen-1] == ' ' || ksb->str[klen-1] == '\t')) klen--;
                ksb->length = klen; ksb->str[klen] = '\0';

                if (key_tag == TAG_STR || key_tag == TAG_NON_SPECIFIC) {
                    key_item = p->ctx->builder.createStringItem(ksb->str);
                } else {
                    p->tag = TAG_NONE;
                    key_item = make_scalar(p, ksb->str, false);
                }
                strbuf_free(ksb);
            }
        }

        if (key_anchor) store_anchor(p, key_anchor, key_item);

        skip_spaces(p);

        Item value;
        if (!at_end(p) && peek(p) == ':') {
            char nc = peek_at(p, 1);
            if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '\0') {
                advance(p);
                value = parse_inline_block_node(p, map_indent);
            } else {
                value = p->ctx->builder.createNull();
                skip_line(p);
            }
        } else if (explicit_key) {
            if (!at_end(p) && peek(p) == '\n') advance(p);
            skip_blank_lines(p);
            int vi = current_indent(p);
            if (vi == map_indent) {
                int vlp = line_start_pos(p);
                int vpos = vlp + vi;
                if (vpos < p->len && p->src[vpos] == ':') {
                    char vnc = (vpos + 1 < p->len) ? p->src[vpos + 1] : '\0';
                    if (vnc == ' ' || vnc == '\t' || vnc == '\n' || vnc == '\0') {
                        p->pos = vlp; p->col = 0;
                        advance_n(p, vi);
                        advance(p);
                        value = parse_inline_block_node(p, map_indent);
                    } else {
                        value = p->ctx->builder.createNull();
                    }
                } else {
                    value = p->ctx->builder.createNull();
                }
            } else {
                value = p->ctx->builder.createNull();
            }
        } else {
            value = p->ctx->builder.createNull();
            skip_line(p);
        }

        put_key_value(p, map, key_item, value);
        if (p->pos == loop_guard) break;
    }

    return map.final();
}

// ============================================================================
// Inline block node parsing (after "- " or ": ")
// ============================================================================

static Item parse_inline_block_node(YamlParser* p, int parent_indent) {
    skip_spaces(p);

    if (at_end(p) || peek(p) == '\n' || peek(p) == '#') {
        int saved_tag = p->tag;
        bool was_comment = (!at_end(p) && peek(p) == '#');
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);

        // if was_comment, skip the same-indent sequence check
        // (those are sibling entries, not nested content - fixes W42U)
        // but still fall through to parse_block_node for deeper-indented content
        if (!was_comment) {
            // check if next content is a sequence at same indent as parent
            int saved_pos2 = p->pos;
            int saved_line2 = p->line;
            int saved_col2 = p->col;
            skip_blank_lines(p);
            if (!at_end(p)) {
                int ni = current_indent(p);
                int lp = line_start_pos(p);
                int check_pos = lp + ni;
                // sequence at same indent as parent (e.g., "key:\n- val" where - at same indent)
                if (ni == parent_indent && check_pos < p->len && p->src[check_pos] == '-') {
                    char after = (check_pos + 1 < p->len) ? p->src[check_pos + 1] : '\0';
                    if (after == ' ' || after == '\t' || after == '\n' || after == '\0') {
                        p->pos = lp; p->col = 0;
                        return parse_block_sequence(p, ni);
                    }
                }
            }
            p->pos = saved_pos2; p->line = saved_line2; p->col = saved_col2;
        }

        if (saved_tag == TAG_STR || saved_tag == TAG_NON_SPECIFIC) {
            p->tag = TAG_NONE;
            Item next = parse_block_node(p, parent_indent + 1);
            TypeId ntid = get_type_id(next);
            if (ntid == LMD_TYPE_NULL) {
                return make_empty_string(p);
            }
            return next;
        }
        return parse_block_node(p, parent_indent + 1);
    }

    const char* anchor_name = parse_node_properties(p);
    int saved_tag = p->tag;

    // skip trailing comment after anchor/tag
    if (!at_end(p) && peek(p) == '#') {
        skip_spaces_and_comments(p);
    }

    if (at_end(p) || peek(p) == '\n') {
        if (!at_end(p)) advance(p);

        // check for sequence at same indent after tag/anchor
        int saved_pos2 = p->pos;
        int saved_line2 = p->line;
        int saved_col2 = p->col;
        skip_blank_lines(p);
        if (!at_end(p)) {
            int ni = current_indent(p);
            int lp = line_start_pos(p);
            int check_pos = lp + ni;
            if (ni == parent_indent && check_pos < p->len && p->src[check_pos] == '-') {
                char after = (check_pos + 1 < p->len) ? p->src[check_pos + 1] : '\0';
                if (after == ' ' || after == '\t' || after == '\n' || after == '\0') {
                    p->pos = lp; p->col = 0;
                    Item seq = parse_block_sequence(p, ni);
                    if (anchor_name) store_anchor(p, anchor_name, seq);
                    return seq;
                }
            }
            // also check for mapping at greater indent
            if (ni > parent_indent) {
                int lp2 = line_start_pos(p);
                if (is_block_mapping_key(p, ni)) {
                    p->pos = lp2; p->col = 0;
                    Item mp = parse_block_mapping(p, ni);
                    if (anchor_name) store_anchor(p, anchor_name, mp);
                    return mp;
                }
            }
        }
        p->pos = saved_pos2; p->line = saved_line2; p->col = saved_col2;

        if (saved_tag == TAG_STR || saved_tag == TAG_NON_SPECIFIC) {
            Item next = parse_block_node(p, parent_indent + 1);
            TypeId ntid = get_type_id(next);
            if (ntid == LMD_TYPE_NULL) {
                p->tag = TAG_NONE;
                Item val = make_empty_string(p);
                if (anchor_name) store_anchor(p, anchor_name, val);
                return val;
            }
            if (anchor_name) store_anchor(p, anchor_name, next);
            return next;
        }
        Item value = parse_block_node(p, parent_indent + 1);
        if (anchor_name) store_anchor(p, anchor_name, value);
        return value;
    }

    char first = peek(p);
    Item result;

    if (first == '[') {
        result = parse_flow_sequence(p);
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);
    } else if (first == '{') {
        result = parse_flow_mapping(p);
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);
    } else if (first == '|' || first == '>') {
        result = parse_block_scalar(p, parent_indent);
    } else if (first == '"') {
        result = parse_double_quoted(p);
        skip_spaces(p);
        if (!at_end(p) && is_mapping_indicator(p)) {
            // quoted key: build inline mapping
            advance(p); // skip :
            Item val = parse_inline_block_node(p, parent_indent);
            MapBuilder mb = p->ctx->builder.map();
            put_key_value(p, mb, result, val);
            // continue reading mapping entries at greater indent
            while (!at_end(p)) {
                if (!skip_blank_lines(p)) break;
                int ni = current_indent(p);
                if (ni <= parent_indent) break;
                int lp = line_start_pos(p);
                if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) break;
                p->pos = lp; p->col = 0;
                advance_n(p, ni);
                if (peek(p) == '-' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\n' || peek_at(p, 1) == '\0')) {
                    p->pos = lp; p->col = 0; break;
                }
                if (!is_block_mapping_key(p, 0) && peek(p) != '?') {
                    p->pos = lp; p->col = 0; break;
                }
                p->pos = lp; p->col = 0;
                advance_n(p, ni);
                // parse single mapping entry inline
                const char* ka = parse_node_properties(p);
                bool expk = false;
                if (peek(p) == '?') {
                    char nn = peek_at(p, 1);
                    if (nn == ' ' || nn == '\t' || nn == '\n' || nn == '\0') {
                        expk = true; advance(p); skip_spaces(p);
                    }
                }
                Item ki;
                if (peek(p) == '"') {
                    ki = parse_double_quoted(p);
                } else if (peek(p) == '\'') {
                    ki = parse_single_quoted(p);
                } else if (peek(p) == '*') {
                    ki = parse_alias(p);
                } else {
                    StrBuf* ksb2 = strbuf_new_cap(32);
                    while (!at_end(p) && peek(p) != '\n') {
                        if (is_mapping_indicator(p)) break;
                        if (peek(p) == '#' && ksb2->length > 0 && (ksb2->str[ksb2->length-1] == ' ' || ksb2->str[ksb2->length-1] == '\t')) break;
                        strbuf_append_char(ksb2, peek(p)); advance(p);
                    }
                    int kl = ksb2->length;
                    while (kl > 0 && (ksb2->str[kl-1] == ' ' || ksb2->str[kl-1] == '\t')) kl--;
                    ksb2->length = kl; ksb2->str[kl] = '\0';
                    ki = make_scalar(p, ksb2->str, false);
                    strbuf_free(ksb2);
                }
                if (ka) store_anchor(p, ka, ki);
                skip_spaces(p);
                Item kv;
                if (!at_end(p) && peek(p) == ':' && is_mapping_indicator(p)) {
                    advance(p);
                    kv = parse_inline_block_node(p, parent_indent);
                } else {
                    kv = p->ctx->builder.createNull();
                    skip_line(p);
                }
                put_key_value(p, mb, ki, kv);
            }
            result = mb.final();
            if (anchor_name) store_anchor(p, anchor_name, result);
            return result;
        }
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);
    } else if (first == '\'') {
        result = parse_single_quoted(p);
        skip_spaces(p);
        if (!at_end(p) && is_mapping_indicator(p)) {
            advance(p);
            Item val = parse_inline_block_node(p, parent_indent);
            MapBuilder mb = p->ctx->builder.map();
            put_key_value(p, mb, result, val);
            while (!at_end(p)) {
                if (!skip_blank_lines(p)) break;
                int ni = current_indent(p);
                if (ni <= parent_indent) break;
                int lp = line_start_pos(p);
                if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) break;
                p->pos = lp; p->col = 0;
                advance_n(p, ni);
                if (!is_block_mapping_key(p, 0) && peek(p) != '?') {
                    p->pos = lp; p->col = 0; break;
                }
                p->pos = lp; p->col = 0;
                advance_n(p, ni);
                const char* ka = parse_node_properties(p);
                Item ki;
                if (peek(p) == '"') {
                    ki = parse_double_quoted(p);
                } else if (peek(p) == '\'') {
                    ki = parse_single_quoted(p);
                } else if (peek(p) == '*') {
                    ki = parse_alias(p);
                } else {
                    StrBuf* ksb2 = strbuf_new_cap(32);
                    while (!at_end(p) && peek(p) != '\n') {
                        if (is_mapping_indicator(p)) break;
                        strbuf_append_char(ksb2, peek(p)); advance(p);
                    }
                    int kl = ksb2->length;
                    while (kl > 0 && (ksb2->str[kl-1] == ' ' || ksb2->str[kl-1] == '\t')) kl--;
                    ksb2->length = kl; ksb2->str[kl] = '\0';
                    ki = make_scalar(p, ksb2->str, false);
                    strbuf_free(ksb2);
                }
                if (ka) store_anchor(p, ka, ki);
                skip_spaces(p);
                Item kv;
                if (!at_end(p) && peek(p) == ':' && is_mapping_indicator(p)) {
                    advance(p);
                    kv = parse_inline_block_node(p, parent_indent);
                } else {
                    kv = p->ctx->builder.createNull();
                    skip_line(p);
                }
                put_key_value(p, mb, ki, kv);
            }
            result = mb.final();
            if (anchor_name) store_anchor(p, anchor_name, result);
            return result;
        }
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);
    } else if (first == '*') {
        result = parse_alias(p);
        skip_spaces(p);
        if (!at_end(p) && is_mapping_indicator(p)) {
            // alias as mapping key
            advance(p);
            Item val = parse_inline_block_node(p, parent_indent);
            MapBuilder mb = p->ctx->builder.map();
            put_key_value(p, mb, result, val);
            // continue with remaining entries
            while (!at_end(p)) {
                if (!skip_blank_lines(p)) break;
                int ni = current_indent(p);
                if (ni <= parent_indent) break;
                int lp = line_start_pos(p);
                if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) break;
                p->pos = lp; p->col = 0;
                advance_n(p, ni);
                if (!is_block_mapping_key(p, 0) && peek(p) != '?' && peek(p) != '*') {
                    p->pos = lp; p->col = 0; break;
                }
                p->pos = lp; p->col = 0;
                advance_n(p, ni);
                const char* ka = parse_node_properties(p);
                Item ki;
                if (peek(p) == '*') {
                    ki = parse_alias(p);
                } else if (peek(p) == '"') {
                    ki = parse_double_quoted(p);
                } else if (peek(p) == '\'') {
                    ki = parse_single_quoted(p);
                } else {
                    StrBuf* ksb2 = strbuf_new_cap(32);
                    while (!at_end(p) && peek(p) != '\n') {
                        if (is_mapping_indicator(p)) break;
                        strbuf_append_char(ksb2, peek(p)); advance(p);
                    }
                    int kl = ksb2->length;
                    while (kl > 0 && (ksb2->str[kl-1] == ' ' || ksb2->str[kl-1] == '\t')) kl--;
                    ksb2->length = kl; ksb2->str[kl] = '\0';
                    ki = make_scalar(p, ksb2->str, false);
                    strbuf_free(ksb2);
                }
                if (ka) store_anchor(p, ka, ki);
                skip_spaces(p);
                Item kv;
                if (!at_end(p) && peek(p) == ':' && is_mapping_indicator(p)) {
                    advance(p);
                    kv = parse_inline_block_node(p, parent_indent);
                } else {
                    kv = p->ctx->builder.createNull();
                    skip_line(p);
                }
                put_key_value(p, mb, ki, kv);
            }
            result = mb.final();
            if (anchor_name) store_anchor(p, anchor_name, result);
            return result;
        }
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);
    } else if (first == '-' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\t' || peek_at(p, 1) == '\n' || peek_at(p, 1) == '\0')) {
        // inline nested sequence: parse first entry, then continue on subsequent lines
        int seq_indent = p->col;
        ArrayBuilder sarr = p->ctx->builder.array();

        // parse first entry inline
        advance(p); // skip -
        if (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
        Item first_item = parse_inline_block_node(p, seq_indent);
        sarr.append(first_item);

        // continue with subsequent lines at same indent
        while (!at_end(p)) {
            int sg = p->pos;
            if (!skip_blank_lines(p)) break;
            int lp = line_start_pos(p);
            if (is_doc_start_at(p, lp) || is_doc_end_at(p, lp)) { p->pos = sg; break; }
            int ni = current_indent(p);
            if (ni != seq_indent) { p->pos = lp; p->col = 0; break; }
            int dp = lp + ni;
            if (dp >= p->len || p->src[dp] != '-') { p->pos = lp; p->col = 0; break; }
            char ad = (dp + 1 < p->len) ? p->src[dp + 1] : '\0';
            if (ad != ' ' && ad != '\t' && ad != '\n' && ad != '\0') { p->pos = lp; p->col = 0; break; }
            p->pos = lp; p->col = 0;
            advance_n(p, ni);
            advance(p); // skip -
            if (!at_end(p) && (peek(p) == ' ' || peek(p) == '\t')) advance(p);
            Item item = parse_inline_block_node(p, seq_indent);
            sarr.append(item);
            if (p->pos == sg) break;
        }
        result = sarr.final();
    } else if (first == '?' && (peek_at(p, 1) == ' ' || peek_at(p, 1) == '\t' || peek_at(p, 1) == '\n' || peek_at(p, 1) == '\0')) {
        // inline explicit key mapping
        int map_indent2 = p->col;
        int ls = line_start_pos(p);
        int line_indent = 0;
        for (int i = ls; i < p->len && p->src[i] == ' '; i++) line_indent++;
        if (line_indent == map_indent2) {
            p->pos = ls;
            p->col = 0;
            result = parse_block_mapping(p, map_indent2);
        } else {
            // inline context: parse mapping entries directly
            result = parse_block_mapping_inline(p, map_indent2);
        }
    } else {
        if (is_block_mapping_key(p, 0)) {
            int map_indent2 = p->col;
            int ls = line_start_pos(p);
            int line_indent = 0;
            for (int i = ls; i < p->len && p->src[i] == ' '; i++) line_indent++;
            if (line_indent == map_indent2) {
                p->pos = ls;
                p->col = 0;
                result = parse_block_mapping(p, map_indent2);
            } else {
                // inline context: parse mapping entries directly
                result = parse_block_mapping_inline(p, map_indent2);
            }
        } else {
            result = parse_plain_scalar(p, parent_indent, false);
            skip_spaces_and_comments(p);
            if (!at_end(p) && peek(p) == '\n') advance(p);
        }
    }

    if (anchor_name) store_anchor(p, anchor_name, result);
    return result;
}

// ============================================================================
// Document parsing
// ============================================================================

static Item parse_document(YamlParser* p, bool* has_content) {
    *has_content = false;

    // skip YAML directives (%YAML, %TAG, and other %WORD directives)
    while (!at_end(p) && peek(p) == '%') {
        // directives start with % followed by a letter (e.g., %YAML, %TAG, %FOO)
        // non-directive % content (e.g., %!PS-Adobe) does not have a letter after %
        if (p->pos + 1 < p->len) {
            char next = p->src[p->pos + 1];
            if ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')) {
                skip_line(p);
                continue;
            }
        }
        break; // not a directive, stop skipping
    }

    skip_blank_lines(p);
    if (at_end(p)) return p->ctx->builder.createNull();

    if (is_doc_end(p)) {
        return p->ctx->builder.createNull();
    }

    bool had_start = false;
    if (is_doc_start(p)) {
        had_start = true;
        advance_n(p, 3);
        skip_spaces(p);

        if (!at_end(p) && peek(p) != '\n' && peek(p) != '#') {
            *has_content = true;
            // content on same line as ---
            const char* anchor = NULL;
            int tag = TAG_NONE;

            // parse node properties
            if (peek(p) == '!' || peek(p) == '&') {
                anchor = parse_node_properties(p);
                tag = p->tag;
                skip_spaces(p);
                if (at_end(p) || peek(p) == '\n' || peek(p) == '#') {
                    skip_spaces_and_comments(p);
                    if (!at_end(p) && peek(p) == '\n') advance(p);
                    Item val = parse_block_node(p, 0);
                    if (anchor) store_anchor(p, anchor, val);
                    return val;
                }
            }

            char c = peek(p);
            Item val;
            if (c == '|' || c == '>') {
                val = parse_block_scalar(p, -1);  // document-level: allow content at indent 0
            } else if (c == '[') {
                val = parse_flow_sequence(p);
                skip_spaces_and_comments(p);
                if (!at_end(p) && peek(p) == '\n') advance(p);
            } else if (c == '{') {
                val = parse_flow_mapping(p);
                skip_spaces_and_comments(p);
                if (!at_end(p) && peek(p) == '\n') advance(p);
            } else if (c == '"') {
                val = parse_double_quoted(p);
                skip_spaces_and_comments(p);
                if (!at_end(p) && peek(p) == '\n') advance(p);
            } else if (c == '\'') {
                val = parse_single_quoted(p);
                skip_spaces_and_comments(p);
                if (!at_end(p) && peek(p) == '\n') advance(p);
            } else {
                val = parse_plain_scalar(p, -1, false);
                skip_spaces_and_comments(p);
                if (!at_end(p) && peek(p) == '\n') advance(p);
            }
            if (anchor) store_anchor(p, anchor, val);
            return val;
        }
        skip_spaces_and_comments(p);
        if (!at_end(p) && peek(p) == '\n') advance(p);

        // check for content before next boundary
        int saved = p->pos;
        int saved_line = p->line;
        int saved_col = p->col;
        skip_blank_lines(p);
        if (at_end(p) || is_doc_start(p) || is_doc_end(p)) {
            *has_content = true;
            p->pos = saved; p->line = saved_line; p->col = saved_col;
            return p->ctx->builder.createNull();
        }
        p->pos = saved; p->line = saved_line; p->col = saved_col;
    }

    // check for actual content
    {
        int saved = p->pos;
        int saved_line = p->line;
        int saved_col = p->col;
        skip_blank_lines(p);
        if (at_end(p)) {
            p->pos = saved; p->line = saved_line; p->col = saved_col;
            return p->ctx->builder.createNull();
        }
        if (is_doc_start(p) || is_doc_end(p)) {
            p->pos = saved; p->line = saved_line; p->col = saved_col;
            return p->ctx->builder.createNull();
        }
        p->pos = saved; p->line = saved_line; p->col = saved_col;
    }

    *has_content = true;

    // check for document-level block scalar (| or >) without ---
    {
        int saved = p->pos;
        int saved_line = p->line;
        int saved_col = p->col;
        skip_blank_lines(p);
        if (!at_end(p)) {
            int ci = current_indent(p);
            int lp = line_start_pos(p);
            char fc = (lp + ci < p->len) ? p->src[lp + ci] : '\0';
            if ((fc == '|' || fc == '>') && ci == 0) {
                // position at the block scalar indicator
                p->pos = lp; p->col = 0;
                return parse_block_scalar(p, -1);
            }
        }
        p->pos = saved; p->line = saved_line; p->col = saved_col;
    }

    return parse_block_node(p, 0);
}

// ============================================================================
// Main entry point
// ============================================================================

void parse_yaml(Input *input, const char* yaml_str) {
    if (!yaml_str || !*yaml_str) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    InputContext ctx(input, yaml_str);

    YamlParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.ctx = &ctx;
    parser.src = yaml_str;
    parser.pos = 0;
    parser.len = strlen(yaml_str);
    parser.line = 1;
    parser.col = 0;
    parser.anchor_count = 0;
    parser.tag = TAG_NONE;

    YamlParser* p = &parser;

    // skip BOM
    if (p->len >= 3 && (unsigned char)p->src[0] == 0xEF &&
        (unsigned char)p->src[1] == 0xBB && (unsigned char)p->src[2] == 0xBF) {
        advance_n(p, 3);
    }

    // check for empty input
    {
        int saved = p->pos;
        int saved_line = p->line;
        int saved_col = p->col;
        skip_blank_lines(p);
        if (at_end(p)) {
            input->root = ctx.builder.createNull();
            ctx.logErrors();
            return;
        }
        // check for only comments/whitespace
        bool only_comments = true;
        p->pos = saved; p->line = saved_line; p->col = saved_col;
        while (!at_end(p)) {
            skip_spaces(p);
            if (at_end(p)) break;
            if (peek(p) == '#') { skip_comment(p); if (!at_end(p) && peek(p) == '\n') advance(p); continue; }
            if (peek(p) == '\n') { advance(p); continue; }
            // check for doc end marker
            if (is_doc_end(p)) {
                advance_n(p, 3);
                skip_spaces_and_comments(p);
                if (!at_end(p) && peek(p) == '\n') advance(p);
                continue;
            }
            only_comments = false;
            break;
        }
        if (only_comments) {
            input->root = ctx.builder.createNull();
            ctx.logErrors();
            return;
        }
        p->pos = saved; p->line = saved_line; p->col = saved_col;
    }

    ArrayBuilder docs = ctx.builder.array();
    int doc_count = 0;
    Item first_doc = ctx.builder.createNull();

    int max_iterations = p->len * 4;
    int iter_count = 0;
    while (!at_end(p)) {
        if (++iter_count > max_iterations) {
            log_error("yaml: main loop exceeded max iterations at pos %d/%d", p->pos, p->len);
            break;
        }
        int loop_guard = p->pos;

        skip_blank_lines(p);
        if (at_end(p)) break;

        if (is_doc_end(p)) {
            advance_n(p, 3);
            skip_spaces_and_comments(p);
            if (!at_end(p) && peek(p) == '\n') advance(p);
            continue;
        }

        bool has_content = false;
        Item doc = parse_document(p, &has_content);

        skip_blank_lines(p);
        if (!at_end(p) && is_doc_end(p)) {
            advance_n(p, 3);
            skip_spaces_and_comments(p);
            if (!at_end(p) && peek(p) == '\n') advance(p);
        }

        if (has_content) {
            if (doc_count == 0) {
                first_doc = doc;
            } else if (doc_count == 1) {
                docs.append(first_doc);
                docs.append(doc);
            } else {
                docs.append(doc);
            }
            doc_count++;
        }

        if (p->pos == loop_guard) {
            if (!at_end(p)) advance(p);
        }
    }

    if (doc_count == 0) {
        input->root = ctx.builder.createNull();
    } else if (doc_count == 1) {
        input->root = first_doc;
    } else {
        input->root = docs.final();
    }
    input->doc_count = doc_count;

    ctx.logErrors();
}
