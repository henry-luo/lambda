#include "input_context.hpp"
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

    std::string message(buffer);
    std::string context_line;

    if (tracker_) {
        context_line = tracker_->extractLine(loc.line);
    }

    if (context_line.empty()) {
        errors_.addError(loc, message);
    } else {
        errors_.addError(loc, message, context_line);
    }
}

void InputContext::addError(const SourceLocation& loc, const std::string& message) {
    std::string context_line;

    if (tracker_) {
        context_line = tracker_->extractLine(loc.line);
    }

    if (context_line.empty()) {
        errors_.addError(loc, message);
    } else {
        errors_.addError(loc, message, context_line);
    }
}

void InputContext::addError(const SourceLocation& loc, const std::string& message,
                             const std::string& hint) {
    std::string context_line;

    if (tracker_) {
        context_line = tracker_->extractLine(loc.line);
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

    addWarning(loc, std::string(buffer));
}

void InputContext::addWarning(const SourceLocation& loc, const std::string& message) {
    std::string context_line;

    if (tracker_) {
        context_line = tracker_->extractLine(loc.line);
    }

    if (context_line.empty()) {
        errors_.addWarning(loc, message);
    } else {
        errors_.addWarning(loc, message, context_line);
    }
}

void InputContext::addNote(const SourceLocation& loc, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    addNote(loc, std::string(buffer));
}

void InputContext::addNote(const SourceLocation& loc, const std::string& message) {
    errors_.addNote(loc, message);
}

void InputContext::addError(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (tracker_) {
        addError(tracker_->location(), std::string(buffer));
    } else {
        // No tracker, add error without location
        errors_.addError(SourceLocation(0, 1, 1), std::string(buffer));
    }
}

void InputContext::addError(const std::string& message) {
    if (tracker_) {
        addError(tracker_->location(), message);
    } else {
        errors_.addError(SourceLocation(0, 1, 1), message);
    }
}

void InputContext::addWarning(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (tracker_) {
        addWarning(tracker_->location(), std::string(buffer));
    } else {
        errors_.addWarning(SourceLocation(0, 1, 1), std::string(buffer));
    }
}

void InputContext::addWarning(const std::string& message) {
    if (tracker_) {
        addWarning(tracker_->location(), message);
    } else {
        errors_.addWarning(SourceLocation(0, 1, 1), message);
    }
}

void InputContext::addNote(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (tracker_) {
        addNote(tracker_->location(), std::string(buffer));
    } else {
        errors_.addNote(SourceLocation(0, 1, 1), std::string(buffer));
    }
}

void InputContext::addNote(const std::string& message) {
    if (tracker_) {
        addNote(tracker_->location(), message);
    } else {
        errors_.addNote(SourceLocation(0, 1, 1), message);
    }
}

void InputContext::logErrors() const {
    if (errors_.totalCount() == 0) {
        return;
    }

    std::string formatted = errors_.formatErrors();
    log_error("%s", formatted.c_str());
}

} // namespace lambda
