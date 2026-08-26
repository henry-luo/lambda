#include "input-context.hpp"
#include "../../lib/log.h"
#include <cstdarg>
#include <cstdio>
#include <cstdint>

namespace lambda {

bool InputContext::syncTo(const char* source_pos) {
    if (!source_pos || !source_begin_) return false;
    uintptr_t begin = (uintptr_t)source_begin_;
    uintptr_t current = (uintptr_t)source_pos;
    if (current < begin || current - begin > owned_source_len_) {
        return false;
    }
    return tracker.seek((size_t)(current - begin));
}

void InputContext::addError(const SourceLocation& loc, const char* fmt, ...) {
    markParseError();
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* context_line = nullptr;
    if (owned_source_) {
        context_line = tracker.extractLine(loc.line);
    }

    // Copy message to msg_buf_ for persistence
    strbuf_reset(msg_buf_);
    strbuf_append_str(msg_buf_, buffer);
    const char* msg = msg_buf_->str;

    if (context_line && context_line[0] != '\0') {
        errors_.addError(loc, msg, context_line);
    } else {
        errors_.addError(loc, msg);
    }
}

void InputContext::addError(const SourceLocation& loc, const char* message,
                             const char* hint) {
    markParseError();
    const char* context_line = nullptr;
    if (owned_source_) {
        context_line = tracker.extractLine(loc.line);
    }

    errors_.addError(ParseError(loc, ParseErrorSeverity::ERROR, message,
                                 context_line, hint));
}

void InputContext::addErrorCode(const SourceLocation& loc, const char* code,
                                const char* fmt, ...) {
    markParseError();
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* context_line = owned_source_ ? tracker.extractLine(loc.line) : nullptr;
    errors_.addError(ParseError(loc, ParseErrorSeverity::ERROR, code, buffer,
        context_line && context_line[0] != '\0' ? context_line : nullptr, nullptr));
}

void InputContext::addWarning(const SourceLocation& loc, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* context_line = nullptr;
    if (owned_source_) {
        context_line = tracker.extractLine(loc.line);
    }

    // Copy message to msg_buf_ for persistence
    strbuf_reset(msg_buf_);
    strbuf_append_str(msg_buf_, buffer);
    const char* msg = msg_buf_->str;

    if (context_line && context_line[0] != '\0') {
        errors_.addWarning(loc, msg, context_line);
    } else {
        errors_.addWarning(loc, msg);
    }
}

void InputContext::addNote(const SourceLocation& loc, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Copy message to msg_buf_ for persistence
    strbuf_reset(msg_buf_);
    strbuf_append_str(msg_buf_, buffer);

    errors_.addNote(loc, msg_buf_->str);
}

void InputContext::addError(const char* fmt, ...) {
    markParseError();
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Copy message to msg_buf_ for persistence
    strbuf_reset(msg_buf_);
    strbuf_append_str(msg_buf_, buffer);
    const char* msg = msg_buf_->str;

    if (owned_source_) {
        const char* context_line = tracker.extractLine(tracker.location().line);
        if (context_line && context_line[0] != '\0') {
            errors_.addError(tracker.location(), msg, context_line);
        } else {
            errors_.addError(tracker.location(), msg);
        }
    } else {
        errors_.addError(SourceLocation(0, 1, 1), msg);
    }
}

void InputContext::addWarning(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Copy message to msg_buf_ for persistence
    strbuf_reset(msg_buf_);
    strbuf_append_str(msg_buf_, buffer);
    const char* msg = msg_buf_->str;

    if (owned_source_) {
        const char* context_line = tracker.extractLine(tracker.location().line);
        if (context_line && context_line[0] != '\0') {
            errors_.addWarning(tracker.location(), msg, context_line);
        } else {
            errors_.addWarning(tracker.location(), msg);
        }
    } else {
        errors_.addWarning(SourceLocation(0, 1, 1), msg);
    }
}

void InputContext::addNote(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Copy message to msg_buf_ for persistence
    strbuf_reset(msg_buf_);
    strbuf_append_str(msg_buf_, buffer);

    if (owned_source_) {
        errors_.addNote(tracker.location(), msg_buf_->str);
    } else {
        errors_.addNote(SourceLocation(0, 1, 1), msg_buf_->str);
    }
}

void InputContext::logErrors() const {
    if (errors_.totalCount() == 0) {
        return;
    }

    const char* formatted = const_cast<ParseErrorList&>(errors_).formatErrors();
    if (input_ && errors_.hasErrors() && !input_->parse_error_message &&
            formatted && input_->pool) {
        size_t message_len = strlen(formatted);
        char* message_copy = (char*)pool_alloc(input_->pool, message_len + 1);
        if (message_copy) {
            memcpy(message_copy, formatted, message_len + 1);
            input_->parse_error_message = message_copy;
        }
    }
    log_error("%s", formatted ? formatted : "parser reported an unspecified error");
}

} // namespace lambda
