#include "source_tracker.hpp"
#include <cstring>
#include <cctype>

namespace lambda {

SourceTracker::SourceTracker(const char* source, size_t len)
    : source_(source)
    , source_len_(len)
    , current_(source)
    , location_(0, 1, 1)
    , line_count_(1)
    , line_index_built_(false)
    , extract_buf_(strbuf_new_cap(256))
{
    line_starts_[0] = 0;  // Line 1 starts at offset 0
}

SourceTracker::~SourceTracker() {
    strbuf_free(extract_buf_);
}

void SourceTracker::buildLineIndex() {
    if (line_index_built_) return;

    line_count_ = 1;
    line_starts_[0] = 0;

    for (size_t i = 0; i < source_len_ && line_count_ < MAX_LINE_STARTS; ++i) {
        if (source_[i] == '\n') {
            line_starts_[line_count_++] = i + 1;
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
            if (!line_index_built_ && line_count_ < MAX_LINE_STARTS) {
                line_starts_[line_count_++] = location_.offset;
            }
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
    while (!atEnd() && std::isspace((unsigned char)*current_)) {
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
    line_count_ = 1;
    line_starts_[0] = 0;
    line_index_built_ = false;
}

} // namespace lambda
