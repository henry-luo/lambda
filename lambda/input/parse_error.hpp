#pragma once
#ifndef LAMBDA_PARSE_ERROR_HPP
#define LAMBDA_PARSE_ERROR_HPP

#include "../../lib/arraylist.h"
#include "../../lib/strbuf.h"
#include <cstdint>
#include <cstddef>

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
    const char* message;        // Arena-allocated or static
    const char* context_line;   // Source line where error occurred (may be null)
    const char* hint;           // Optional hint for fixing the error (may be null)

    ParseError() : location(), severity(ParseErrorSeverity::ERROR),
                   message(nullptr), context_line(nullptr), hint(nullptr) {}

    ParseError(const SourceLocation& loc, ParseErrorSeverity sev,
               const char* msg)
        : location(loc), severity(sev), message(msg),
          context_line(nullptr), hint(nullptr) {}

    ParseError(const SourceLocation& loc, ParseErrorSeverity sev,
               const char* msg, const char* ctx)
        : location(loc), severity(sev), message(msg),
          context_line(ctx), hint(nullptr) {}

    ParseError(const SourceLocation& loc, ParseErrorSeverity sev,
               const char* msg, const char* ctx,
               const char* h)
        : location(loc), severity(sev), message(msg),
          context_line(ctx), hint(h) {}
};

// Collection of parse errors with configurable limit
class ParseErrorList {
private:
    ArrayList* errors_;         // ArrayList of ParseError
    StrBuf* format_buf_;        // Reusable buffer for formatting
    size_t max_errors_;
    size_t error_count_;        // Count of ERROR severity
    size_t warning_count_;      // Count of WARNING severity

public:
    ParseErrorList(size_t max_errors = 100);
    ~ParseErrorList();

    // Non-copyable
    ParseErrorList(const ParseErrorList&) = delete;
    ParseErrorList& operator=(const ParseErrorList&) = delete;

    // Add an error to the list (message should be arena-allocated or static)
    bool addError(const ParseError& error);

    // Convenience methods for adding errors
    void addError(const SourceLocation& loc, const char* msg);
    void addError(const SourceLocation& loc, const char* msg,
                  const char* context);
    void addWarning(const SourceLocation& loc, const char* msg);
    void addWarning(const SourceLocation& loc, const char* msg,
                    const char* context);
    void addNote(const SourceLocation& loc, const char* msg);

    // Check if we should stop parsing (hit error limit)
    bool shouldStop() const { return (size_t)errors_->length >= max_errors_; }

    // Query error state
    bool hasErrors() const { return error_count_ > 0; }
    bool hasWarnings() const { return warning_count_ > 0; }
    size_t errorCount() const { return error_count_; }
    size_t warningCount() const { return warning_count_; }
    size_t totalCount() const { return (size_t)errors_->length; }

    // Access errors
    ParseError* getError(size_t index) const;
    size_t size() const { return (size_t)errors_->length; }

    // Format errors for display (returns internal buffer - do not free)
    const char* formatErrors();
    void formatError(const ParseError& error, size_t index, StrBuf* buf) const;

    // Configuration
    void setMaxErrors(size_t max) { max_errors_ = max; }
    size_t maxErrors() const { return max_errors_; }

    // Clear all errors
    void clear();
};

} // namespace lambda

#endif // LAMBDA_PARSE_ERROR_HPP
