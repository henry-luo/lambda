#include "parse_error.hpp"
#include <cstdio>
#include <cstring>

namespace lambda {

ParseErrorList::ParseErrorList(size_t max_errors)
    : errors_(arraylist_new(16))
    , format_buf_(strbuf_new_cap(1024))
    , max_errors_(max_errors)
    , error_count_(0)
    , warning_count_(0)
{
}

ParseErrorList::~ParseErrorList() {
    // Free ParseError copies stored in the list
    for (int i = 0; i < errors_->length; i++) {
        ParseError* err = (ParseError*)errors_->data[i];
        // Free the duplicated strings
        if (err->message) free((void*)err->message);
        if (err->context_line) free((void*)err->context_line);
        if (err->hint) free((void*)err->hint);
        free(err);
    }
    arraylist_free(errors_);
    strbuf_free(format_buf_);
}

bool ParseErrorList::addError(const ParseError& error) {
    // Check if we've hit the limit
    if ((size_t)errors_->length >= max_errors_) {
        return false;
    }

    // Allocate a copy of the error
    ParseError* err_copy = (ParseError*)malloc(sizeof(ParseError));
    *err_copy = error;

    // Deep copy the message string since it may be in a reused buffer
    if (error.message) {
        err_copy->message = strdup(error.message);
    }
    // Deep copy context_line if present
    if (error.context_line) {
        err_copy->context_line = strdup(error.context_line);
    }
    // Deep copy hint if present
    if (error.hint) {
        err_copy->hint = strdup(error.hint);
    }

    arraylist_append(errors_, err_copy);

    // Update counters
    switch (error.severity) {
        case ParseErrorSeverity::ERROR:
            error_count_++;
            break;
        case ParseErrorSeverity::WARNING:
            warning_count_++;
            break;
        case ParseErrorSeverity::NOTE:
            // Notes don't count toward limits
            break;
    }

    return true;
}

void ParseErrorList::addError(const SourceLocation& loc, const char* msg) {
    addError(ParseError(loc, ParseErrorSeverity::ERROR, msg));
}

void ParseErrorList::addError(const SourceLocation& loc, const char* msg,
                               const char* context) {
    addError(ParseError(loc, ParseErrorSeverity::ERROR, msg, context));
}

void ParseErrorList::addWarning(const SourceLocation& loc, const char* msg) {
    addError(ParseError(loc, ParseErrorSeverity::WARNING, msg));
}

void ParseErrorList::addWarning(const SourceLocation& loc, const char* msg,
                                 const char* context) {
    addError(ParseError(loc, ParseErrorSeverity::WARNING, msg, context));
}

void ParseErrorList::addNote(const SourceLocation& loc, const char* msg) {
    addError(ParseError(loc, ParseErrorSeverity::NOTE, msg));
}

ParseError* ParseErrorList::getError(size_t index) const {
    if (index >= (size_t)errors_->length) return nullptr;
    return (ParseError*)errors_->data[index];
}

void ParseErrorList::formatError(const ParseError& error, size_t index, StrBuf* buf) const {
    // Severity label
    const char* severity_str = "";
    switch (error.severity) {
        case ParseErrorSeverity::ERROR:   severity_str = "error"; break;
        case ParseErrorSeverity::WARNING: severity_str = "warning"; break;
        case ParseErrorSeverity::NOTE:    severity_str = "note"; break;
    }

    // Format: "line:column: severity: message"
    strbuf_append_format(buf, "line %zu, col %zu: %s: %s\n",
                   error.location.line, error.location.column,
                   severity_str, error.message ? error.message : "(null)");

    // Show context line if available
    if (error.context_line && error.context_line[0] != '\0') {
        strbuf_append_format(buf, "  %s\n", error.context_line);

        // Add caret pointer to error location
        size_t ctx_len = strlen(error.context_line);
        if (error.location.column > 0 && error.location.column <= ctx_len + 1) {
            strbuf_append_str(buf, "  ");
            for (size_t i = 1; i < error.location.column; ++i) {
                strbuf_append_char(buf, ' ');
            }
            strbuf_append_str(buf, "^\n");
        }
    }

    // Show hint if available
    if (error.hint && error.hint[0] != '\0') {
        strbuf_append_format(buf, "  hint: %s\n", error.hint);
    }
}

const char* ParseErrorList::formatErrors() {
    if (errors_->length == 0) {
        return "";
    }

    strbuf_reset(format_buf_);

    // Summary header
    strbuf_append_format(format_buf_, "Parse errors (%zu error%s",
                   error_count_, error_count_ != 1 ? "s" : "");
    if (warning_count_ > 0) {
        strbuf_append_format(format_buf_, ", %zu warning%s",
                       warning_count_, warning_count_ != 1 ? "s" : "");
    }
    strbuf_append_str(format_buf_, "):\n\n");

    // Format each error
    size_t count = (size_t)errors_->length;
    for (size_t i = 0; i < count; ++i) {
        ParseError* err = (ParseError*)errors_->data[i];
        formatError(*err, i + 1, format_buf_);
        if (i < count - 1) {
            strbuf_append_str(format_buf_, "\n");
        }
    }

    // If we hit the limit, add a note
    if (count >= max_errors_) {
        strbuf_append_format(format_buf_, "\n(error limit of %zu reached, stopping)\n", max_errors_);
    }

    return format_buf_->str;
}

void ParseErrorList::clear() {
    // Free ParseError copies and their duplicated strings
    for (int i = 0; i < errors_->length; i++) {
        ParseError* err = (ParseError*)errors_->data[i];
        if (err->message) free((void*)err->message);
        if (err->context_line) free((void*)err->context_line);
        if (err->hint) free((void*)err->hint);
        free(err);
    }
    arraylist_clear(errors_);
    error_count_ = 0;
    warning_count_ = 0;
}

} // namespace lambda
