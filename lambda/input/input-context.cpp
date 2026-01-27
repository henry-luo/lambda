#include "input-context.hpp"
#include "../../lib/log.h"
#include <cstdarg>
#include <cstdio>

namespace lambda {

void InputContext::addError(const SourceLocation& loc, const char* fmt, ...) {
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
    const char* context_line = nullptr;
    if (owned_source_) {
        context_line = tracker.extractLine(loc.line);
    }

    errors_.addError(ParseError(loc, ParseErrorSeverity::ERROR, message,
                                 context_line, hint));
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
    log_error("%s", formatted);
}

} // namespace lambda
