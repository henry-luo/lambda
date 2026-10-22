#include "input.hpp"
#include "input-parsers.h"
#include "../io/mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "../../lib/base64.h"
#include "../../lib/mail_mime.h"
#include "../../lib/memtrack.h"
#include "../../lib/strbuf.h"

extern "C" {
#include "../../lib/str.h"
}

// line-oriented helpers and folded RFC header parsing
#include "input-rfc-text.h"

using namespace lambda;

// EML uses is_continuation_line (alias for is_folded_line)
static inline bool is_continuation_line(const char* p) { return is_folded_line(p); }

enum EmlTransferEncoding {
    EML_TRANSFER_IDENTITY,
    EML_TRANSFER_BASE64,
    EML_TRANSFER_QUOTED_PRINTABLE,
    EML_TRANSFER_UNSUPPORTED,
};

struct EmlMimeInfo {
    bool is_html;
    bool is_multipart;
    bool is_attachment;
    String* boundary;
    EmlTransferEncoding transfer_encoding;
    // the part's descriptors for the MIME tree (MG5)
    String* type;               // lower case; text/plain when absent (RFC 2045 §5.2)
    String* charset;
    String* disposition;        // "inline" or "attachment"
    String* filename;           // Content-Disposition filename, else Content-Type name
    String* content_id;         // without its angle brackets
    String* format;             // "flowed" for RFC 3676 text
    bool delsp;
};

struct EmlBoundaryLine {
    const char* start;
    const char* after;
    bool closing;
};

static EmlMimeInfo eml_default_mime_info() {
    EmlMimeInfo info = {};
    info.transfer_encoding = EML_TRANSFER_IDENTITY;
    return info;
}

static const char* eml_next_line(const char* line_end, const char* end) {
    if (line_end < end && *line_end == '\r') line_end++;
    if (line_end < end && *line_end == '\n') line_end++;
    return line_end;
}

// The scratch buffer's text in the Input arena, or NULL when empty.
static String* eml_arena_string(InputContext& ctx, StrBuf* buf) {
    return buf->length ? ctx.builder.createString(buf->str, buf->length) : NULL;
}

static String* eml_param(InputContext& ctx, StrBuf* buf, const char* value, size_t value_len,
                         const char* name, bool decode_words) {
    strbuf_reset(buf);
    return mime_header_param(value, value_len, name, decode_words, buf) ? eml_arena_string(ctx, buf) : NULL;
}

static void eml_parse_content_type(InputContext& ctx, const char* value,
                                   size_t value_len, EmlMimeInfo* info) {
    StrBuf* buf = strbuf_new();
    if (!buf) return;
    mime_header_token(value, value_len, buf);
    info->type = eml_arena_string(ctx, buf);
    info->is_html = info->type && strcmp(info->type->chars, "text/html") == 0;
    info->is_multipart = info->type && strncmp(info->type->chars, "multipart/", 10) == 0;
    info->boundary = eml_param(ctx, buf, value, value_len, "boundary", false);
    info->charset = eml_param(ctx, buf, value, value_len, "charset", false);
    info->format = eml_param(ctx, buf, value, value_len, "format", false);
    String* delsp = eml_param(ctx, buf, value, value_len, "delsp", false);
    info->delsp = delsp && str_ieq_const(delsp->chars, delsp->len, "yes");
    // a Content-Disposition filename wins over the older Content-Type name
    if (!info->filename) info->filename = eml_param(ctx, buf, value, value_len, "name", true);
    strbuf_free(buf);
}

static EmlTransferEncoding eml_parse_transfer_encoding(const char* value,
                                                        size_t value_len) {
    const char* start = value;
    const char* end = value + value_len;
    start = strn_skip_line_space(start, end);
    const char* token_end = start;
    while (token_end < end && *token_end != ';' &&
           !str_char_is_line_space(*token_end)) {
        token_end++;
    }

    size_t len = (size_t)(token_end - start);
    if (str_ieq_const(start, len, "base64")) return EML_TRANSFER_BASE64;
    if (str_ieq_const(start, len, "quoted-printable")) {
        return EML_TRANSFER_QUOTED_PRINTABLE;
    }
    if (len == 0 || str_ieq_const(start, len, "7bit") ||
            str_ieq_const(start, len, "8bit") ||
            str_ieq_const(start, len, "binary")) {
        return EML_TRANSFER_IDENTITY;
    }
    return EML_TRANSFER_UNSUPPORTED;
}

static void eml_parse_disposition(InputContext& ctx, const char* value,
                                  size_t value_len, EmlMimeInfo* info) {
    StrBuf* buf = strbuf_new();
    if (!buf) return;
    mime_header_token(value, value_len, buf);
    info->disposition = eml_arena_string(ctx, buf);
    info->is_attachment = info->disposition && strcmp(info->disposition->chars, "attachment") == 0;
    String* filename = eml_param(ctx, buf, value, value_len, "filename", true);
    if (filename) info->filename = filename;
    strbuf_free(buf);
}

// Content-ID: <part@host> names the part a cid: URL refers to (RFC 2392).
static void eml_parse_content_id(InputContext& ctx, const char* value,
                                 size_t value_len, EmlMimeInfo* info) {
    const char* start = strn_skip_line_space(value, value + value_len);
    const char* end = value + value_len;
    while (end > start && str_char_is_line_space(end[-1])) end--;
    if (start < end && *start == '<') start++;
    if (end > start && end[-1] == '>') end--;
    if (end > start) info->content_id = ctx.builder.createString(start, (size_t)(end - start));
}

static bool eml_is_mime_header(const char* name, size_t name_len) {
    return str_ieq_const(name, name_len, "content-type") ||
           str_ieq_const(name, name_len, "content-transfer-encoding") ||
           str_ieq_const(name, name_len, "content-disposition") ||
           str_ieq_const(name, name_len, "content-id");
}

// One MIME header of the message or of a part; the value must outlive the call.
static void eml_apply_mime_value(InputContext& ctx, const char* name, size_t name_len,
                                 const char* value, size_t value_len, EmlMimeInfo* info) {
    if (str_ieq_const(name, name_len, "content-type")) {
        eml_parse_content_type(ctx, value, value_len, info);
    }
    else if (str_ieq_const(name, name_len, "content-transfer-encoding")) {
        info->transfer_encoding = eml_parse_transfer_encoding(value, value_len);
    }
    else if (str_ieq_const(name, name_len, "content-disposition")) {
        eml_parse_disposition(ctx, value, value_len, info);
    }
    else if (str_ieq_const(name, name_len, "content-id")) {
        eml_parse_content_id(ctx, value, value_len, info);
    }
}

static void eml_apply_mime_header(InputContext& ctx, const char* name,
                                  size_t name_len, StringBuf* value,
                                  EmlMimeInfo* info) {
    if (!name || !value || !value->str) return;
    if (!eml_is_mime_header(name, name_len)) return;

    // The shared scratch buffer is reused while parsing parameters, so preserve
    // the unfolded header value in the Input arena first.
    String* stable_value = ctx.builder.createString(value->str->chars, value->length);
    if (!stable_value) return;
    eml_apply_mime_value(ctx, name, name_len, stable_value->chars, stable_value->len, info);
}

static const char* eml_parse_mime_headers(InputContext& ctx, const char* start,
                                          const char* end, EmlMimeInfo* info) {
    const char* pos = start;
    const char* current_name = NULL;
    size_t current_name_len = 0;
    stringbuf_reset(ctx.sb);

    while (pos < end) {
        const char* line_start = pos;
        const char* line_end = strn_scan_to_line_end(pos, end);
        const char* next = eml_next_line(line_end, end);

        if (line_start == line_end) {
            eml_apply_mime_header(ctx, current_name, current_name_len, ctx.sb, info);
            return next;
        }

        if (str_char_is_line_space(*line_start) && current_name) {
            const char* continuation = strn_skip_line_space(line_start, line_end);
            if (ctx.sb->length > 0) stringbuf_append_char(ctx.sb, ' ');
            stringbuf_append_str_n(ctx.sb, continuation,
                                   (size_t)(line_end - continuation));
            pos = next;
            continue;
        }

        eml_apply_mime_header(ctx, current_name, current_name_len, ctx.sb, info);
        current_name = NULL;
        current_name_len = 0;
        stringbuf_reset(ctx.sb);

        const char* colon = line_start;
        while (colon < line_end && *colon != ':') colon++;
        if (colon < line_end) {
            const char* name_end = colon;
            while (name_end > line_start && str_char_is_line_space(name_end[-1])) name_end--;
            current_name = line_start;
            current_name_len = (size_t)(name_end - line_start);
            const char* value_start = strn_skip_line_space(colon + 1, line_end);
            stringbuf_append_str_n(ctx.sb, value_start,
                                   (size_t)(line_end - value_start));
        }
        pos = next;
    }

    eml_apply_mime_header(ctx, current_name, current_name_len, ctx.sb, info);
    return end;
}

static char* eml_decode_quoted_printable(const char* data, size_t len,
                                         size_t* decoded_len) {
    char* decoded = (char*)mem_alloc(len + 1, MEM_CAT_INPUT_OTHER);
    if (!decoded) return NULL;

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] != '=') {
            decoded[out++] = data[i];
            continue;
        }

        if (i + 1 < len && data[i + 1] == '\n') {
            i++;
            continue;
        }
        if (i + 2 < len && data[i + 1] == '\r' && data[i + 2] == '\n') {
            i += 2;
            continue;
        }
        if (i + 1 < len && data[i + 1] == '\r') {
            i++;
            continue;
        }
        if (i + 2 < len) {
            int high = str_hex_val(data[i + 1]);
            int low = str_hex_val(data[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded[out++] = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        // Invalid quoted-printable escapes are preserved for tolerant parsing.
        decoded[out++] = '=';
    }

    decoded[out] = '\0';
    *decoded_len = out;
    return decoded;
}

static char* eml_decode_transfer(InputContext& ctx, const char* data, size_t len,
                                 EmlTransferEncoding encoding, size_t* decoded_len) {
    if (len == 0) {
        *decoded_len = 0;
        return mem_dup_n("", 0, MEM_CAT_INPUT_OTHER);
    }

    if (encoding == EML_TRANSFER_BASE64) {
        char* decoded = (char*)base64_decode(data, len, decoded_len);
        if (!decoded) ctx.addWarning("Failed to decode a base64 email part");
        return decoded;
    }
    if (encoding == EML_TRANSFER_QUOTED_PRINTABLE) {
        char* decoded = eml_decode_quoted_printable(data, len, decoded_len);
        if (!decoded) ctx.addWarning("Failed to decode a quoted-printable email part");
        return decoded;
    }
    if (encoding == EML_TRANSFER_UNSUPPORTED) {
        ctx.addWarning("Unsupported email part Content-Transfer-Encoding");
        *decoded_len = 0;
        return NULL;
    }

    *decoded_len = len;
    return mem_dup_n(data, len, MEM_CAT_INPUT_OTHER);
}

static bool eml_find_boundary_line(const char* start, const char* end,
                                   String* boundary, EmlBoundaryLine* found) {
    if (!boundary || boundary->len == 0) return false;

    const char* pos = start;
    while (pos < end) {
        const char* line_start = pos;
        const char* line_end = strn_scan_to_line_end(pos, end);
        const char* next = eml_next_line(line_end, end);
        size_t marker_len = (size_t)boundary->len + 2;

        if ((size_t)(line_end - line_start) >= marker_len &&
                line_start[0] == '-' && line_start[1] == '-' &&
                memcmp(line_start + 2, boundary->chars, boundary->len) == 0) {
            const char* suffix = line_start + marker_len;
            bool closing = false;
            if (suffix + 2 <= line_end && suffix[0] == '-' && suffix[1] == '-') {
                closing = true;
                suffix += 2;
            }
            suffix = strn_skip_line_space(suffix, line_end);
            if (suffix == line_end) {
                found->start = line_start;
                found->after = next;
                found->closing = closing;
                return true;
            }
        }
        pos = next;
    }
    return false;
}

static const char* eml_part_end_before_boundary(const char* part_start,
                                                 const char* boundary_start) {
    const char* part_end = boundary_start;
    if (part_end > part_start && part_end[-1] == '\n') {
        part_end--;
        if (part_end > part_start && part_end[-1] == '\r') part_end--;
    }
    else if (part_end > part_start && part_end[-1] == '\r') {
        part_end--;
    }
    return part_end;
}

static Element* eml_parse_html_entity(InputContext& ctx, const char* body,
                                      size_t body_len, const EmlMimeInfo& info,
                                      int depth);

static Element* eml_parse_html_multipart(InputContext& ctx, const char* body,
                                         size_t body_len, const EmlMimeInfo& info,
                                         int depth) {
    const char* multipart_body = body;
    size_t multipart_len = body_len;
    char* decoded_multipart = NULL;

    if (info.transfer_encoding != EML_TRANSFER_IDENTITY) {
        decoded_multipart = eml_decode_transfer(ctx, body, body_len,
                                                info.transfer_encoding,
                                                &multipart_len);
        if (!decoded_multipart) return NULL;
        multipart_body = decoded_multipart;
    }

    const char* end = multipart_body + multipart_len;
    EmlBoundaryLine boundary_line = {};
    if (!eml_find_boundary_line(multipart_body, end, info.boundary, &boundary_line) ||
            boundary_line.closing) {
        if (decoded_multipart) mem_free(decoded_multipart);
        return NULL;
    }

    Element* html = NULL;
    const char* part_start = boundary_line.after;
    while (part_start < end &&
           eml_find_boundary_line(part_start, end, info.boundary, &boundary_line)) {
        const char* part_end =
            eml_part_end_before_boundary(part_start, boundary_line.start);
        EmlMimeInfo part_info = eml_default_mime_info();
        const char* part_body =
            eml_parse_mime_headers(ctx, part_start, part_end, &part_info);
        if (part_body <= part_end) {
            html = eml_parse_html_entity(ctx, part_body,
                                         (size_t)(part_end - part_body),
                                         part_info, depth + 1);
        }
        if (html || boundary_line.closing) break;
        part_start = boundary_line.after;
    }

    if (decoded_multipart) mem_free(decoded_multipart);
    return html;
}

static Element* eml_parse_html_entity(InputContext& ctx, const char* body,
                                      size_t body_len, const EmlMimeInfo& info,
                                      int depth) {
    // Bound recursive multipart nesting independently of message size.
    static const int EML_MAX_MIME_DEPTH = 32;
    if (depth > EML_MAX_MIME_DEPTH || info.is_attachment) return NULL;

    if (info.is_multipart) {
        if (!info.boundary) return NULL;
        return eml_parse_html_multipart(ctx, body, body_len, info, depth);
    }
    if (!info.is_html) return NULL;

    size_t decoded_len = 0;
    char* decoded = eml_decode_transfer(ctx, body, body_len,
                                        info.transfer_encoding, &decoded_len);
    if (!decoded) return NULL;

    Element* html = html5_parse(ctx.input(), decoded);
    mem_free(decoded);
    return html;
}

// ── the MIME part tree (MG5) ────────────────────────────────────────────
// A multipart is {type, parts}; a leaf is {type, charset, disposition,
// filename, cid, size, content}, with text decoded to UTF-8 and other
// content as binary. `format` and `delsp` appear on format=flowed text.

static const int EML_MAX_PARTS = 1000;

static Item eml_text_item(String* value) {
    return value ? (Item){.item = s2it(value)} : ItemNull;
}

static Item eml_build_part(InputContext& ctx, const char* body, size_t body_len,
                           const EmlMimeInfo& info, int depth, int* budget);

static Item eml_build_multipart(InputContext& ctx, const char* body, size_t body_len,
                                const EmlMimeInfo& info, int depth, int* budget) {
    ArrayBuilder parts = ctx.builder.array();
    const char* end = body + body_len;
    EmlBoundaryLine line = {};
    if (eml_find_boundary_line(body, end, info.boundary, &line) && !line.closing) {
        const char* part_start = line.after;
        while (part_start < end && *budget > 0) {
            // a message cut before its closing boundary keeps its last part
            bool found = eml_find_boundary_line(part_start, end, info.boundary, &line);
            if (!found) line = {end, end, true};
            const char* part_end = found ? eml_part_end_before_boundary(part_start, line.start) : end;
            EmlMimeInfo part_info = eml_default_mime_info();
            const char* part_body = eml_parse_mime_headers(ctx, part_start, part_end, &part_info);
            if (part_body <= part_end) {
                parts.append(eml_build_part(ctx, part_body, (size_t)(part_end - part_body),
                                            part_info, depth + 1, budget));
            }
            if (line.closing) break;
            part_start = line.after;
        }
    }
    return ctx.builder.map()
        .put("type", eml_text_item(info.type))
        .put("parts", parts.final())
        .final();
}

static Item eml_build_leaf(InputContext& ctx, const char* body, size_t body_len,
                           const EmlMimeInfo& info) {
    const char* type = info.type ? info.type->chars : "text/plain";
    size_t decoded_len = 0;
    char* decoded = eml_decode_transfer(ctx, body, body_len, info.transfer_encoding, &decoded_len);
    Item content = ItemNull;
    if (decoded && strncmp(type, "text/", 5) == 0) {
        StrBuf* text = strbuf_new_cap(decoded_len + 16);
        if (text) {
            mime_to_utf8(info.charset ? info.charset->chars : NULL, info.charset ? info.charset->len : 0,
                         decoded, decoded_len, text);
            content = ctx.builder.createStringItem(text->str, text->length);
            strbuf_free(text);
        }
    } else if (decoded && decoded_len > 0) {
        content = {.item = x2it(ctx.builder.createBinary(decoded, decoded_len))};
    }
    if (decoded) mem_free(decoded);
    MapBuilder part = ctx.builder.map();
    part.put("type", ctx.builder.createStringItem(type))
        .put("charset", eml_text_item(info.charset))
        .put("disposition", eml_text_item(info.disposition))
        .put("filename", eml_text_item(info.filename))
        .put("cid", eml_text_item(info.content_id))
        .put("size", (int64_t)decoded_len)
        .put("content", content);
    if (info.format) part.put("format", eml_text_item(info.format)).put("delsp", info.delsp);
    return part.final();
}

static Item eml_build_part(InputContext& ctx, const char* body, size_t body_len,
                           const EmlMimeInfo& info, int depth, int* budget) {
    // bound nesting and fan-out independently of message size (§5.8)
    (*budget)--;
    if (info.is_multipart && info.boundary && depth < 32) {
        return eml_build_multipart(ctx, body, body_len, info, depth, budget);
    }
    return eml_build_leaf(ctx, body, body_len, info);
}

// Address-list headers become [{name, address}], the mailbox shape
// doc.misc.eml() writes; names decoded (RFC 2047).
struct EmlAddressSink {
    InputContext* ctx;
    ArrayBuilder* list;
};

static void eml_address_visit(void* user, const char* name, size_t name_len,
                              const char* address, size_t address_len) {
    EmlAddressSink* sink = (EmlAddressSink*)user;
    MarkBuilder& builder = sink->ctx->builder;
    Item name_item = name_len ? builder.createStringItem(name, name_len) : ItemNull;
    sink->list->append(builder.map()
        .put("name", name_item)
        .put("address", builder.createStringItem(address, address_len))
        .final());
}

static const char* const eml_address_headers[] = {"from", "sender", "reply-to", "to", "cc", "bcc"};
static const char* const eml_address_keys[] = {"from", "sender", "reply_to", "to", "cc", "bcc"};
static const int EML_ADDRESS_HEADERS = 6;

// Helper function to parse header name
static String* parse_header_name(InputContext& ctx, const char **eml) {
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (**eml && **eml != ':' && **eml != '\n' && **eml != '\r') {
        stringbuf_append_char(sb, **eml);
        (*eml)++;
    }

    if (sb->str && sb->str->len > 0) {
        return ctx.builder.createString(sb->str->chars, sb->length);
    }
    return NULL;
}

// Helper function to parse email addresses from a header value
static String* extract_email_address(InputContext& ctx, const char* header_value) {
    if (!header_value) return NULL;

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    const char* start = strchr(header_value, '<');
    const char* end = NULL;

    if (start) {
        // Format: "Name <email@domain.com>"
        start++;
        end = strchr(start, '>');
        if (end) {
            stringbuf_append_str_n(sb, start, (size_t)(end - start));
        }
    } else {
        // Format: "email@domain.com" or "Name email@domain.com"
        const char* at_pos = strchr(header_value, '@');
        if (at_pos) {
            // Find start of email (look backwards for space or start of string)
            start = at_pos;
            while (start > header_value && *(start-1) != ' ' && *(start-1) != '\t') {
                start--;
            }

            // Find end of email (look forwards for space or end of string)
            end = at_pos;
            while (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') {
                end++;
            }

            stringbuf_append_str_n(sb, start, (size_t)(end - start));
        }
    }

    if (sb->str && sb->str->len > 0) {
        return ctx.builder.createString(sb->str->chars, sb->length);
    }

    return NULL;
}

// Helper function to parse date value
static String* parse_date_value(InputContext& ctx, const char* date_str) {
    if (!date_str) return NULL;

    // For now, just return the raw date string
    // TODO: Could parse into structured datetime format
    return ctx.builder.createString(date_str);
}

// Main EML parsing function
void parse_eml(Input* input, const char* eml_string) {
    if (!eml_string || !input || !*eml_string) return;

    // create error tracking context with source
    InputContext ctx(input, eml_string, strlen(eml_string));

    const char* eml = eml_string;
    EmlMimeInfo mime_info = eml_default_mime_info();

    // Create root map for the email
    Map* email_map = map_pooled(input->pool);
    if (!email_map) {
        ctx.addError("Failed to allocate memory for email map");
        return;
    }

    Item address_lists[EML_ADDRESS_HEADERS];
    for (int a = 0; a < EML_ADDRESS_HEADERS; a++) address_lists[a] = ItemNull;

    // Initialize headers map
    Map* headers_map = map_pooled(input->pool);
    List* header_entries = rfc_new_entry_list(input);
    if (!headers_map || !header_entries) {
        ctx.addError("Failed to allocate memory for headers map");
        return;
    }

    // Parse headers
    while (*eml) {
        // Header-value parsing consumes its own line ending, leaving this
        // cursor at the separator's empty line.
        if (*eml == '\r' || *eml == '\n') {
            if (*eml == '\r') eml++;
            if (*eml == '\n') eml++;
            break;
        }

        // Skip lines that start with whitespace (continuation lines are handled in parse_header_value)
        if (is_continuation_line(eml)) {
            skip_to_newline(&eml);
            continue;
        }

        // Parse header name
        String* header_name = parse_header_name(ctx, &eml);
        if (!header_name) {
            skip_to_newline(&eml);
            continue;
        }
        if (*eml != ':') {
            skip_to_newline(&eml);
            continue;
        }

        // Parse header value
        StringBuf* header_sb = ctx.sb;
        size_t header_value_len = parse_rfc_header_value(header_sb, &eml);
        String* header_value = ctx.builder.createString(
            header_value_len > 0 ? header_sb->str->chars : "", header_value_len);
        if (!header_value) {
            ctx.addError("Failed to create EML header value");
            break;
        }

        // Normalize header name to lowercase for consistency
        str_lower_inplace(header_name->chars, header_name->len);

        // Store header in headers map
        Item value = {.item = s2it(header_value)};
        ctx.builder.putToMap(lam::gc_borrow(headers_map), header_name, value);
        rfc_append_entry(ctx, header_entries, header_name, value);

        eml_apply_mime_value(ctx, header_name->chars, header_name->len,
                             header_value->chars, header_value->len, &mime_info);
        for (int a = 0; a < EML_ADDRESS_HEADERS; a++) {
            if (strcmp(header_name->chars, eml_address_headers[a]) != 0) continue;
            ArrayBuilder list = ctx.builder.array();
            EmlAddressSink sink = {&ctx, &list};
            mime_parse_addresses(header_value->chars, header_value->len, eml_address_visit, &sink);
            address_lists[a] = list.final();
        }

        // Also store common headers as top-level fields for easier access
        if (strcmp(header_name->chars, "from") == 0) {
            String* from_email = extract_email_address(ctx, header_value->chars);
            if (from_email) {
                String* from_key = ctx.builder.createName("from");
                Item from_value = {.item = s2it(from_email)};
                ctx.builder.putToMap(lam::gc_borrow(email_map), from_key, from_value);
            }
        }
        else if (strcmp(header_name->chars, "to") == 0) {
            String* to_email = extract_email_address(ctx, header_value->chars);
            if (to_email) {
                String* to_key = ctx.builder.createName("to");
                Item to_value = {.item = s2it(to_email)};
                ctx.builder.putToMap(lam::gc_borrow(email_map), to_key, to_value);
            }
        }
        else if (strcmp(header_name->chars, "subject") == 0) {
            // RFC 2047 words decoded; the raw value stays in `headers`
            String* subject_key = ctx.builder.createName("subject");
            StrBuf* decoded = strbuf_new();
            if (decoded) mime_decode_header(header_value->chars, header_value->len, decoded);
            Item subject_value = decoded && decoded->str
                ? ctx.builder.createStringItem(decoded->str, decoded->length)
                : (Item){.item = s2it(header_value)};
            if (decoded) strbuf_free(decoded);
            ctx.builder.putToMap(lam::gc_borrow(email_map), subject_key, subject_value);
        }
        else if (strcmp(header_name->chars, "date") == 0) {
            String* date_parsed = parse_date_value(ctx, header_value->chars);
            if (date_parsed) {
                String* date_key = ctx.builder.createName("date");
                Item date_value = {.item = s2it(date_parsed)};
                ctx.builder.putToMap(lam::gc_borrow(email_map), date_key, date_value);
            }
        }
        else if (strcmp(header_name->chars, "message-id") == 0) {
            String* msgid_key = ctx.builder.createName("message_id");
            Item msgid_value = {.item = s2it(header_value)};
            ctx.builder.putToMap(lam::gc_borrow(email_map), msgid_key, msgid_value);
        }
    }

    // Store headers map in email
    String* headers_key = ctx.builder.createString("headers");
    Item headers_value = {.item = (uint64_t)headers_map};
    ctx.builder.putToMap(lam::gc_borrow(email_map), headers_key, headers_value);
    ctx.builder.putToMap(lam::gc_borrow(email_map), ctx.builder.createName("header_entries"),
        {.item = (uint64_t)header_entries});
    MapBuilder addresses = ctx.builder.map();
    for (int a = 0; a < EML_ADDRESS_HEADERS; a++) {
        if (get_type_id(address_lists[a]) != LMD_TYPE_NULL) addresses.put(eml_address_keys[a], address_lists[a]);
    }
    ctx.builder.putToMap(lam::gc_borrow(email_map), ctx.builder.createName("addresses"), addresses.final());

    // MIME HTML parts become parsed Mark documents; other messages retain the
    // raw body string for compatibility with the existing EML representation.
    size_t body_len = strlen(eml);
    // Keep MIME wire content as well as the display body so attachments survive export.
    ctx.builder.putToMap(lam::gc_borrow(email_map), ctx.builder.createName("raw_body"),
        ctx.builder.createStringItem(eml, body_len));
    int part_budget = EML_MAX_PARTS;
    ctx.builder.putToMap(lam::gc_borrow(email_map), ctx.builder.createName("mime"),
        eml_build_part(ctx, eml, body_len, mime_info, 0, &part_budget));
    Element* html_body =
        eml_parse_html_entity(ctx, eml, body_len, mime_info, 0);
    if (html_body) {
        String* body_key = ctx.builder.createString("body");
        Item body_value = {.element = html_body};
        ctx.builder.putToMap(lam::gc_borrow(email_map), body_key, body_value);
    }
    else if (body_len > 0) {
        String* body_string = ctx.builder.createString(eml, body_len);
        if (!body_string) {
            ctx.addWarning("Failed to create body string");
        }
        else {
            String* body_key = ctx.builder.createString("body");
            Item body_value = {.item = s2it(body_string)};
            ctx.builder.putToMap(lam::gc_borrow(email_map), body_key, body_value);
        }
    }

    // Set the email map as the root of the input
    input->root = {.item = (uint64_t)email_map};

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
