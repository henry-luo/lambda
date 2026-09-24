#include "source_tracker.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include <cstring>

namespace lambda {

SourceTracker::SourceTracker(const char* source, size_t len)
    : source_(source)
    , source_len_(len)
    , current_(source)
    , location_(0, 1, 1)
    , synced_(0)
    // the line index is built only on demand (error context), so start small
    // instead of reserving one slot per 40 source bytes up front
    , line_starts_(MEM_CAT_INPUT_OTHER, 16)
    , line_index_built_(false)
    , line_index_failed_(false)
    , extract_buf_(strbuf_new_cap(256))
{
    pushLineStart(0);  // Line 1 starts at offset 0
}

SourceTracker::~SourceTracker() {
    strbuf_free(extract_buf_);
}

bool SourceTracker::pushLineStart(size_t offset) {
    if (line_index_failed_) return false;
    if (!line_starts_.append(offset)) {
        log_error("SourceTracker: line index allocation failed at %zu lines", line_starts_.size());
        line_index_failed_ = true;
        return false;
    }
    return true;
}

void SourceTracker::buildLineIndex() {
    if (line_index_built_) return;
    line_starts_.clear();
    pushLineStart(0);
    const char* p = source_;
    const char* end = source_ + source_len_;
    while (p < end) {
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        if (!nl) break;
        if (!pushLineStart((size_t)(nl - source_) + 1)) break;
        p = nl + 1;
    }
    line_index_built_ = true;
}

char SourceTracker::peek(size_t ahead) const {
    if (current_ + ahead >= source_ + source_len_) {
        return '\0';
    }
    return current_[ahead];
}

// Catch line/column up from synced_ to the current offset: newlines via
// memchr, then the column counts the non-continuation bytes after the last
// newline -- the same values the per-byte loop produced. Queries are mostly
// forward, so the total work stays linear; a backward move (reset, seek)
// recounts from the start.
void SourceTracker::sync() const {
    size_t off = (size_t)(current_ - source_);
    if (off == synced_) return;
    if (off < synced_) {
        location_ = SourceLocation(0, 1, 1);
        synced_ = 0;
    }
    const char* p = source_ + synced_;
    const char* end = source_ + off;
    const char* col_from = p;
    for (const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p)); nl;
            nl = (const char*)memchr(nl + 1, '\n', (size_t)(end - nl - 1))) {
        location_.line++;
        location_.column = 1;
        col_from = nl + 1;
    }
    size_t lead = 0;
    for (const char* q = col_from; q < end; q++) {
        lead += !isUtf8Continuation((unsigned char)*q);
    }
    location_.column += lead;
    location_.offset = off;
    synced_ = off;
}

bool SourceTracker::advanceChar() {
    if (atEnd()) return false;

    // Advance past the current character
    advance(1);

    // Skip UTF-8 continuation bytes
    while (!atEnd() && isUtf8Continuation((unsigned char)*current_)) {
        current_++;
    }

    return true;
}

size_t SourceTracker::skipWhitespace() {
    size_t count = 0;
    while (!atEnd() && str_char_is_ascii_space(*current_)) {
        advance(1);
        count++;
    }
    return count;
}

bool SourceTracker::match(const char* str) {
    if (!str) return false;

    size_t len = std::strlen(str);
    if (remaining() < len) return false;

    return std::strncmp(current_, str, len) == 0;
}

bool SourceTracker::match(char c) {
    return !atEnd() && *current_ == c;
}

const char* SourceTracker::extract(size_t start_offset, size_t end_offset) {
    if (start_offset >= source_len_ || end_offset > source_len_ || start_offset >= end_offset) {
        return "";
    }

    strbuf_reset(extract_buf_);
    strbuf_append_str_n(extract_buf_, source_ + start_offset, end_offset - start_offset);
    return extract_buf_->str;
}

const char* SourceTracker::extractLine(size_t line_num) {
    if (line_num < 1) return "";

    // Build line index if needed
    buildLineIndex();

    if (line_num > line_starts_.size()) return "";

    size_t start = line_starts_[line_num - 1];
    size_t end = source_len_;

    if (line_num < line_starts_.size()) {
        end = line_starts_[line_num] - 1;  // Exclude the newline
    }

    // Trim trailing newline/carriage return
    while (end > start && (source_[end - 1] == '\n' || source_[end - 1] == '\r')) {
        end--;
    }

    return extract(start, end);
}

const char* SourceTracker::getContextLine() {
    return extractLine(line());
}

void SourceTracker::reset() {
    // the line index depends only on the source, so it survives a reset
    current_ = source_;
    location_ = SourceLocation(0, 1, 1);
    synced_ = 0;
}

bool SourceTracker::seek(size_t offset) {
    if (offset > source_len_) return false;
    reset();
    return advance(offset);
}

} // namespace lambda
