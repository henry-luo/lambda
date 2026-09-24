#pragma once
#ifndef LAMBDA_SOURCE_TRACKER_HPP
#define LAMBDA_SOURCE_TRACKER_HPP

#include "parse_error.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/arraylist.hpp"
#include <cstdint>
#include <cstddef>

namespace lambda {

// Tracks position in source text with O(1) line/column updates
// Handles UTF-8 multi-byte characters correctly
class SourceTracker {
private:
    const char* source_;        // Source text (not owned)
    size_t source_len_;         // Total length of source
    const char* current_;       // Current position

    // Line and column are derived lazily: advance() only moves current_, and
    // location()/line()/column() catch location_ up from synced_. Eager
    // per-byte tracking (newline, UTF-8 continuation, offset and column
    // stores on every byte) was a quarter of JSON parse time (string tuning
    // P3). The derived values are identical at every query.
    mutable SourceLocation location_;   // valid at offset synced_
    mutable size_t synced_;
    void sync() const;

    // Line start positions for fast context extraction. Grown from the
    // document (SCU16): a tracker never embeds a maximum-document table, so
    // `sizeof(SourceTracker)` is independent of the line count and an
    // InputContext can live on the stack.
    lam::ArrayList<size_t> line_starts_;

    // Track if we've built the line index
    bool line_index_built_;
    // Set once a line-start allocation failed; context extraction beyond the
    // recorded lines then reports empty text instead of guessing.
    bool line_index_failed_;

    // Append one line start; false (and logs once) when memory is exhausted.
    bool pushLineStart(size_t offset);

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
    const SourceLocation& location() const { sync(); return location_; }
    size_t offset() const { return (size_t)(current_ - source_); }
    size_t line() const { sync(); return location_.line; }
    size_t column() const { sync(); return location_.column; }

    // Current character access
    char current() const { return *current_; }
    char peek(size_t ahead = 1) const;
    bool atEnd() const { return current_ >= source_ + source_len_; }
    size_t remaining() const { return (source_ + source_len_) - current_; }

    // Movement - returns true if successful. Inline and position-only: a
    // parser may call it for every character.
    bool advance(size_t count = 1) {
        size_t rest = remaining();
        if (count <= rest) {
            current_ += count;
            return true;
        }
        current_ += rest;   // stop at the end, as the per-byte loop did
        return false;
    }
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

    // Move to a byte offset, rebuilding line/column state as needed.
    // This is intentionally an explicit seek so adapters that still expose
    // pointer cursors can report diagnostics through the shared tracker.
    bool seek(size_t offset);
};

} // namespace lambda

#endif // LAMBDA_SOURCE_TRACKER_HPP
