#include "parse_error.hpp"
#include <sstream>
#include <iomanip>

namespace lambda {

bool ParseErrorList::addError(const ParseError& error) {
    // Check if we've hit the limit
    if (errors_.size() >= max_errors_) {
        return false;
    }

    errors_.push_back(error);

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

void ParseErrorList::addError(const SourceLocation& loc, const std::string& msg) {
    addError(ParseError(loc, ParseErrorSeverity::ERROR, msg));
}

void ParseErrorList::addError(const SourceLocation& loc, const std::string& msg,
                               const std::string& context) {
    addError(ParseError(loc, ParseErrorSeverity::ERROR, msg, context));
}

void ParseErrorList::addWarning(const SourceLocation& loc, const std::string& msg) {
    addError(ParseError(loc, ParseErrorSeverity::WARNING, msg));
}

void ParseErrorList::addWarning(const SourceLocation& loc, const std::string& msg,
                                 const std::string& context) {
    addError(ParseError(loc, ParseErrorSeverity::WARNING, msg, context));
}

void ParseErrorList::addNote(const SourceLocation& loc, const std::string& msg) {
    addError(ParseError(loc, ParseErrorSeverity::NOTE, msg));
}

std::string ParseErrorList::formatError(const ParseError& error, size_t index) const {
    std::ostringstream oss;

    // Severity label
    const char* severity_str = "";
    switch (error.severity) {
        case ParseErrorSeverity::ERROR:   severity_str = "error"; break;
        case ParseErrorSeverity::WARNING: severity_str = "warning"; break;
        case ParseErrorSeverity::NOTE:    severity_str = "note"; break;
    }

    // Format: "line:column: severity: message"
    oss << "line " << error.location.line << ", col " << error.location.column
        << ": " << severity_str << ": " << error.message << "\n";

    // Show context line if available
    if (!error.context_line.empty()) {
        oss << "  " << error.context_line << "\n";

        // Add caret pointer to error location
        if (error.location.column > 0 && error.location.column <= error.context_line.length() + 1) {
            oss << "  ";
            for (size_t i = 1; i < error.location.column; ++i) {
                oss << " ";
            }
            oss << "^\n";
        }
    }

    // Show hint if available
    if (!error.hint.empty()) {
        oss << "  hint: " << error.hint << "\n";
    }

    return oss.str();
}

std::string ParseErrorList::formatErrors() const {
    if (errors_.empty()) {
        return "";
    }

    std::ostringstream oss;

    // Summary header
    oss << "Parse errors (" << error_count_ << " error";
    if (error_count_ != 1) oss << "s";
    if (warning_count_ > 0) {
        oss << ", " << warning_count_ << " warning";
        if (warning_count_ != 1) oss << "s";
    }
    oss << "):\n\n";

    // Format each error
    for (size_t i = 0; i < errors_.size(); ++i) {
        oss << formatError(errors_[i], i + 1);
        if (i < errors_.size() - 1) {
            oss << "\n";
        }
    }

    // If we hit the limit, add a note
    if (errors_.size() >= max_errors_) {
        oss << "\n(error limit of " << max_errors_ << " reached, stopping)\n";
    }

    return oss.str();
}

} // namespace lambda
