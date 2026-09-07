#include "source_tracker.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/memtrack.h"
#include <cstring>

namespace lambda {

SourceTracker::SourceTracker(const char* source, size_t len)
    : source_(source)
    , source_len_(len)
    , current_(source)
    , location_(0, 1, 1)
    , line_starts_(nullptr)
    , line_cap_(0)
    , line_count_(0)
    , line_index_built_(false)
    , line_index_failed_(false)
    , extract_buf_(strbuf_new_cap(256))
{
    pushLineStart(0);  // Line 1 starts at offset 0
}

SourceTracker::~SourceTracker() {
    strbuf_free(extract_buf_);
    if (line_starts_) mem_free(line_starts_);
}

bool SourceTracker::pushLineStart(size_t offset) {
    if (line_index_failed_) return false;
    if (line_count_ == line_cap_) {
        // Size the first block from the source so small documents allocate
        // once; grow geometrically afterwards.
        size_t new_cap = line_cap_ ? line_cap_ * 2 : (source_len_ / 40 + 16);
        size_t* grown = (size_t*)mem_realloc(line_starts_, new_cap * sizeof(size_t),
            MEM_CAT_INPUT_OTHER);
        if (!grown) {
            log_error("SourceTracker: line index allocation failed at %zu lines", line_count_);
            line_index_failed_ = true;
            return false;
        }
        line_starts_ = grown;
        line_cap_ = new_cap;
    }
    line_starts_[line_count_++] = offset;
    return true;
}

void SourceTracker::buildLineIndex() {
    if (line_index_built_) return;

    line_count_ = 0;
    pushLineStart(0);

    for (size_t i = 0; i < source_len_; ++i) {
        if (source_[i] == '\n') {
            if (!pushLineStart(i + 1)) break;
        }
    }

    line_index_built_ = true;
}

char SourceTracker::peek(size_t ahead) const {
    if (current_ + ahead >= source_ + source_len_) {
        return '\0';
    }
    return current_[ahead];
}

bool SourceTracker::advance(size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (atEnd()) return false;

        char c = *current_;
        current_++;
        location_.offset++;

        if (c == '\n') {
            location_.line++;
            location_.column = 1;

            // Track line start for context extraction
            if (!line_index_built_) pushLineStart(location_.offset);
        } else if (!isUtf8Continuation((unsigned char)c)) {
            // Only increment column for non-continuation bytes
            location_.column++;
        }
    }

    return true;
}

bool SourceTracker::advanceChar() {
    if (atEnd()) return false;

    // Advance past the current character
    advance(1);

    // Skip UTF-8 continuation bytes
    while (!atEnd() && isUtf8Continuation((unsigned char)*current_)) {
        current_++;
        location_.offset++;
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

    if (line_num > line_count_) return "";

    size_t start = line_starts_[line_num - 1];
    size_t end = source_len_;

    if (line_num < line_count_) {
        end = line_starts_[line_num] - 1;  // Exclude the newline
    }

    // Trim trailing newline/carriage return
    while (end > start && (source_[end - 1] == '\n' || source_[end - 1] == '\r')) {
        end--;
    }

    return extract(start, end);
}

const char* SourceTracker::getContextLine() {
    return extractLine(location_.line);
}

void SourceTracker::reset() {
    current_ = source_;
    location_ = SourceLocation(0, 1, 1);
    line_count_ = 0;
    pushLineStart(0);
    line_index_built_ = false;
}

bool SourceTracker::seek(size_t offset) {
    if (offset > source_len_) return false;
    reset();
    return advance(offset);
}

} // namespace lambda
