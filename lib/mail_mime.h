// mail_mime.h — MIME header decoding for mail (vibe/Lambda_Demo_Mail.md MG5):
// charsets to UTF-8, RFC 2047 encoded words, RFC 2045/2231 parameters and
// RFC 5322 address lists. Context-free (D7.1.2v2); the eml reader builds
// Lambda values from the results.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

// Appends `length` bytes in `charset` to `out` as UTF-8. Invalid sequences
// become U+FFFD; an unknown charset is read as UTF-8, and failing that as
// windows-1252. False only when memory runs out.
bool mime_to_utf8(const char* charset, size_t charset_length, const char* bytes, size_t length, StrBuf* out);

// Appends an unfolded header value to `out` with its RFC 2047 encoded words
// decoded. Raw 8-bit text that is not UTF-8 is read as windows-1252.
bool mime_decode_header(const char* value, size_t length, StrBuf* out);

// The value's leading token, lower-cased ("text/plain" from a Content-Type).
void mime_header_token(const char* value, size_t length, StrBuf* out);

// Appends the decoded value of parameter `name` from a Content-Type or
// Content-Disposition value, joining RFC 2231 continuations and decoding
// `name*=charset'lang'value`. With `decode_words`, stray RFC 2047 words in a
// plain value are decoded too (filenames); never for a boundary, whose legal
// characters include `=` and `?`. False when absent.
bool mime_header_param(const char* value, size_t length, const char* name, bool decode_words, StrBuf* out);

// Calls `visit` for each mailbox in an address-list header, with its decoded
// display name (possibly empty) and its addr-spec. Groups are flattened.
// Returns the number of mailboxes.
typedef void (*MimeAddressVisitor)(void* user, const char* name, size_t name_length,
                                   const char* address, size_t address_length);
int mime_parse_addresses(const char* value, size_t length, MimeAddressVisitor visit, void* user);

#ifdef __cplusplus
}
#endif
