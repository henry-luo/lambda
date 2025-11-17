#pragma once
#ifndef LAMBDA_PARSE_ERROR_HPP
#define LAMBDA_PARSE_ERROR_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace lambda {

// Source location tracking - 1-based line and column numbers
struct SourceLocation {
    size_t offset;      // Byte offset in source (0-based)
    size_t line;        // Line number (1-based)
    size_t column;      // Column number (1-based, UTF-8 aware)

    SourceLocation() : offset(0), line(1), column(1) {}
    SourceLocation(size_t off, size_t ln, size_t col)
        : offset(off), line(ln), column(col) {}

    bool isValid() const { return line > 0 && column > 0; }
};

// Error severity levels
enum class ParseErrorSeverity {
    ERROR,      // Fatal parsing error
    WARNING,    // Recoverable issue
    NOTE        // Additional context/information
};

// Individual parse error with location and context
struct ParseError {
    SourceLocation location;
    ParseErrorSeverity severity;
    std::string message;
    std::string context_line;   // Source line where error occurred
    std::string hint;           // Optional hint for fixing the error

    ParseError(const SourceLocation& loc, ParseErrorSeverity sev,
               const std::string& msg)
        : location(loc), severity(sev), message(msg) {}

    ParseError(const SourceLocation& loc, ParseErrorSeverity sev,
               const std::string& msg, const std::string& ctx)
        : location(loc), severity(sev), message(msg), context_line(ctx) {}

    ParseError(const SourceLocation& loc, ParseErrorSeverity sev,
               const std::string& msg, const std::string& ctx,
               const std::string& h)
        : location(loc), severity(sev), message(msg),
          context_line(ctx), hint(h) {}
};

// Collection of parse errors with configurable limit
class ParseErrorList {
private:
    std::vector<ParseError> errors_;
    size_t max_errors_;
    size_t error_count_;      // Count of ERROR severity
    size_t warning_count_;    // Count of WARNING severity

public:
    ParseErrorList(size_t max_errors = 100)
        : max_errors_(max_errors), error_count_(0), warning_count_(0) {}

    // Add an error to the list
    bool addError(const ParseError& error);

    // Convenience methods for adding errors
    void addError(const SourceLocation& loc, const std::string& msg);
    void addError(const SourceLocation& loc, const std::string& msg,
                  const std::string& context);
    void addWarning(const SourceLocation& loc, const std::string& msg);
    void addWarning(const SourceLocation& loc, const std::string& msg,
                    const std::string& context);
    void addNote(const SourceLocation& loc, const std::string& msg);

    // Check if we should stop parsing (hit error limit)
    bool shouldStop() const { return errors_.size() >= max_errors_; }

    // Query error state
    bool hasErrors() const { return error_count_ > 0; }
    bool hasWarnings() const { return warning_count_ > 0; }
    size_t errorCount() const { return error_count_; }
    size_t warningCount() const { return warning_count_; }
    size_t totalCount() const { return errors_.size(); }

    // Access errors
    const std::vector<ParseError>& errors() const { return errors_; }

    // Format errors for display
    std::string formatErrors() const;
    std::string formatError(const ParseError& error, size_t index) const;

    // Configuration
    void setMaxErrors(size_t max) { max_errors_ = max; }
    size_t maxErrors() const { return max_errors_; }

    // Clear all errors
    void clear() {
        errors_.clear();
        error_count_ = 0;
        warning_count_ = 0;
    }
};

} // namespace lambda

#endif // LAMBDA_PARSE_ERROR_HPP
