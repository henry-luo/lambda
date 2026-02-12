#pragma once
#ifndef LAMBDA_SOURCE_TRACKER_HPP
#define LAMBDA_SOURCE_TRACKER_HPP

#include "parse_error.hpp"
#include "../../lib/strbuf.h"
#include <cstdint>
#include <cstddef>

namespace lambda {

// Tracks position in source text with O(1) line/column updates
// Handles UTF-8 multi-byte characters correctly
class SourceTracker {
private:
    static const size_t MAX_LINE_STARTS = 100000;  // Max tracked lines (100K)

    const char* source_;        // Source text (not owned)
    size_t source_len_;         // Total length of source
    const char* current_;       // Current position

    SourceLocation location_;   // Current location

    // Line start positions for fast context extraction
    size_t line_starts_[MAX_LINE_STARTS];
    size_t line_count_;         // Number of lines tracked

    // Track if we've built the line index
    bool line_index_built_;

    // Reusable buffer for extract operations
    StrBuf* extract_buf_;

    // Build line index lazily
    void buildLineIndex();

    // Check if byte is UTF-8 continuation byte (10xxxxxx)
    static bool isUtf8Continuation(unsigned char byte) {
        return (byte & 0xC0) == 0x80;
    }

public:
    SourceTracker(const char* source, size_t len);
    ~SourceTracker();

    // Non-copyable
    SourceTracker(const SourceTracker&) = delete;
    SourceTracker& operator=(const SourceTracker&) = delete;

    // Current position info
    const SourceLocation& location() const { return location_; }
    size_t offset() const { return location_.offset; }
    size_t line() const { return location_.line; }
    size_t column() const { return location_.column; }

    // Current character access
    char current() const { return *current_; }
    char peek(size_t ahead = 1) const;
    bool atEnd() const { return current_ >= source_ + source_len_; }
    size_t remaining() const { return (source_ + source_len_) - current_; }

    // Movement - returns true if successful
    bool advance(size_t count = 1);
    bool advanceChar();  // Advance one UTF-8 character

    // Skip whitespace, return number of chars skipped
    size_t skipWhitespace();

    // Pattern matching
    bool match(const char* str);  // Check if current position matches string
    bool match(char c);           // Check if current char matches

    // Extract text - returns internal buffer, valid until next extract call
    const char* extract(size_t start_offset, size_t end_offset);
    const char* extractLine(size_t line_num);
    const char* getContextLine();  // Get current line

    // Get substring from current position
    const char* rest() const { return current_; }

    // Reset to beginning
    void reset();
};

} // namespace lambda

#endif // LAMBDA_SOURCE_TRACKER_HPP
