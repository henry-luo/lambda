// mail_imap.h — IMAP response framing, response parsing and command encoding
// (RFC 9051, RFC 3501) for lambda.io.mail (vibe/Lambda_Demo_Mail.md ML1, ML2).
//
// Context-free (D7.1.2v2): bytes in, framed responses and token trees out. The
// runtime sends the encoded commands and turns the trees into Lambda values.
// Framing reads literals by their byte count, so message content can never end
// or forge a response (the curl custom-request hazard of §2.4).

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── framing ────────────────────────────────────────────────────────────

// A literal of one response: in memory at [offset, offset + length) of the
// response's literal bytes, or streamed into the file at `path`.
typedef struct ImapLiteral {
    size_t offset;
    size_t length;
    const char* path;
} ImapLiteral;

// One complete server response. `text` holds its lines, CRLFs included and
// literal bytes excluded; each literal's `{n}` marker stays in the text.
typedef struct ImapResponse {
    const char* text;
    size_t text_length;
    const char* bytes;
    const ImapLiteral* literals;
    int literal_count;
} ImapResponse;

typedef struct ImapReader ImapReader;

ImapReader* imap_reader_new(void);
void imap_reader_free(ImapReader* reader);
// Literals longer than `threshold` stream into new files under `dir`; the
// files belong to the caller once their response is returned. NULL keeps
// every literal in memory.
bool imap_reader_set_spill(ImapReader* reader, const char* dir, size_t threshold);
// Appends received bytes.
bool imap_reader_feed(ImapReader* reader, const char* data, size_t length);
// 1: *out holds a complete response, valid until the next call; 0: more bytes
// are needed; -1: a protocol or file error (imap_reader_error says which).
int imap_reader_next(ImapReader* reader, ImapResponse* out);
// True while a response is partly read.
bool imap_reader_pending(const ImapReader* reader);
const char* imap_reader_error(const ImapReader* reader);

// ── parsing ────────────────────────────────────────────────────────────

typedef enum ImapNodeKind {
    IMAP_NODE_ATOM,
    IMAP_NODE_NUMBER,
    IMAP_NODE_STRING,
    IMAP_NODE_LITERAL,
    IMAP_NODE_NIL,
    IMAP_NODE_LIST,
} ImapNodeKind;

typedef struct ImapNode {
    ImapNodeKind kind;
    const char* text;          // atom or string, NUL-terminated (strings unescaped)
    size_t length;
    uint64_t number;
    int literal;               // index into ImapResponse.literals
    struct ImapNode* first;    // list items
    struct ImapNode* next;     // the next sibling
} ImapNode;

typedef enum ImapResponseKind {
    IMAP_RESPONSE_UNTAGGED,
    IMAP_RESPONSE_TAGGED,
    IMAP_RESPONSE_CONTINUATION,
} ImapResponseKind;

// A status response (tagged, or untagged OK/NO/BAD/BYE/PREAUTH) has `status`,
// an optional bracketed response code as tokens, and the rest of its line as
// `text`. Any other untagged response is the token list `data`. A
// continuation has only `text`.
typedef struct ImapParsed {
    ImapResponseKind kind;
    const char* tag;
    size_t tag_length;
    const char* status;        // upper case, NULL when not a status response
    ImapNode* code;
    const char* text;
    size_t text_length;
    ImapNode* data;
} ImapParsed;

typedef struct ImapParser ImapParser;

ImapParser* imap_parser_new(void);
void imap_parser_free(ImapParser* parser);
// Parses one framed response; its nodes live until the next call. -1 on a
// malformed response (imap_parser_error says why).
int imap_parse(ImapParser* parser, const ImapResponse* response, ImapParsed* out);
const char* imap_parser_error(const ImapParser* parser);

// ── commands ───────────────────────────────────────────────────────────

// One command being encoded. A synchronizing literal ends a segment: the
// sender writes bytes up to each wait offset, then waits for the server's
// continuation before writing on.
typedef struct ImapCommand {
    StrBuf* bytes;
    size_t* waits;
    int wait_count;
    int wait_capacity;
    size_t nonsync_limit;      // literals up to this size need no continuation
    bool utf8;                 // UTF8=ACCEPT: 8-bit text may be quoted
    bool need_space;
    int depth;
    char error[160];
} ImapCommand;

// `nonsync_limit`: SIZE_MAX with LITERAL+, 4096 with LITERAL-, else 0.
bool imap_command_init(ImapCommand* command, size_t nonsync_limit, bool utf8);
void imap_command_free(ImapCommand* command);
// The tag and the command text, which may hold atoms and sets but no CR, LF
// or NUL: arguments that need quoting go through the calls below.
bool imap_command_begin(ImapCommand* command, const char* tag, const char* text, size_t length);
bool imap_command_atom(ImapCommand* command, const char* text, size_t length);
void imap_command_number(ImapCommand* command, uint64_t number);
// A quoted string, or a literal when the text cannot be quoted.
bool imap_command_string(ImapCommand* command, const char* text, size_t length);
bool imap_command_literal(ImapCommand* command, const char* bytes, size_t length);
void imap_command_nil(ImapCommand* command);
void imap_command_list_begin(ImapCommand* command);
bool imap_command_list_end(ImapCommand* command);
bool imap_command_end(ImapCommand* command);

#ifdef __cplusplus
}
#endif
