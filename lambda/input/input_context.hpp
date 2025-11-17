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
// Manages Input, MarkBuilder, error collection, and optional position tracking
class InputContext {
private:
    Input* input_;                          // The Input being parsed (not owned)
    MarkBuilder builder_;                   // MarkBuilder for creating Items
    ParseErrorList errors_;                 // Error collection
    SourceTracker* tracker_;                // Optional position tracker (may be owned)
    bool owns_tracker_;                     // Whether we own the tracker

public:
    // Constructor without position tracking
    explicit InputContext(Input* input)
        : input_(input)
        , builder_(input)
        , errors_(100)  // Default max 100 errors
        , tracker_(nullptr)
        , owns_tracker_(false)
    {}

    // Constructor with position tracking (takes ownership)
    InputContext(Input* input, const char* source, size_t len)
        : input_(input)
        , builder_(input)
        , errors_(100)
        , tracker_(new SourceTracker(source, len))
        , owns_tracker_(true)
    {}

    // Constructor with position tracking from string
    InputContext(Input* input, const std::string& source)
        : InputContext(input, source.c_str(), source.length())
    {}

    // Constructor with external tracker (does not take ownership)
    InputContext(Input* input, SourceTracker* tracker)
        : input_(input)
        , builder_(input)
        , errors_(100)
        , tracker_(tracker)
        , owns_tracker_(false)
    {}

    // Destructor
    ~InputContext() {
        if (owns_tracker_ && tracker_) {
            delete tracker_;
        }
    }

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
    SourceTracker* tracker() { return tracker_; }
    const SourceTracker* tracker() const { return tracker_; }

    // Check if position tracking is available
    bool hasTracker() const { return tracker_ != nullptr; }

    // Get current location (if tracker available)
    SourceLocation location() const {
        return tracker_ ? tracker_->location() : SourceLocation();
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
