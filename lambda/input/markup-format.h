/**
 * markup-format.h - Markup format enumeration
 *
 * This minimal header defines just the MarkupFormat enum to avoid
 * circular dependencies between input.hpp and markup-parser.h.
 */
#ifndef MARKUP_FORMAT_H
#define MARKUP_FORMAT_H

// Markup format enumeration
typedef enum {
    MARKUP_MARKDOWN,
    MARKUP_RST,
    MARKUP_TEXTILE,
    MARKUP_WIKI,
    MARKUP_ORG,
    MARKUP_ASCIIDOC,
    MARKUP_MAN,        // Unix man pages (troff)
    MARKUP_AUTO_DETECT
} MarkupFormat;

#endif // MARKUP_FORMAT_H
