#pragma once
#ifndef LAMBDA_INPUT_CONTEXT_HPP
#define LAMBDA_INPUT_CONTEXT_HPP

#include "parse_error.hpp"
#include "source_tracker.hpp"
#include "../mark_builder.hpp"
#include "../lambda-data.hpp"
#include "../../lib/strbuf.h"
#include <cstdlib>
#include <cstring>

namespace lambda {

// Unified context for parsing with error tracking
// Manages Input, MarkBuilder, error collection, and source position tracking
// Always owns its SourceTracker instance
class InputContext {
private:
    Input* input_;                   // The Input being parsed (not owned)
    ParseErrorList errors_;          // Error collection
    char* owned_source_;             // Owned copy of source (malloc'd)
    size_t owned_source_len_;        // Length of owned source
    StrBuf* msg_buf_;                // Buffer for formatting error messages
public:
    MarkBuilder builder;             // MarkBuilder for creating Items
    SourceTracker tracker;           // Source position tracker (always owned)
    StringBuf* sb;                   // shared string buffer for temp work

    // Constructor without source text (empty tracker)
    explicit InputContext(Input* input)
        : input_(input)
        , builder(input)
        , errors_(100)  // Default max 100 errors
        , owned_source_(nullptr)
        , owned_source_len_(0)
        , msg_buf_(strbuf_new_cap(256))
        , tracker("", 0)
        , sb(stringbuf_new(input->pool))
    {}

    // Constructor with source text and explicit length
    InputContext(Input* input, const char* source, size_t len)
        : input_(input)
        , builder(input)
        , errors_(100)
        , owned_source_((char*)malloc(len + 1))
        , owned_source_len_(len)
        , msg_buf_(strbuf_new_cap(256))
        , tracker(owned_source_, len)
        , sb(stringbuf_new(input->pool))
    {
        if (owned_source_ && source) {
            memcpy(owned_source_, source, len);
            owned_source_[len] = '\0';
        }
    }

    // Constructor with source text from C string (calculates length)
    InputContext(Input* input, const char* source)
        : InputContext(input, source, source ? strlen(source) : 0)
    {}

    // Destructor
    ~InputContext() {
        if (owned_source_) free(owned_source_);
        strbuf_free(msg_buf_);
    }

    // Non-copyable (owns resources)
    InputContext(const InputContext&) = delete;
    InputContext& operator=(const InputContext&) = delete;

    // Accessors
    Input* input() { return input_; }
    const Input* input() const { return input_; }
    ParseErrorList& errors() { return errors_; }
    const ParseErrorList& errors() const { return errors_; }


    // Get current location
    SourceLocation location() const {
        return tracker.location();
    }

    // Error handling - with location
    void addError(const SourceLocation& loc, const char* fmt, ...);
    void addError(const SourceLocation& loc, const char* message,
                  const char* hint);

    void addWarning(const SourceLocation& loc, const char* fmt, ...);

    void addNote(const SourceLocation& loc, const char* fmt, ...);

    // Error handling - at current position (requires tracker)
    void addError(const char* fmt, ...);

    void addWarning(const char* fmt, ...);

    void addNote(const char* fmt, ...);

    // Error state queries
    bool hasErrors() const { return errors_.hasErrors(); }
    bool hasWarnings() const { return errors_.hasWarnings(); }
    size_t errorCount() const { return errors_.errorCount(); }
    size_t warningCount() const { return errors_.warningCount(); }
    bool shouldStopParsing() const { return errors_.shouldStop(); }

    // Format and log all errors
    const char* formatErrors() { return errors_.formatErrors(); }
    void logErrors() const;

    // Configuration
    void setMaxErrors(size_t max) { errors_.setMaxErrors(max); }
    size_t maxErrors() const { return errors_.maxErrors(); }
};

} // namespace lambda

// Common utility functions
bool is_greek_letter(const char* cmd_name);
bool is_math_operator(const char* cmd_name);
bool is_trig_function(const char* cmd_name);
bool is_log_function(const char* cmd_name);
bool is_latex_command(const char* cmd_name);
bool is_latex_environment(const char* env_name);
bool is_math_environment(const char* env_name);
bool is_raw_text_environment(const char* env_name);

#endif // LAMBDA_INPUT_CONTEXT_HPP
