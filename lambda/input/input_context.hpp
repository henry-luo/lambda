#pragma once
#ifndef LAMBDA_INPUT_CONTEXT_HPP
#define LAMBDA_INPUT_CONTEXT_HPP

#include "parse_error.hpp"
#include "source_tracker.hpp"
#include "../mark_builder.hpp"
#include "../lambda-data.hpp"
#include <memory>

namespace lambda {

// Unified context for parsing with error tracking
// Manages Input, MarkBuilder, error collection, and source position tracking
// Always owns its SourceTracker instance
class InputContext {
private:
    Input* input_;                          // The Input being parsed (not owned)
    MarkBuilder builder_;                   // MarkBuilder for creating Items
    ParseErrorList errors_;                 // Error collection
    std::string owned_source_;              // Owned copy of source - MUST be before tracker_!
    SourceTracker tracker_;                 // Source position tracker (always owned)

public:
    // Constructor without source text (empty tracker)
    explicit InputContext(Input* input)
        : input_(input)
        , builder_(input)
        , errors_(100)  // Default max 100 errors
        , owned_source_()
        , tracker_("", 0)
    {}

    // Constructor with source text and explicit length
    InputContext(Input* input, const char* source, size_t len)
        : input_(input)
        , builder_(input)
        , errors_(100)
        , owned_source_(source, len)  // Make a copy
        , tracker_(owned_source_.c_str(), owned_source_.length())
    {}

    // Constructor with source text from std::string
    InputContext(Input* input, const std::string& source)
        : input_(input)
        , builder_(input)
        , errors_(100)
        , owned_source_(source)  // Make a copy
        , tracker_(owned_source_.c_str(), owned_source_.length())
    {}

    // Constructor with source text from C string (calculates length)
    InputContext(Input* input, const char* source)
        : input_(input)
        , builder_(input)
        , errors_(100)
        , owned_source_(source ? source : "")  // Make a copy
        , tracker_(owned_source_.c_str(), owned_source_.length())
    {}

    // Destructor (no manual cleanup needed - all RAII)
    ~InputContext() = default;

    // Non-copyable (owns resources)
    InputContext(const InputContext&) = delete;
    InputContext& operator=(const InputContext&) = delete;

    // Accessors
    Input* input() { return input_; }
    const Input* input() const { return input_; }
    MarkBuilder& builder() { return builder_; }
    const MarkBuilder& builder() const { return builder_; }
    ParseErrorList& errors() { return errors_; }
    const ParseErrorList& errors() const { return errors_; }
    SourceTracker& tracker() { return tracker_; }
    const SourceTracker& tracker() const { return tracker_; }

    // Check if position tracking is available (always true now, but kept for compatibility)
    bool hasTracker() const { return !owned_source_.empty(); }

    // Get current location
    SourceLocation location() const {
        return tracker_.location();
    }

    // Error handling - with location
    void addError(const SourceLocation& loc, const char* fmt, ...);
    void addError(const SourceLocation& loc, const std::string& message);
    void addError(const SourceLocation& loc, const std::string& message,
                  const std::string& hint);

    void addWarning(const SourceLocation& loc, const char* fmt, ...);
    void addWarning(const SourceLocation& loc, const std::string& message);

    void addNote(const SourceLocation& loc, const char* fmt, ...);
    void addNote(const SourceLocation& loc, const std::string& message);

    // Error handling - at current position (requires tracker)
    void addError(const char* fmt, ...);
    void addError(const std::string& message);

    void addWarning(const char* fmt, ...);
    void addWarning(const std::string& message);

    void addNote(const char* fmt, ...);
    void addNote(const std::string& message);

    // Error state queries
    bool hasErrors() const { return errors_.hasErrors(); }
    bool hasWarnings() const { return errors_.hasWarnings(); }
    size_t errorCount() const { return errors_.errorCount(); }
    size_t warningCount() const { return errors_.warningCount(); }
    bool shouldStopParsing() const { return errors_.shouldStop(); }

    // Format and log all errors
    std::string formatErrors() const { return errors_.formatErrors(); }
    void logErrors() const;

    // Configuration
    void setMaxErrors(size_t max) { errors_.setMaxErrors(max); }
    size_t maxErrors() const { return errors_.maxErrors(); }
};

} // namespace lambda

#endif // LAMBDA_INPUT_CONTEXT_HPP
