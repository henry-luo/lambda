// mail_imap.c — IMAP response framing, parsing and command encoding
// (RFC 9051, RFC 3501; vibe/Lambda_Demo_Mail.md ML2, Appendix B).

#include "mail_imap.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "memtrack.h"

// Hard limits: a hostile or broken server must not exhaust memory (§5.8).
#define IMAP_MAX_LINE ((size_t)64 * 1024 * 1024)
#define IMAP_MAX_TEXT ((size_t)256 * 1024 * 1024)
#define IMAP_MAX_MEMORY_LITERAL ((size_t)256 * 1024 * 1024)
#define IMAP_MAX_SPILL_LITERAL ((uint64_t)4 * 1024 * 1024 * 1024)
#define IMAP_MAX_LITERALS (1 << 20)
#define IMAP_MAX_DEPTH 64
#define IMAP_QUOTED_LIMIT 1000

// ── framing ────────────────────────────────────────────────────────────

struct ImapReader {
    StrBuf* in;                 // received bytes; [in_pos, length) unread
    size_t in_pos;
    StrBuf* text;               // the response being assembled
    StrBuf* bytes;              // its in-memory literal bytes
    ImapLiteral* literals;
    char** paths;               // spill paths, parallel to literals
    int literal_count;
    int literal_capacity;
    bool literals_allowed;      // false for status and continuation responses
    bool in_literal;
    uint64_t literal_remaining;
    FILE* spill_file;
    char* spill_dir;
    size_t spill_threshold;
    unsigned spill_sequence;
    bool returned;              // the last call returned a response
    bool failed;
    char error[200];
};

static int reader_fail(ImapReader* reader, const char* message) {
    reader->failed = true;
    snprintf(reader->error, sizeof(reader->error), "%s", message);
    log_error("mail imap: framing failed: %s", message);
    return -1;
}

// A partial spill file of a failed response is removed; returned files stay.
static void reader_drop_spill(ImapReader* reader) {
    if (reader->spill_file) {
        fclose(reader->spill_file);
        reader->spill_file = NULL;
        if (reader->literal_count > 0 && reader->paths[reader->literal_count - 1])
            remove(reader->paths[reader->literal_count - 1]);
    }
}

static void reader_reset_response(ImapReader* reader) {
    strbuf_reset(reader->text);
    strbuf_reset(reader->bytes);
    for (int i = 0; i < reader->literal_count; i++) {
        mem_free(reader->paths[i]);
        reader->paths[i] = NULL;
    }
    reader->literal_count = 0;
    reader->literals_allowed = true;
    reader->in_literal = false;
    reader->literal_remaining = 0;
}

ImapReader* imap_reader_new(void) {
    ImapReader* reader = (ImapReader*)mem_calloc(1, sizeof(ImapReader), MEM_CAT_NETWORK);
    if (!reader) return NULL;
    reader->in = strbuf_new_cap(16 * 1024);
    reader->text = strbuf_new_cap(4 * 1024);
    reader->bytes = strbuf_new_cap(4 * 1024);
    reader->literals_allowed = true;
    if (!reader->in || !reader->text || !reader->bytes) {
        imap_reader_free(reader);
        return NULL;
    }
    return reader;
}

void imap_reader_free(ImapReader* reader) {
    if (!reader) return;
    reader_drop_spill(reader);
    reader_reset_response(reader);
    if (reader->in) strbuf_free(reader->in);
    if (reader->text) strbuf_free(reader->text);
    if (reader->bytes) strbuf_free(reader->bytes);
    mem_free(reader->literals);
    mem_free(reader->paths);
    mem_free(reader->spill_dir);
    mem_free(reader);
}

bool imap_reader_set_spill(ImapReader* reader, const char* dir, size_t threshold) {
    if (!reader) return false;
    mem_free(reader->spill_dir);
    reader->spill_dir = dir ? mem_strdup(dir, MEM_CAT_NETWORK) : NULL;
    reader->spill_threshold = threshold;
    return !dir || reader->spill_dir;
}

bool imap_reader_feed(ImapReader* reader, const char* data, size_t length) {
    if (!reader || reader->failed) return false;
    // drop consumed bytes before growing, so a long session stays bounded
    if (reader->in_pos > 0 && reader->in_pos * 2 >= reader->in->length) {
        size_t rest = reader->in->length - reader->in_pos;
        memmove(reader->in->str, reader->in->str + reader->in_pos, rest);
        reader->in->length = rest;
        reader->in->str[rest] = '\0';
        reader->in_pos = 0;
    }
    if (length) strbuf_append_str_n(reader->in, data, length);
    return true;
}

bool imap_reader_pending(const ImapReader* reader) {
    return reader && (reader->text->length > 0 || reader->in_literal ||
                      reader->in->length > reader->in_pos);
}

const char* imap_reader_error(const ImapReader* reader) {
    return reader && reader->error[0] ? reader->error : "no error";
}

static bool ascii_word_is(const char* s, size_t n, const char* word) {
    size_t w = strlen(word);
    if (n < w) return false;
    for (size_t i = 0; i < w; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != word[i]) return false;
    }
    return n == w || s[w] == ' ' || s[w] == '\r' || s[w] == '\n';
}

// Status and continuation responses carry no literals, so a `{n}` that ends
// their human-readable text is text, not a literal marker.
static bool first_line_allows_literals(const char* line, size_t n) {
    if (n >= 1 && line[0] == '+') return false;
    if (n >= 2 && line[0] == '*' && line[1] == ' ') {
        const char* word = line + 2;
        size_t rest = n - 2;
        static const char* statuses[] = {"OK", "NO", "BAD", "BYE", "PREAUTH"};
        for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++)
            if (ascii_word_is(word, rest, statuses[i])) return false;
        return true;
    }
    return false;   // a tagged response is always a status response
}

// The `{n}` or `{n+}` marker ending a line's content, if any.
static bool line_literal_marker(const char* line, size_t n, uint64_t* size) {
    if (n < 3 || line[n - 1] != '}') return false;
    size_t i = n - 1;
    if (i > 0 && line[i - 1] == '+') i--;
    size_t digits_end = i;
    while (i > 0 && line[i - 1] >= '0' && line[i - 1] <= '9') i--;
    if (i == digits_end || i == 0 || line[i - 1] != '{') return false;
    if (digits_end - i > 19) return false;
    uint64_t value = 0;
    for (size_t k = i; k < digits_end; k++) value = value * 10 + (uint64_t)(line[k] - '0');
    *size = value;
    return true;
}

static bool reader_add_literal(ImapReader* reader, uint64_t size) {
    if (reader->literal_count >= IMAP_MAX_LITERALS) return false;
    if (reader->literal_count == reader->literal_capacity) {
        int capacity = reader->literal_capacity ? reader->literal_capacity * 2 : 8;
        ImapLiteral* literals = (ImapLiteral*)mem_realloc(reader->literals,
            (size_t)capacity * sizeof(ImapLiteral), MEM_CAT_NETWORK);
        if (!literals) return false;
        reader->literals = literals;
        char** paths = (char**)mem_realloc(reader->paths, (size_t)capacity * sizeof(char*), MEM_CAT_NETWORK);
        if (!paths) return false;
        reader->paths = paths;
        for (int i = reader->literal_capacity; i < capacity; i++) reader->paths[i] = NULL;
        reader->literal_capacity = capacity;
    }
    ImapLiteral* literal = &reader->literals[reader->literal_count];
    literal->offset = reader->bytes->length;
    literal->length = (size_t)size;
    literal->path = NULL;
    reader->paths[reader->literal_count] = NULL;
    reader->literal_count++;
    return true;
}

static int reader_open_spill(ImapReader* reader) {
    char path[1024];
    for (int attempt = 0; attempt < 64; attempt++) {
        snprintf(path, sizeof(path), "%s/imap-literal-%u-%u.bin", reader->spill_dir,
                 (unsigned)(uintptr_t)reader & 0xffffu, ++reader->spill_sequence);
        // "x": never overwrite a file the caller already keeps there
        FILE* file = fopen(path, "wbx");
        if (file) {
            int index = reader->literal_count - 1;
            reader->paths[index] = mem_strdup(path, MEM_CAT_NETWORK);
            if (!reader->paths[index]) {
                fclose(file);
                remove(path);
                return reader_fail(reader, "out of memory naming a literal file");
            }
            reader->literals[index].path = reader->paths[index];
            reader->spill_file = file;
            return 0;
        }
        if (errno != EEXIST) break;
    }
    return reader_fail(reader, "cannot create a literal file in the 'to' directory");
}

int imap_reader_next(ImapReader* reader, ImapResponse* out) {
    if (!reader || !out) return -1;
    if (reader->failed) return -1;
    if (reader->returned) {
        reader->returned = false;
        reader_reset_response(reader);
    }
    for (;;) {
        size_t avail = reader->in->length - reader->in_pos;
        const char* data = reader->in->str + reader->in_pos;
        if (reader->in_literal) {
            if (avail == 0) return 0;
            size_t take = reader->literal_remaining < avail ? (size_t)reader->literal_remaining : avail;
            if (reader->spill_file) {
                if (fwrite(data, 1, take, reader->spill_file) != take) {
                    reader_drop_spill(reader);
                    return reader_fail(reader, "cannot write a literal file");
                }
            } else {
                strbuf_append_str_n(reader->bytes, data, take);
            }
            reader->in_pos += take;
            reader->literal_remaining -= take;
            if (reader->literal_remaining == 0) {
                reader->in_literal = false;
                if (reader->spill_file) {
                    bool ok = fclose(reader->spill_file) == 0;
                    reader->spill_file = NULL;
                    if (!ok) return reader_fail(reader, "cannot finish a literal file");
                }
            }
            continue;
        }
        const char* lf = (const char*)memchr(data, '\n', avail);
        if (!lf) {
            if (avail > IMAP_MAX_LINE) return reader_fail(reader, "response line too long");
            return 0;
        }
        size_t line_length = (size_t)(lf - data) + 1;
        if (line_length > IMAP_MAX_LINE) return reader_fail(reader, "response line too long");
        if (reader->text->length + line_length > IMAP_MAX_TEXT) return reader_fail(reader, "response too long");
        if (reader->text->length == 0) reader->literals_allowed = first_line_allows_literals(data, line_length);
        strbuf_append_str_n(reader->text, data, line_length);
        reader->in_pos += line_length;
        size_t content = line_length - 1;
        if (content > 0 && data[content - 1] == '\r') content--;
        uint64_t size = 0;
        if (reader->literals_allowed && line_literal_marker(data, content, &size)) {
            bool spill = reader->spill_dir && size > reader->spill_threshold;
            if (size > (spill ? IMAP_MAX_SPILL_LITERAL : (uint64_t)IMAP_MAX_MEMORY_LITERAL))
                return reader_fail(reader, "literal too large");
            if (!reader_add_literal(reader, size)) return reader_fail(reader, "too many literals in one response");
            if (spill && reader_open_spill(reader) < 0) return -1;
            reader->in_literal = size > 0;
            reader->literal_remaining = size;
            if (size == 0 && reader->spill_file) {
                fclose(reader->spill_file);
                reader->spill_file = NULL;
            }
            continue;
        }
        out->text = reader->text->str;
        out->text_length = reader->text->length;
        out->bytes = reader->bytes->str;
        out->literals = reader->literals;
        out->literal_count = reader->literal_count;
        reader->returned = true;
        return 1;
    }
}

// ── parsing ────────────────────────────────────────────────────────────

typedef struct ImapBlock {
    struct ImapBlock* next;
    size_t used;
    size_t size;
    // payload follows, 8-byte aligned
} ImapBlock;

struct ImapParser {
    ImapBlock* blocks;
    char error[200];
};

typedef struct ImapCursor {
    const char* s;
    size_t pos;
    size_t end;                 // before the final CRLF
    int literal_next;
    int literal_count;
    int depth;
} ImapCursor;

#define IMAP_BLOCK_HEADER ((sizeof(ImapBlock) + 7) & ~(size_t)7)

static void parser_free_blocks(ImapParser* parser) {
    ImapBlock* block = parser->blocks;
    while (block) {
        ImapBlock* next = block->next;
        mem_free(block);
        block = next;
    }
    parser->blocks = NULL;
}

static void* parser_alloc(ImapParser* parser, size_t size) {
    size = (size + 7) & ~(size_t)7;
    ImapBlock* block = parser->blocks;
    if (!block || block->size - block->used < size) {
        size_t payload = size > 16 * 1024 ? size : 16 * 1024;
        block = (ImapBlock*)mem_alloc(IMAP_BLOCK_HEADER + payload, MEM_CAT_NETWORK);
        if (!block) return NULL;
        block->next = parser->blocks;
        block->used = 0;
        block->size = payload;
        parser->blocks = block;
    }
    void* p = (char*)block + IMAP_BLOCK_HEADER + block->used;
    block->used += size;
    return p;
}

static int parse_fail(ImapParser* parser, const char* message, size_t pos) {
    snprintf(parser->error, sizeof(parser->error), "%s at offset %zu", message, pos);
    return -1;
}

ImapParser* imap_parser_new(void) {
    return (ImapParser*)mem_calloc(1, sizeof(ImapParser), MEM_CAT_NETWORK);
}

void imap_parser_free(ImapParser* parser) {
    if (!parser) return;
    parser_free_blocks(parser);
    mem_free(parser);
}

const char* imap_parser_error(const ImapParser* parser) {
    return parser && parser->error[0] ? parser->error : "no error";
}

static ImapNode* new_node(ImapParser* parser, ImapNodeKind kind) {
    ImapNode* node = (ImapNode*)parser_alloc(parser, sizeof(ImapNode));
    if (node) memset(node, 0, sizeof(*node));
    if (node) node->kind = kind;
    return node;
}

static char* copy_text(ImapParser* parser, const char* s, size_t n) {
    char* copy = (char*)parser_alloc(parser, n + 1);
    if (!copy) return NULL;
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

static void skip_spaces(ImapCursor* c) {
    while (c->pos < c->end && c->s[c->pos] == ' ') c->pos++;
}

// Atom bytes, leniently: anything printable but the structural specials.
// A `[` inside an atom opens a section (`BODY[HEADER.FIELDS (FROM)]`), which
// runs to its `]` whatever it holds.
static bool is_atom_byte(char ch, bool code) {
    unsigned char c = (unsigned char)ch;
    if (c <= 0x20 || c == 0x7f) return false;
    if (c == '(' || c == ')' || c == '{' || c == '"') return false;
    if (code && c == ']') return false;
    return true;
}

static int parse_items(ImapParser* parser, ImapCursor* c, char terminator, bool code, ImapNode** first);

static int parse_value(ImapParser* parser, ImapCursor* c, bool code, ImapNode** out) {
    char ch = c->s[c->pos];
    if (ch == '(') {
        if (++c->depth > IMAP_MAX_DEPTH) return parse_fail(parser, "lists nested too deeply", c->pos);
        c->pos++;
        ImapNode* list = new_node(parser, IMAP_NODE_LIST);
        if (!list) return parse_fail(parser, "out of memory", c->pos);
        if (parse_items(parser, c, ')', code, &list->first) < 0) return -1;
        if (c->pos >= c->end || c->s[c->pos] != ')') return parse_fail(parser, "unclosed list", c->pos);
        c->pos++;
        c->depth--;
        *out = list;
        return 1;
    }
    if (ch == '"') {
        size_t start = ++c->pos;
        size_t escapes = 0;
        while (c->pos < c->end && c->s[c->pos] != '"') {
            if (c->s[c->pos] == '\\') { c->pos++; escapes++; }
            if (c->pos < c->end && (c->s[c->pos] == '\r' || c->s[c->pos] == '\n'))
                return parse_fail(parser, "line break in a quoted string", c->pos);
            c->pos++;
        }
        if (c->pos >= c->end) return parse_fail(parser, "unclosed quoted string", start);
        size_t raw = c->pos - start;
        char* text = (char*)parser_alloc(parser, raw - escapes + 1);
        if (!text) return parse_fail(parser, "out of memory", c->pos);
        size_t n = 0;
        for (size_t i = start; i < c->pos; i++) {
            if (c->s[i] == '\\' && i + 1 < c->pos) i++;
            text[n++] = c->s[i];
        }
        text[n] = '\0';
        c->pos++;   // the closing quote
        ImapNode* node = new_node(parser, IMAP_NODE_STRING);
        if (!node) return parse_fail(parser, "out of memory", c->pos);
        node->text = text;
        node->length = n;
        *out = node;
        return 1;
    }
    if (ch == '{' || (ch == '~' && c->pos + 1 < c->end && c->s[c->pos + 1] == '{')) {
        // the reader already took the bytes: skip the marker and its CRLF
        const char* close = (const char*)memchr(c->s + c->pos, '}', c->end + 2 - c->pos);
        if (!close) return parse_fail(parser, "bad literal marker", c->pos);
        size_t after = (size_t)(close - c->s) + 1;
        if (after + 1 < c->end + 2 && c->s[after] == '\r') after++;
        if (after >= c->end + 2 || c->s[after] != '\n') return parse_fail(parser, "literal marker not at line end", c->pos);
        if (c->literal_next >= c->literal_count) return parse_fail(parser, "literal without bytes", c->pos);
        ImapNode* node = new_node(parser, IMAP_NODE_LITERAL);
        if (!node) return parse_fail(parser, "out of memory", c->pos);
        node->literal = c->literal_next++;
        c->pos = after + 1;
        *out = node;
        return 1;
    }
    size_t start = c->pos;
    while (c->pos < c->end) {
        char b = c->s[c->pos];
        if (b == '[' && !code) {
            const char* close = (const char*)memchr(c->s + c->pos, ']', c->end - c->pos);
            if (!close) return parse_fail(parser, "unclosed section", c->pos);
            c->pos = (size_t)(close - c->s) + 1;
            continue;
        }
        if (!is_atom_byte(b, code)) break;
        c->pos++;
    }
    if (c->pos == start) return parse_fail(parser, "unexpected character", c->pos);
    size_t n = c->pos - start;
    const char* s = c->s + start;
    if (n == 3 && (s[0] | 0x20) == 'n' && (s[1] | 0x20) == 'i' && (s[2] | 0x20) == 'l') {
        *out = new_node(parser, IMAP_NODE_NIL);
        return *out ? 1 : parse_fail(parser, "out of memory", c->pos);
    }
    bool digits = n <= 19;
    for (size_t i = 0; digits && i < n; i++) digits = s[i] >= '0' && s[i] <= '9';
    ImapNode* node = new_node(parser, digits ? IMAP_NODE_NUMBER : IMAP_NODE_ATOM);
    if (!node) return parse_fail(parser, "out of memory", c->pos);
    if (digits) {
        uint64_t value = 0;
        for (size_t i = 0; i < n; i++) value = value * 10 + (uint64_t)(s[i] - '0');
        node->number = value;
    }
    node->text = copy_text(parser, s, n);
    node->length = n;
    if (!node->text) return parse_fail(parser, "out of memory", c->pos);
    *out = node;
    return 1;
}

// Items up to `terminator` (or the end of the response for '\0').
static int parse_items(ImapParser* parser, ImapCursor* c, char terminator, bool code, ImapNode** first) {
    ImapNode** tail = first;
    *first = NULL;
    for (;;) {
        skip_spaces(c);
        if (c->pos >= c->end) return terminator ? parse_fail(parser, "unexpected end of response", c->pos) : 0;
        if (terminator && c->s[c->pos] == terminator) return 0;
        ImapNode* node = NULL;
        if (parse_value(parser, c, code, &node) < 0) return -1;
        *tail = node;
        tail = &node->next;
    }
}

static int parse_status(ImapParser* parser, ImapCursor* c, ImapParsed* out) {
    size_t start = c->pos;
    while (c->pos < c->end && c->s[c->pos] != ' ') c->pos++;
    size_t n = c->pos - start;
    char* status = copy_text(parser, c->s + start, n);
    if (!status) return parse_fail(parser, "out of memory", c->pos);
    for (size_t i = 0; i < n; i++) if (status[i] >= 'a' && status[i] <= 'z') status[i] = (char)(status[i] - 32);
    out->status = status;
    if (c->pos < c->end) c->pos++;   // one space before the text
    if (c->pos < c->end && c->s[c->pos] == '[') {
        c->pos++;
        if (parse_items(parser, c, ']', true, &out->code) < 0) return -1;
        c->pos++;   // ']'
        if (c->pos < c->end && c->s[c->pos] == ' ') c->pos++;
    }
    out->text = c->s + c->pos;
    out->text_length = c->end - c->pos;
    return 1;
}

int imap_parse(ImapParser* parser, const ImapResponse* response, ImapParsed* out) {
    if (!parser || !response || !out) return -1;
    parser_free_blocks(parser);
    parser->error[0] = '\0';
    memset(out, 0, sizeof(*out));
    ImapCursor c = {response->text, 0, response->text_length, 0, response->literal_count, 0};
    // the final CRLF ends the response; earlier ones follow literal markers
    if (c.end > 0 && c.s[c.end - 1] == '\n') c.end--;
    if (c.end > 0 && c.s[c.end - 1] == '\r') c.end--;
    if (c.end == 0) return parse_fail(parser, "empty response", 0);
    if (c.s[0] == '+') {
        out->kind = IMAP_RESPONSE_CONTINUATION;
        c.pos = (c.end > 1 && c.s[1] == ' ') ? 2 : 1;
        out->text = c.s + c.pos;
        out->text_length = c.end - c.pos;
        return 1;
    }
    if (c.s[0] == '*') {
        out->kind = IMAP_RESPONSE_UNTAGGED;
        c.pos = 1;
        skip_spaces(&c);
        static const char* statuses[] = {"OK", "NO", "BAD", "BYE", "PREAUTH"};
        for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
            if (ascii_word_is(c.s + c.pos, c.end - c.pos, statuses[i])) return parse_status(parser, &c, out);
        }
        return parse_items(parser, &c, '\0', false, &out->data) < 0 ? -1 : 1;
    }
    out->kind = IMAP_RESPONSE_TAGGED;
    while (c.pos < c.end && c.s[c.pos] != ' ') c.pos++;
    out->tag = c.s;
    out->tag_length = c.pos;
    skip_spaces(&c);
    return parse_status(parser, &c, out);
}

// ── commands ───────────────────────────────────────────────────────────

static bool command_fail(ImapCommand* command, const char* message) {
    snprintf(command->error, sizeof(command->error), "%s", message);
    return false;
}

static void command_space(ImapCommand* command) {
    if (command->need_space) strbuf_append_char(command->bytes, ' ');
    command->need_space = true;
}

bool imap_command_init(ImapCommand* command, size_t nonsync_limit, bool utf8) {
    memset(command, 0, sizeof(*command));
    command->bytes = strbuf_new_cap(256);
    command->nonsync_limit = nonsync_limit;
    command->utf8 = utf8;
    return command->bytes != NULL;
}

void imap_command_free(ImapCommand* command) {
    if (!command) return;
    if (command->bytes) strbuf_free(command->bytes);
    mem_free(command->waits);
    command->bytes = NULL;
    command->waits = NULL;
}

bool imap_command_begin(ImapCommand* command, const char* tag, const char* text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r' || c == '\n' || c == 0) return command_fail(command, "command text may not hold CR, LF or NUL");
    }
    if (length == 0) return command_fail(command, "empty command");
    strbuf_append_str(command->bytes, tag);
    strbuf_append_char(command->bytes, ' ');
    strbuf_append_str_n(command->bytes, text, length);
    command->need_space = true;
    return true;
}

bool imap_command_atom(ImapCommand* command, const char* text, size_t length) {
    if (length == 0) return command_fail(command, "empty atom");
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c <= 0x20 || c >= 0x7f || c == '(' || c == ')' || c == '{' || c == '"')
            return command_fail(command, "atom holds a character that needs quoting");
    }
    command_space(command);
    strbuf_append_str_n(command->bytes, text, length);
    return true;
}

void imap_command_number(ImapCommand* command, uint64_t number) {
    command_space(command);
    strbuf_append_uint64(command->bytes, number);
}

static bool can_quote(const ImapCommand* command, const char* text, size_t length) {
    if (length > IMAP_QUOTED_LIMIT) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r' || c == '\n' || c == 0) return false;
        if (c >= 0x80 && !command->utf8) return false;
    }
    return true;
}

bool imap_command_string(ImapCommand* command, const char* text, size_t length) {
    if (!can_quote(command, text, length)) return imap_command_literal(command, text, length);
    command_space(command);
    strbuf_append_char(command->bytes, '"');
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '"' || text[i] == '\\') strbuf_append_char(command->bytes, '\\');
        strbuf_append_char(command->bytes, text[i]);
    }
    strbuf_append_char(command->bytes, '"');
    return true;
}

bool imap_command_literal(ImapCommand* command, const char* bytes, size_t length) {
    if (memchr(bytes, 0, length)) return command_fail(command, "a literal may not hold NUL bytes");
    bool nonsync = length <= command->nonsync_limit;
    command_space(command);
    strbuf_append_char(command->bytes, '{');
    strbuf_append_uint64(command->bytes, (uint64_t)length);
    if (nonsync) strbuf_append_char(command->bytes, '+');
    strbuf_append_str(command->bytes, "}\r\n");
    if (!nonsync) {
        if (command->wait_count == command->wait_capacity) {
            int capacity = command->wait_capacity ? command->wait_capacity * 2 : 4;
            size_t* waits = (size_t*)mem_realloc(command->waits, (size_t)capacity * sizeof(size_t), MEM_CAT_NETWORK);
            if (!waits) return command_fail(command, "out of memory");
            command->waits = waits;
            command->wait_capacity = capacity;
        }
        command->waits[command->wait_count++] = command->bytes->length;
    }
    strbuf_append_str_n(command->bytes, bytes, length);
    return true;
}

void imap_command_nil(ImapCommand* command) {
    command_space(command);
    strbuf_append_str(command->bytes, "NIL");
}

void imap_command_list_begin(ImapCommand* command) {
    command_space(command);
    strbuf_append_char(command->bytes, '(');
    command->need_space = false;
    command->depth++;
}

bool imap_command_list_end(ImapCommand* command) {
    if (command->depth == 0) return command_fail(command, "unbalanced list");
    strbuf_append_char(command->bytes, ')');
    command->need_space = true;
    command->depth--;
    return true;
}

bool imap_command_end(ImapCommand* command) {
    if (command->depth != 0) return command_fail(command, "unbalanced list");
    strbuf_append_str(command->bytes, "\r\n");
    return true;
}
