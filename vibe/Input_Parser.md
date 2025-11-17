# Lambda Input Parser Refactoring Plan

## Executive Summary

This document proposes a comprehensive refactoring of Lambda's input parsing system to improve consistency, reduce code duplication, modernize the codebase, and establish clear architectural patterns. The refactoring will introduce a unified `InputContext` abstraction, complete migration to `MarkBuilder` API, elimination of legacy C functions, and conversion of all parsers to modern C++.

**Estimated Impact**: ~40 parser files, ~25,000 lines of code
**Priority**: High - Foundational improvement enabling future development
**Timeline**: 3-4 weeks for full implementation

---

## Current State Analysis

### Architecture Overview

Lambda's input parsing system consists of:
- **40+ format parsers**: JSON, XML, HTML, YAML, Markdown, LaTeX, PDF, CSS, etc.
- **Hybrid C/C++ codebase**: Mix of C and C++ implementations
- **Input struct**: Central context passed to all parsers
- **Memory management**: Pool (variable-size) + Arena (bump-pointer) allocators
- **MarkBuilder**: Modern C++ fluent API (recently introduced, partially adopted)

### Input Struct (Current)

```cpp
typedef struct Input {
    Pool* pool;              // variable-size memory pool
    Arena* arena;            // bump-pointer arena allocator
    NamePool* name_pool;     // string interning pool
    ArrayList* type_list;    // type registry
    StringBuf* sb;           // shared temp string buffer
    Url* url;                // source URL/location
    Item root;               // parsed result root node
} Input;
```

### Key Pain Points

#### 1. **Inconsistent Parameter Passing**
- Some parsers: `parse_json(Input* input, const char* json)`
- Others: `parse_xml(Input* input, MarkBuilder* builder, const char** xml)`
- Still others: `parse_yaml(Input* input, MarkBuilder& builder, ...)`
- Creates confusion, hard to maintain consistent patterns

#### 2. **Mixed Memory Management Patterns**

**Legacy Pattern** (60% of parsers):
```cpp
Map* map = map_pooled(input->pool);
String* str = (String*)pool_calloc(input->pool, sizeof(String) + len);
Array* arr = array_pooled(input->pool);
array_append(arr, item, input->pool);
```

**Modern Pattern** (40% of parsers):
```cpp
MarkBuilder builder(input);
Item result = builder.map()
    .put("key", builder.createString("value"))
    .final();
```

Mixed usage leads to:
- Code duplication
- Inconsistent error handling
- Harder to understand which allocator is used where
- Difficult to optimize globally

#### 3. **Exposed Implementation Details**
```cpp
// Parsers directly access Input internals
input->pool    // should be abstracted
input->arena   // should be abstracted
input->root = result;  // direct assignment
```

- Tight coupling to memory allocator implementation
- No encapsulation boundary
- Hard to refactor internal memory management

#### 4. **Legacy C API Functions**

**Still in use across 30+ parsers**:
```cpp
// From input.hpp (should be deprecated)
void map_put(Map* mp, String* key, Item value, Input* input);
Element* input_create_element(Input* input, const char* tag_name);
void input_add_attribute_to_element(Input* input, Element* element,
                                    const char* attr_name, const char* attr_value);
Input* input_new(Url* abs_url);  // C-style constructor

// Utility functions still in C
char** input_split_lines(const char* text, int* line_count);
void input_free_lines(char** lines, int line_count);
bool input_is_whitespace_char(char c);
```

Problems:
- Verbose API (requires passing `input` or `pool` to every call)
- No RAII cleanup
- Inconsistent error handling (NULL returns vs error codes)
- Can't use C++ features (move semantics, references, templates)

#### 5. **Duplicate String/Whitespace Utilities**
```cpp
// Duplicated across 15+ parsers:
static void skip_whitespace(const char** text) { ... }
static bool is_whitespace(char c) { ... }
static char* trim_string(const char* str) { ... }
static String* create_string_from_cstr(Input* input, const char* str) { ... }
```

Estimated **~500 lines of duplicated code** across parsers.

#### 6. **Inconsistent Error Handling**
```cpp
// Parser A:
if (error) {
    input->root = {.item = ITEM_ERROR};
    return;
}

// Parser B:
if (error) {
    return NULL_ITEM;
}

// Parser C:
if (error) {
    log_error("Parse failed");
    return {.item = ITEM_NULL};
}
```

No consistent error reporting, debugging is difficult.

#### 7. **No Parser Context/State Management**
```cpp
// Large parsers manually track state
typedef struct MarkupParser {
    Input* input;
    MarkBuilder* builder;
    const char* content;
    size_t pos;
    size_t length;
    MarkupFormat format;
    // ... 10+ more fields
} MarkupParser;
```

- Every complex parser reinvents context management
- No shared abstraction for common parser state
- Boilerplate repeated across parsers

#### 8. **Inadequate Error Reporting**
```cpp
// Current error handling patterns:

// Pattern A: Single error flag
if (error) {
    input->root = {.item = ITEM_ERROR};
    return;
}

// Pattern B: Log and return
if (error) {
    log_error("Parse failed");
    return NULL_ITEM;
}

// Pattern C: Silent failure
if (error) {
    return {.item = ITEM_NULL};
}
```

**Critical Issues**:
- ❌ **No location tracking**: Errors don't report line/column numbers
- ❌ **Single error only**: Parser stops at first error, can't collect multiple errors
- ❌ **No error recovery**: Can't continue parsing after errors to find more issues
- ❌ **Poor debugging experience**: Users get minimal information about what went wrong
- ❌ **Inconsistent across parsers**: Each parser handles errors differently

**Real-world Impact**:
- User gets "Parse failed" with no indication where in a 5000-line YAML file the problem is
- Fixing one syntax error requires re-parsing to find the next error (slow iteration)
- No way to get a comprehensive list of all problems in a document
- Hard to build IDE features (error squiggles, quick fixes) without location data

---

## Proposed Solution

### Phase 1: InputContext Abstraction with Advanced Error Tracking

#### 1.1 Error Tracking System

**File**: `lambda/input/parse_error.hpp`

```cpp
#ifndef LAMBDA_PARSE_ERROR_HPP
#define LAMBDA_PARSE_ERROR_HPP

#include <vector>
#include <string>

/**
 * SourceLocation - Tracks position in source document
 */
struct SourceLocation {
    size_t offset;      // byte offset from start
    int line;           // 1-based line number
    int column;         // 1-based column number (UTF-8 aware)

    SourceLocation() : offset(0), line(1), column(1) {}
    SourceLocation(size_t off, int ln, int col)
        : offset(off), line(ln), column(col) {}

    bool isValid() const { return line > 0; }
};

/**
 * ParseErrorSeverity - Error severity levels
 */
enum class ParseErrorSeverity {
    ERROR,      // parse error that prevents correct interpretation
    WARNING,    // recoverable issue that might cause problems
    NOTE        // informational message (e.g., "did you mean?")
};

/**
 * ParseError - Single parse error with location and context
 */
struct ParseError {
    ParseErrorSeverity severity;
    SourceLocation location;
    std::string message;
    std::string context_line;    // line of text where error occurred
    std::string hint;            // optional suggestion for fixing

    ParseError(ParseErrorSeverity sev, SourceLocation loc,
               const std::string& msg)
        : severity(sev), location(loc), message(msg) {}

    ParseError(ParseErrorSeverity sev, SourceLocation loc,
               const std::string& msg, const std::string& hint_text)
        : severity(sev), location(loc), message(msg), hint(hint_text) {}
};

/**
 * ParseErrorList - Collection of parse errors with management
 */
class ParseErrorList {
private:
    std::vector<ParseError> errors_;
    size_t max_errors_;
    size_t error_count_;      // total errors (including severity filter)
    size_t warning_count_;    // total warnings

public:
    ParseErrorList(size_t max_errors = 100)
        : max_errors_(max_errors)
        , error_count_(0)
        , warning_count_(0) {}

    /**
     * Add an error to the list
     * Returns true if error was added, false if max_errors reached
     */
    bool addError(const ParseError& error);
    bool addError(SourceLocation loc, const std::string& message);
    bool addError(SourceLocation loc, const std::string& message,
                  const std::string& hint);

    /**
     * Add a warning
     */
    bool addWarning(SourceLocation loc, const std::string& message);
    bool addWarning(SourceLocation loc, const std::string& message,
                    const std::string& hint);

    /**
     * Add a note/info message
     */
    bool addNote(SourceLocation loc, const std::string& message);

    /**
     * Check if max errors reached (should stop parsing)
     */
    bool shouldStop() const {
        return error_count_ >= max_errors_;
    }

    /**
     * Query error state
     */
    bool hasErrors() const { return error_count_ > 0; }
    bool hasWarnings() const { return warning_count_ > 0; }
    bool empty() const { return errors_.empty(); }
    size_t errorCount() const { return error_count_; }
    size_t warningCount() const { return warning_count_; }
    size_t totalCount() const { return errors_.size(); }

    /**
     * Access errors
     */
    const std::vector<ParseError>& errors() const { return errors_; }
    const ParseError& operator[](size_t index) const { return errors_[index]; }

    /**
     * Format errors for display
     */
    std::string formatErrors() const;
    std::string formatError(const ParseError& err, bool with_context = true) const;

    /**
     * Clear all errors
     */
    void clear() {
        errors_.clear();
        error_count_ = 0;
        warning_count_ = 0;
    }

    /**
     * Set max errors limit
     */
    void setMaxErrors(size_t max) { max_errors_ = max; }
    size_t maxErrors() const { return max_errors_; }
};

#endif // LAMBDA_PARSE_ERROR_HPP
```

**File**: `lambda/input/source_tracker.hpp`

```cpp
#ifndef LAMBDA_SOURCE_TRACKER_HPP
#define LAMBDA_SOURCE_TRACKER_HPP

#include "parse_error.hpp"
#include <cstddef>

/**
 * SourceTracker - Tracks position in source text for error reporting
 *
 * FEATURES:
 * - Maintains line/column numbers as parsing progresses
 * - UTF-8 aware column counting
 * - Efficient: O(1) position updates
 * - Supports backtracking (for lookahead/recovery)
 *
 * USAGE:
 *   SourceTracker tracker(content);
 *   while (!tracker.atEnd()) {
 *       char c = tracker.advance();
 *       if (error) {
 *           ctx.addError(tracker.location(), "Error message");
 *       }
 *   }
 */
class SourceTracker {
private:
    const char* content_;      // source text
    size_t length_;            // total length
    size_t offset_;            // current byte offset
    int line_;                 // current line (1-based)
    int column_;               // current column (1-based, UTF-8 aware)

    // line start offsets for context extraction
    std::vector<size_t> line_starts_;

public:
    /**
     * Initialize tracker with source content
     */
    explicit SourceTracker(const char* content);
    SourceTracker(const char* content, size_t length);

    /**
     * Get current location
     */
    SourceLocation location() const {
        return SourceLocation(offset_, line_, column_);
    }

    /**
     * Position queries
     */
    size_t offset() const { return offset_; }
    int line() const { return line_; }
    int column() const { return column_; }
    bool atEnd() const { return offset_ >= length_; }
    size_t remaining() const { return length_ - offset_; }

    /**
     * Character access
     */
    char peek() const { return atEnd() ? '\0' : content_[offset_]; }
    char peek(size_t ahead) const {
        return (offset_ + ahead >= length_) ? '\0' : content_[offset_ + ahead];
    }

    /**
     * Advance position
     */
    char advance();                    // advance by 1 char, return it
    void skip(size_t n);              // skip n bytes
    bool match(const char* str);      // match and advance if successful

    /**
     * Position management
     */
    SourceLocation mark() const { return location(); }
    void restore(const SourceLocation& loc);

    /**
     * Get context for error messages
     */
    const char* current() const { return content_ + offset_; }
    std::string getLine(int line_num) const;
    std::string getContextLine(const SourceLocation& loc) const;

    /**
     * Utility
     */
    void skipWhitespace();
    void skipToEndOfLine();
    bool startsWith(const char* prefix) const;
};

#endif // LAMBDA_SOURCE_TRACKER_HPP
```

#### 1.2 Enhanced InputContext Class

**File**: `lambda/input/input_context.hpp`

```cpp
#ifndef LAMBDA_INPUT_CONTEXT_HPP
#define LAMBDA_INPUT_CONTEXT_HPP

#include "../lambda-data.hpp"
#include "../mark_builder.hpp"
#include "parse_error.hpp"
#include "source_tracker.hpp"
#include "../../lib/url.h"
#include "../../lib/log.h"

/**
 * InputContext - Unified context for all input parsers
 *
 * PURPOSE:
 * - Encapsulate memory management (pool, arena, string interning)
 * - Provide fluent API via MarkBuilder integration
 * - Centralize error handling with location tracking
 * - Support error recovery and multiple error collection
 * - Abstract away Input struct internals
 *
 * ERROR HANDLING:
 * - Tracks multiple errors with line/column locations
 * - Supports error recovery (parser can continue after errors)
 * - Configurable error limit (default 100)
 * - Severity levels: ERROR, WARNING, NOTE
 *
 * MEMORY MODEL:
 * - Stack-allocated in parser entry functions
 * - RAII cleanup (automatic resource management)
 * - Wraps Input struct, MarkBuilder, and error tracking together
 *
 * USAGE:
 *   void parse_json(Input* input, const char* json) {
 *       InputContext ctx(input, json);
 *
 *       while (!ctx.tracker().atEnd()) {
 *           if (parse_error) {
 *               ctx.addError(ctx.location(), "Unexpected token");
 *               // Try to recover...
 *               if (ctx.shouldStopParsing()) break;
 *           }
 *       }
 *
 *       if (ctx.hasErrors()) {
 *           ctx.logAllErrors();
 *           ctx.setErrorResult();
 *       } else {
 *           ctx.setRoot(result);
 *       }
 *   }
 */
class InputContext {
private:
    Input* input_;              // underlying Input struct
    MarkBuilder builder_;       // fluent builder API
    ParseErrorList errors_;     // collected parse errors
    SourceTracker* tracker_;    // position tracking (optional)public:
    // ============================================================================
    // Construction & Lifecycle
    // ============================================================================

    /**
     * Construct InputContext from Input
     * Stack-allocated, RAII cleanup
     */
    explicit InputContext(Input* input);

    /**
     * Destructor - automatic cleanup
     */
    ~InputContext();

    // Non-copyable (prevent accidental copies)
    InputContext(const InputContext&) = delete;
    InputContext& operator=(const InputContext&) = delete;

    // Movable (for return value optimization)
    InputContext(InputContext&&) = default;
    InputContext& operator=(InputContext&&) = default;

    // ============================================================================
    // MarkBuilder Integration (Primary API)
    // ============================================================================

    /**
     * Get MarkBuilder for fluent construction
     * Preferred API for modern parsers
     */
    MarkBuilder& builder() { return builder_; }

    /**
     * Create string (delegates to builder)
     */
    String* createString(const char* str);
    String* createString(const char* str, size_t len);

    /**
     * Create element (delegates to builder)
     */
    ElementBuilder element(const char* tag_name);

    /**
     * Create map (delegates to builder)
     */
    MapBuilder map();

    /**
     * Create array (delegates to builder)
     */
    ArrayBuilder array();

    // ============================================================================
    // Parser Utilities (Centralized common operations)
    // ============================================================================

    /**
     * String manipulation utilities
     */
    char* trimWhitespace(const char* str);
    char* duplicateString(const char* str);
    char* duplicateString(const char* str, size_t len);

    /**
     * Line splitting for line-oriented parsers
     */
    char** splitLines(const char* text, int* line_count);
    void freeLines(char** lines, int line_count);

    /**
     * Whitespace utilities
     */
    static bool isWhitespace(char c);
    static bool isWhitespaceOnly(const char* str);
    static void skipWhitespace(const char** text);
    static int countLeadingSpaces(const char* line);
    static int countLeadingChar(const char* line, char ch);

    /**
     * String helpers
     */
    static bool startsWith(const char* str, const char* prefix);
    static bool endsWith(const char* str, const char* suffix);
    static const char* findFirst(const char* str, const char* chars);

    // ============================================================================
    // Result Management
    // ============================================================================

    /**
     * Set parse result (writes to input->root)
     */
    void setRoot(Item result);

    /**
     * Get current root (reads input->root)
     */
    Item getRoot() const;

    // ============================================================================
    // Source Position Tracking
    // ============================================================================

    /**
     * Get source tracker (nullptr if not initialized)
     */
    SourceTracker* tracker() { return tracker_; }
    const SourceTracker* tracker() const { return tracker_; }

    /**
     * Get current location (requires tracker)
     */
    SourceLocation location() const {
        return tracker_ ? tracker_->location() : SourceLocation();
    }

    /**
     * Check if position tracking is available
     */
    bool hasTracker() const { return tracker_ != nullptr; }

    // ============================================================================
    // Error Handling - Multiple Errors with Location Tracking
    // ============================================================================

    /**
     * Add parse error with location
     */
    bool addError(SourceLocation loc, const char* format, ...);
    bool addError(SourceLocation loc, const std::string& message);
    bool addError(SourceLocation loc, const std::string& message,
                  const std::string& hint);

    /**
     * Add error at current position (requires tracker)
     */
    bool addError(const char* format, ...);
    bool addError(const std::string& message);
    bool addError(const std::string& message, const std::string& hint);

    /**
     * Add warning
     */
    bool addWarning(SourceLocation loc, const char* format, ...);
    bool addWarning(SourceLocation loc, const std::string& message);
    bool addWarning(const char* format, ...);

    /**
     * Add note/info
     */
    bool addNote(SourceLocation loc, const std::string& message);
    bool addNote(const std::string& message);

    /**
     * Query error state
     */
    bool hasErrors() const { return errors_.hasErrors(); }
    bool hasWarnings() const { return errors_.hasWarnings(); }
    size_t errorCount() const { return errors_.errorCount(); }
    size_t warningCount() const { return errors_.warningCount(); }

    /**
     * Check if should stop parsing (max errors reached)
     */
    bool shouldStopParsing() const { return errors_.shouldStop(); }

    /**
     * Get all errors
     */
    const ParseErrorList& errors() const { return errors_; }
    ParseErrorList& errors() { return errors_; }

    /**
     * Set error limit
     */
    void setMaxErrors(size_t max) { errors_.setMaxErrors(max); }
    size_t maxErrors() const { return errors_.maxErrors(); }

    /**
     * Log all errors to console
     */
    void logAllErrors() const;

    /**
     * Format errors as string
     */
    std::string formatErrors() const { return errors_.formatErrors(); }

    /**
     * Set root to ITEM_ERROR (call after parsing if hasErrors())
     */
    void setErrorResult();

    /**
     * Set root to ITEM_NULL (for empty/invalid input)
     */
    void setNullResult();    // ============================================================================
    // Memory Access (Controlled exposure of internals)
    // ============================================================================

    /**
     * Get underlying Input (for legacy interop)
     * Try to avoid using this directly - use ctx methods instead
     */
    Input* input() const { return input_; }

    /**
     * Get memory pool (for legacy code that needs direct access)
     * Prefer using builder() methods instead
     */
    Pool* pool() const;

    /**
     * Get arena (for legacy code that needs direct access)
     * Prefer using builder() methods instead
     */
    Arena* arena() const;

    /**
     * Get URL/location
     */
    Url* url() const;

    /**
     * Get string interning pool
     */
    NamePool* namePool() const;

    // ============================================================================
    // Configuration
    // ============================================================================

    /**
     * Enable/disable string interning
     */
    void setInternStrings(bool enabled);

    /**
     * Enable/disable automatic string merging in elements
     */
    void setAutoStringMerge(bool enabled);

    // ============================================================================
    // Logging Helpers
    // ============================================================================

    /**
     * Log with context (includes URL/location)
     */
    void logDebug(const char* format, ...);
    void logInfo(const char* format, ...);
    void logWarning(const char* format, ...);
    void logError(const char* format, ...);
};

/**
 * ParserState - Optional base class for stateful parsers
 *
 * USE CASE: Complex parsers that need to track parsing state
 * (e.g., HTML parser with element stack, Markdown with nesting)
 *
 * PATTERN:
 *   struct MyParserState : public ParserState {
 *       int nesting_level;
 *       bool inside_code_block;
 *
 *       MyParserState(InputContext& ctx) : ParserState(ctx) {}
 *   };
 */
class ParserState {
protected:
    InputContext& ctx_;
    const char* content_;
    size_t pos_;
    size_t length_;

public:
    explicit ParserState(InputContext& ctx)
        : ctx_(ctx), content_(nullptr), pos_(0), length_(0) {}

    virtual ~ParserState() = default;

    // Context access
    InputContext& ctx() { return ctx_; }
    MarkBuilder& builder() { return ctx_.builder(); }

    // Position tracking
    void setContent(const char* content, size_t length) {
        content_ = content;
        length_ = length;
        pos_ = 0;
    }

    const char* current() const { return content_ + pos_; }
    size_t position() const { return pos_; }
    size_t remaining() const { return length_ - pos_; }
    bool atEnd() const { return pos_ >= length_; }

    char peek() const { return atEnd() ? '\0' : content_[pos_]; }
    char advance() { return atEnd() ? '\0' : content_[pos_++]; }
    void skip(size_t n) { pos_ = std::min(pos_ + n, length_); }

    // Utility methods
    bool match(const char* str);
    void skipWhitespace();
    String* readUntil(const char* delim);
};

#endif // LAMBDA_INPUT_CONTEXT_HPP
```

#### 1.2 Implementation

**File**: `lambda/input/parse_error.cpp`

```cpp
#include "parse_error.hpp"
#include <sstream>
#include <iomanip>

bool ParseErrorList::addError(const ParseError& error) {
    if (error.severity == ParseErrorSeverity::ERROR) {
        error_count_++;
        if (error_count_ > max_errors_) {
            return false;
        }
    } else if (error.severity == ParseErrorSeverity::WARNING) {
        warning_count_++;
    }

    errors_.push_back(error);
    return true;
}

bool ParseErrorList::addError(SourceLocation loc, const std::string& message) {
    return addError(ParseError(ParseErrorSeverity::ERROR, loc, message));
}

bool ParseErrorList::addError(SourceLocation loc, const std::string& message,
                               const std::string& hint) {
    return addError(ParseError(ParseErrorSeverity::ERROR, loc, message, hint));
}

bool ParseErrorList::addWarning(SourceLocation loc, const std::string& message) {
    return addError(ParseError(ParseErrorSeverity::WARNING, loc, message));
}

bool ParseErrorList::addWarning(SourceLocation loc, const std::string& message,
                                 const std::string& hint) {
    return addError(ParseError(ParseErrorSeverity::WARNING, loc, message, hint));
}

bool ParseErrorList::addNote(SourceLocation loc, const std::string& message) {
    return addError(ParseError(ParseErrorSeverity::NOTE, loc, message));
}

std::string ParseErrorList::formatErrors() const {
    std::ostringstream oss;

    for (size_t i = 0; i < errors_.size(); i++) {
        oss << formatError(errors_[i], true);
        if (i < errors_.size() - 1) {
            oss << "\n";
        }
    }

    // Summary
    if (error_count_ > 0 || warning_count_ > 0) {
        oss << "\n\n";
        if (error_count_ > 0) {
            oss << error_count_ << " error(s)";
        }
        if (warning_count_ > 0) {
            if (error_count_ > 0) oss << ", ";
            oss << warning_count_ << " warning(s)";
        }
        oss << " found";

        if (error_count_ >= max_errors_) {
            oss << " (stopped at max error limit)";
        }
    }

    return oss.str();
}

std::string ParseErrorList::formatError(const ParseError& err, bool with_context) const {
    std::ostringstream oss;

    // Severity prefix
    switch (err.severity) {
        case ParseErrorSeverity::ERROR:
            oss << "error";
            break;
        case ParseErrorSeverity::WARNING:
            oss << "warning";
            break;
        case ParseErrorSeverity::NOTE:
            oss << "note";
            break;
    }

    // Location
    if (err.location.isValid()) {
        oss << " at line " << err.location.line
            << ", column " << err.location.column;
    }

    // Message
    oss << ": " << err.message;

    // Context line
    if (with_context && !err.context_line.empty()) {
        oss << "\n  " << err.context_line;

        // Add caret to show exact position
        if (err.location.column > 0) {
            oss << "\n  ";
            for (int i = 1; i < err.location.column; i++) {
                oss << " ";
            }
            oss << "^";
        }
    }

    // Hint
    if (!err.hint.empty()) {
        oss << "\n  hint: " << err.hint;
    }

    return oss.str();
}
```

**File**: `lambda/input/source_tracker.cpp`

```cpp
#include "source_tracker.hpp"
#include <cstring>

SourceTracker::SourceTracker(const char* content)
    : content_(content)
    , length_(content ? strlen(content) : 0)
    , offset_(0)
    , line_(1)
    , column_(1)
{
    // Build line start index
    line_starts_.push_back(0);
    for (size_t i = 0; i < length_; i++) {
        if (content_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

SourceTracker::SourceTracker(const char* content, size_t length)
    : content_(content)
    , length_(length)
    , offset_(0)
    , line_(1)
    , column_(1)
{
    // Build line start index
    line_starts_.push_back(0);
    for (size_t i = 0; i < length_; i++) {
        if (content_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

char SourceTracker::advance() {
    if (atEnd()) return '\0';

    char c = content_[offset_++];

    if (c == '\n') {
        line_++;
        column_ = 1;
    } else if (c == '\r') {
        // handle \r\n as single newline
        if (offset_ < length_ && content_[offset_] == '\n') {
            offset_++;
        }
        line_++;
        column_ = 1;
    } else {
        // UTF-8 aware column counting (count characters, not bytes)
        if ((c & 0xC0) != 0x80) {  // not a continuation byte
            column_++;
        }
    }

    return c;
}

void SourceTracker::skip(size_t n) {
    for (size_t i = 0; i < n && !atEnd(); i++) {
        advance();
    }
}

bool SourceTracker::match(const char* str) {
    size_t len = strlen(str);
    if (remaining() < len) return false;
    if (strncmp(current(), str, len) != 0) return false;
    skip(len);
    return true;
}

void SourceTracker::restore(const SourceLocation& loc) {
    offset_ = loc.offset;
    line_ = loc.line;
    column_ = loc.column;
}

std::string SourceTracker::getLine(int line_num) const {
    if (line_num < 1 || line_num > static_cast<int>(line_starts_.size())) {
        return "";
    }

    size_t start = line_starts_[line_num - 1];
    size_t end = (line_num < static_cast<int>(line_starts_.size()))
                 ? line_starts_[line_num] - 1
                 : length_;

    // Trim trailing newline
    while (end > start && (content_[end - 1] == '\n' || content_[end - 1] == '\r')) {
        end--;
    }

    return std::string(content_ + start, end - start);
}

std::string SourceTracker::getContextLine(const SourceLocation& loc) const {
    return getLine(loc.line);
}

void SourceTracker::skipWhitespace() {
    while (!atEnd() && (peek() == ' ' || peek() == '\t' ||
                        peek() == '\n' || peek() == '\r')) {
        advance();
    }
}

void SourceTracker::skipToEndOfLine() {
    while (!atEnd() && peek() != '\n' && peek() != '\r') {
        advance();
    }
}

bool SourceTracker::startsWith(const char* prefix) const {
    size_t len = strlen(prefix);
    return remaining() >= len && strncmp(current(), prefix, len) == 0;
}
```

**File**: `lambda/input/input_context.cpp`

```cpp
#include "input_context.hpp"
#include <stdarg.h>
#include <ctype.h>
#include <cstring>

// ============================================================================
// InputContext Implementation
// ============================================================================

InputContext::InputContext(Input* input)
    : input_(input)
    , builder_(input)
    , errors_()
    , tracker_(nullptr)
    , owns_tracker_(false)
{
}

InputContext::InputContext(Input* input, const char* content)
    : input_(input)
    , builder_(input)
    , errors_()
    , tracker_(new SourceTracker(content))
    , owns_tracker_(true)
{
}

InputContext::InputContext(Input* input, const char* content, size_t length)
    : input_(input)
    , builder_(input)
    , errors_()
    , tracker_(new SourceTracker(content, length))
    , owns_tracker_(true)
{
}

InputContext::InputContext(Input* input, SourceTracker* tracker)
    : input_(input)
    , builder_(input)
    , errors_()
    , tracker_(tracker)
    , owns_tracker_(false)
{
}

InputContext::~InputContext() {
    // RAII cleanup - MarkBuilder cleans up automatically
    if (owns_tracker_ && tracker_) {
        delete tracker_;
    }
}

String* InputContext::createString(const char* str) {
    return builder_.createString(str);
}

String* InputContext::createString(const char* str, size_t len) {
    return builder_.createString(str, len);
}

ElementBuilder InputContext::element(const char* tag_name) {
    return builder_.element(tag_name);
}

MapBuilder InputContext::map() {
    return builder_.map();
}

ArrayBuilder InputContext::array() {
    return builder_.array();
}

// ============================================================================
// String Utilities
// ============================================================================

char* InputContext::trimWhitespace(const char* str) {
    if (!str) return nullptr;

    // skip leading whitespace
    while (isWhitespace(*str)) str++;

    if (*str == '\0') {
        return duplicateString("");
    }

    // find end
    const char* end = str + strlen(str) - 1;
    while (end > str && isWhitespace(*end)) end--;

    size_t len = end - str + 1;
    return duplicateString(str, len);
}

char* InputContext::duplicateString(const char* str) {
    if (!str) return nullptr;
    return duplicateString(str, strlen(str));
}

char* InputContext::duplicateString(const char* str, size_t len) {
    if (!str) return nullptr;
    char* result = (char*)pool_alloc(input_->pool, len + 1);
    if (!result) return nullptr;
    memcpy(result, str, len);
    result[len] = '\0';
    return result;
}

char** InputContext::splitLines(const char* text, int* line_count) {
    return input_split_lines(text, line_count);  // delegate to existing impl
}

void InputContext::freeLines(char** lines, int line_count) {
    input_free_lines(lines, line_count);  // delegate to existing impl
}

bool InputContext::isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool InputContext::isWhitespaceOnly(const char* str) {
    if (!str) return true;
    while (*str) {
        if (!isWhitespace(*str)) return false;
        str++;
    }
    return true;
}

void InputContext::skipWhitespace(const char** text) {
    while (**text && isWhitespace(**text)) {
        (*text)++;
    }
}

int InputContext::countLeadingSpaces(const char* line) {
    return countLeadingChar(line, ' ');
}

int InputContext::countLeadingChar(const char* line, char ch) {
    int count = 0;
    while (line[count] == ch) count++;
    return count;
}

bool InputContext::startsWith(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool InputContext::endsWith(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

const char* InputContext::findFirst(const char* str, const char* chars) {
    if (!str || !chars) return nullptr;
    return strpbrk(str, chars);
}

// ============================================================================
// Result Management
// ============================================================================

void InputContext::setRoot(Item result) {
    input_->root = result;
}

Item InputContext::getRoot() const {
    return input_->root;
}

// ============================================================================
// Error Handling
// ============================================================================

bool InputContext::addError(SourceLocation loc, const char* format, ...) {
    va_list args;
    va_start(args, format);

    char msg[512];
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    ParseError error(ParseErrorSeverity::ERROR, loc, msg);

    // Add context line if we have a tracker
    if (tracker_) {
        error.context_line = tracker_->getContextLine(loc);
    }

    return errors_.addError(error);
}

bool InputContext::addError(SourceLocation loc, const std::string& message) {
    ParseError error(ParseErrorSeverity::ERROR, loc, message);
    if (tracker_) {
        error.context_line = tracker_->getContextLine(loc);
    }
    return errors_.addError(error);
}

bool InputContext::addError(SourceLocation loc, const std::string& message,
                             const std::string& hint) {
    ParseError error(ParseErrorSeverity::ERROR, loc, message, hint);
    if (tracker_) {
        error.context_line = tracker_->getContextLine(loc);
    }
    return errors_.addError(error);
}

bool InputContext::addError(const char* format, ...) {
    if (!tracker_) {
        // Fallback: add error without location
        va_list args;
        va_start(args, format);
        char msg[512];
        vsnprintf(msg, sizeof(msg), format, args);
        va_end(args);
        return errors_.addError(SourceLocation(), msg);
    }

    va_list args;
    va_start(args, format);
    char msg[512];
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    return addError(tracker_->location(), msg);
}

bool InputContext::addError(const std::string& message) {
    SourceLocation loc = tracker_ ? tracker_->location() : SourceLocation();
    return addError(loc, message);
}

bool InputContext::addError(const std::string& message, const std::string& hint) {
    SourceLocation loc = tracker_ ? tracker_->location() : SourceLocation();
    return addError(loc, message, hint);
}

bool InputContext::addWarning(SourceLocation loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char msg[512];
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    ParseError warning(ParseErrorSeverity::WARNING, loc, msg);
    if (tracker_) {
        warning.context_line = tracker_->getContextLine(loc);
    }
    return errors_.addError(warning);
}

bool InputContext::addWarning(SourceLocation loc, const std::string& message) {
    ParseError warning(ParseErrorSeverity::WARNING, loc, message);
    if (tracker_) {
        warning.context_line = tracker_->getContextLine(loc);
    }
    return errors_.addError(warning);
}

bool InputContext::addWarning(const char* format, ...) {
    SourceLocation loc = tracker_ ? tracker_->location() : SourceLocation();

    va_list args;
    va_start(args, format);
    char msg[512];
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    return addWarning(loc, msg);
}

bool InputContext::addNote(SourceLocation loc, const std::string& message) {
    ParseError note(ParseErrorSeverity::NOTE, loc, message);
    if (tracker_) {
        note.context_line = tracker_->getContextLine(loc);
    }
    return errors_.addError(note);
}

bool InputContext::addNote(const std::string& message) {
    SourceLocation loc = tracker_ ? tracker_->location() : SourceLocation();
    return addNote(loc, message);
}

void InputContext::logAllErrors() const {
    if (errors_.empty()) return;

    std::string formatted = errors_.formatErrors();
    log_error("%s", formatted.c_str());
}

void InputContext::setErrorResult() {
    logAllErrors();
    input_->root = {.item = ITEM_ERROR};
}

void InputContext::setNullResult() {
    input_->root = {.item = ITEM_NULL};
}// ============================================================================
// Memory Access
// ============================================================================

Pool* InputContext::pool() const {
    return input_->pool;
}

Arena* InputContext::arena() const {
    return input_->arena;
}

Url* InputContext::url() const {
    return input_->url;
}

NamePool* InputContext::namePool() const {
    return input_->name_pool;
}

void InputContext::setInternStrings(bool enabled) {
    builder_.setInternStrings(enabled);
}

void InputContext::setAutoStringMerge(bool enabled) {
    builder_.setAutoStringMerge(enabled);
}

// ============================================================================
// Logging
// ============================================================================

void InputContext::logDebug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_vprintf(LOG_DEBUG, format, args);
    va_end(args);
}

void InputContext::logInfo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_vprintf(LOG_INFO, format, args);
    va_end(args);
}

void InputContext::logWarning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_vprintf(LOG_WARNING, format, args);
    va_end(args);
}

void InputContext::logError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_vprintf(LOG_ERROR, format, args);
    va_end(args);
}

// ============================================================================
// ParserState Implementation
// ============================================================================

bool ParserState::match(const char* str) {
    size_t len = strlen(str);
    if (remaining() < len) return false;
    if (strncmp(current(), str, len) != 0) return false;
    pos_ += len;
    return true;
}

void ParserState::skipWhitespace() {
    while (!atEnd() && InputContext::isWhitespace(peek())) {
        advance();
    }
}

String* ParserState::readUntil(const char* delim) {
    const char* start = current();
    const char* end = strpbrk(start, delim);

    if (!end) {
        end = content_ + length_;
    }

    size_t len = end - start;
    pos_ += len;

    return ctx_.createString(start, len);
}
```

### Phase 2: Modernize All Parsers

#### 2.1 Migration Pattern with Error Recovery

**Before** (legacy pattern - single error, no location):
```cpp
void parse_json(Input* input, const char* json_string) {
    Map* root_map = map_pooled(input->pool);

    const char* p = json_string;
    while (*p) {
        if (parse_error) {
            log_error("Parse failed");
            input->root = {.item = ITEM_ERROR};
            return;  // Stop at first error
        }
        // manual parsing...
    }

    input->root = {.item = (uint64_t)root_map};
}
```

**After** (modern pattern - multiple errors, location tracking, recovery):
```cpp
void parse_json(Input* input, const char* json_string) {
    InputContext ctx(input, json_string);  // Enable position tracking
    SourceTracker& tracker = *ctx.tracker();

    MapBuilder map_builder = ctx.map();

    while (!tracker.atEnd()) {
        SourceLocation key_start = tracker.mark();

        if (parse_error) {
            // Add error with precise location
            ctx.addError(key_start, "Expected object key, got '%c'",
                        tracker.peek());

            // Try to recover: skip to next comma or closing brace
            while (!tracker.atEnd() && tracker.peek() != ',' &&
                   tracker.peek() != '}') {
                tracker.advance();
            }

            // Check if we should stop (too many errors)
            if (ctx.shouldStopParsing()) {
                break;
            }

            continue;  // Continue parsing to find more errors
        }

        // parsing...
        map_builder.put(key, value);
    }

    // Set result based on error state
    if (ctx.hasErrors()) {
        ctx.logAllErrors();  // Print all errors with locations
        ctx.setErrorResult();
    } else {
        ctx.setRoot(map_builder.final());
    }
}
```

**Example error output**:
```
error at line 5, column 12: Expected object key, got '{'
  {"name": "John", "age": {invalid}, "city": "NYC"}
                           ^
  hint: Object values must be valid JSON

error at line 7, column 8: Unexpected token ']'
  "items": ]
           ^
  hint: Expected array opening '['

2 error(s) found
```#### 2.2 Parser Signature Standardization

**All parsers should follow**:
```cpp
// Simple parsers (stateless, with error recovery)
void parse_<format>(Input* input, const char* content) {
    InputContext ctx(input, content);  // Enable tracking

    // ... parsing logic with error recovery
    while (!ctx.tracker()->atEnd()) {
        if (error) {
            ctx.addError("Error message");
            recover_from_error(ctx);
            if (ctx.shouldStopParsing()) break;
        }
    }

    if (ctx.hasErrors()) {
        ctx.setErrorResult();
    } else {
        ctx.setRoot(result);
    }
}

// Complex parsers (stateful, with error recovery)
void parse_<format>(Input* input, const char* content) {
    InputContext ctx(input, content);
    ctx.setMaxErrors(100);  // Configure error limit

    MyParserState state(ctx);
    state.setContent(content, strlen(content));

    Item result = parse_impl(state);  // parse_impl handles errors

    if (ctx.hasErrors()) {
        ctx.logAllErrors();
        ctx.setErrorResult();
    } else {
        ctx.setRoot(result);
    }
}

// Legacy parsers (no tracking - for formats where line/col doesn't make sense)
void parse_binary_format(Input* input, const char* content) {
    InputContext ctx(input);  // No tracker for binary

    if (error) {
        ctx.addError(SourceLocation(), "Invalid header");
    }

    // ... rest of parsing
}
```

#### 2.3 Eliminate map_put() and Legacy Functions

**Replace**:
```cpp
map_put(map, key, value, input);
```

**With**:
```cpp
ctx.builder().map()
    .put(key, value)
    .final();
```

**Replace**:
```cpp
Element* elem = input_create_element(input, "div");
input_add_attribute_to_element(input, elem, "class", "container");
```

**With**:
```cpp
Item elem = ctx.element("div")
    .attr("class", "container")
    .final();
```

### Phase 3: Remove C Code, Convert to C++

#### 3.1 Convert extern "C" Functions

**Target files**:
- `input-html-context.cpp` - HTML parsing context (C)
- `input-html-tree.cpp` - HTML tree construction (C)
- `input-html-scan.cpp` - HTML tokenizer (C)

**Pattern**:
```cpp
// Before:
extern "C" {
    void html_function(Input* input, ...) {
        // C implementation
    }
}

// After:
namespace lambda {
namespace html {
    void html_function(InputContext& ctx, ...) {
        // C++ implementation with RAII, references, etc.
    }
}
}
```

#### 3.2 Modernize Type-Unsafe Code

**Replace**:
```cpp
double* dval = (double*)pool_calloc(input->pool, sizeof(double));
*dval = 3.14;
Item item = {.pointer = dval};
```

**With**:
```cpp
Item item = ctx.builder().createFloat(3.14);
```

#### 3.3 Use C++ Features

- **References** instead of pointers where ownership is clear
- **constexpr** for compile-time constants
- **std::optional** for optional values (C++17)
- **Structured bindings** where appropriate (C++17)
- **RAII** for all resources

### Phase 4: Shared Parser Utilities

#### 4.1 Create input_utils.hpp

**Common utilities used by multiple parsers**:

```cpp
#ifndef LAMBDA_INPUT_UTILS_HPP
#define LAMBDA_INPUT_UTILS_HPP

#include "input_context.hpp"

namespace lambda {
namespace input {
namespace utils {

// ============================================================================
// Character Classification
// ============================================================================

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool isAlphaNum(char c) { return isAlpha(c) || isDigit(c); }
inline bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// ============================================================================
// Number Parsing
// ============================================================================

/**
 * Parse integer from string with error checking
 * Returns true on success, false on parse error
 */
bool parseInt(const char* str, int64_t* out);
bool parseDouble(const char* str, double* out);

/**
 * Parse number and create Item
 */
Item parseNumber(InputContext& ctx, const char* str);

// ============================================================================
// Escape Sequence Handling
// ============================================================================

/**
 * Unescape string with common escape sequences
 * \\n \\t \\r \\\" \\\\ etc.
 */
String* unescapeString(InputContext& ctx, const char* str);

/**
 * Decode unicode escape sequences (\\uXXXX)
 */
bool decodeUnicodeEscape(const char* hex, char* out, size_t* out_len);

// ============================================================================
// URL/Path Utilities
// ============================================================================

/**
 * Resolve relative URL against base
 */
Url* resolveUrl(InputContext& ctx, const char* relative_url);

/**
 * Check if URL is absolute
 */
bool isAbsoluteUrl(const char* url);

// ============================================================================
// Line-Based Parsing Helpers
// ============================================================================

/**
 * Line iterator for line-oriented parsers (YAML, INI, etc.)
 */
class LineIterator {
    char** lines_;
    int current_;
    int total_;

public:
    LineIterator(char** lines, int total);

    bool hasNext() const { return current_ < total_; }
    const char* current() const { return lines_[current_]; }
    const char* next();
    const char* peek() const;
    void skip() { current_++; }
    int lineNumber() const { return current_ + 1; }
};

} // namespace utils
} // namespace input
} // namespace lambda

#endif // LAMBDA_INPUT_UTILS_HPP
```

#### 4.2 Benefits

- **Reduce ~500 lines of duplicate code** across parsers
- **Consistent behavior** (e.g., all parsers handle escapes the same way)
- **Easier to test** (test utils once, not in 20 parsers)
- **Easier to optimize** (optimize once, benefits all parsers)

### Phase 5: Update Public API

#### 5.1 Deprecate Old Functions in input.hpp

```cpp
// input.hpp

// ============================================================================
// DEPRECATED FUNCTIONS - Use InputContext instead
// ============================================================================

#ifdef LAMBDA_ENABLE_DEPRECATED_API

// DEPRECATED: Use InputContext methods instead
__attribute__((deprecated("Use InputContext::createString() instead")))
String* input_create_string(Input* input, const char* str);

// DEPRECATED: Use ctx.builder().element() instead
__attribute__((deprecated("Use ctx.builder().element() instead")))
Element* input_create_element(Input* input, const char* tag_name);

// DEPRECATED: Use MapBuilder::put() instead
__attribute__((deprecated("Use MapBuilder::put() instead")))
void map_put(Map* mp, String* key, Item value, Input* input);

// DEPRECATED: Use InputManager::createInput() instead
__attribute__((deprecated("Use InputManager::createInput() instead")))
Input* input_new(Url* abs_url);

#endif // LAMBDA_ENABLE_DEPRECATED_API

// ============================================================================
// MODERN API (Keep these)
// ============================================================================

// Core input creation
Input* input_from_source(const char* source, Url* url, String* type, String* flavor);
Input* input_from_directory(const char* directory_path, bool recursive, int max_depth);
Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);

// Format-specific entry points
void parse_json(Input* input, const char* json_string);
void parse_xml(Input* input, const char* xml_string);
void parse_yaml(Input* input, const char* yaml_str);
// ... etc (all parsers)
```

#### 5.2 Migration Guide

Create `docs/Input_API_Migration.md`:

```markdown
# Input API Migration Guide

## Overview

Lambda's input parsing system has been modernized with InputContext.
This guide helps migrate old code to the new API.

## Quick Reference

| Old API | New API |
|---------|---------|
| `input_create_string(input, str)` | `ctx.createString(str)` |
| `map_pooled(input->pool)` | `ctx.map()` |
| `array_pooled(input->pool)` | `ctx.array()` |
| `input_create_element(input, tag)` | `ctx.element(tag)` |
| `map_put(map, key, val, input)` | `builder.put(key, val)` |
| `input->pool` | `ctx.pool()` (try to avoid) |

## Migration Examples

### Example 1: Simple Parser

**Before**:
```cpp
void parse_json(Input* input, const char* json) {
    Map* map = map_pooled(input->pool);
    String* key = input_create_string(input, "result");
    map_put(map, key, value, input);
    input->root = {.item = (uint64_t)map};
}
```

**After**:
```cpp
void parse_json(Input* input, const char* json) {
    InputContext ctx(input);
    ctx.setRoot(ctx.map()
        .put("result", value)
        .final());
}
```

### Example 2: Complex Parser

**Before**:
```cpp
void parse_complex(Input* input, const char* content) {
    StringBuf* sb = stringbuf_new(input->pool);
    Array* arr = array_pooled(input->pool);

    // parsing...
    String* str = input_create_string(input, sb->data);
    array_append(arr, {.string = str}, input->pool);

    stringbuf_free(sb);
    input->root = {.array = arr};
}
```

**After**:
```cpp
void parse_complex(Input* input, const char* content) {
    InputContext ctx(input);

    ctx.setRoot(ctx.array()
        .append(ctx.createString(parsed_data))
        .final());
}
```
```

---

## Implementation Roadmap

### Week 1: Foundation with Error System
- [ ] **Day 1**: Implement error tracking infrastructure
  - Write `parse_error.hpp` and `parse_error.cpp`
  - Write `source_tracker.hpp` and `source_tracker.cpp`
  - Unit tests for error tracking and position tracking

- [ ] **Day 2-3**: Implement `InputContext` class with error support
  - Write `input_context.hpp` and `input_context.cpp`
  - Integrate SourceTracker and ParseErrorList
  - Unit tests for InputContext methods
  - Test error recovery patterns

- [ ] **Day 4**: Implement `input_utils.hpp`
  - Extract common utilities from parsers
  - Add error recovery helpers
  - Unit tests for utilities
  - Documentation

- [ ] **Day 5**: Create migration tooling and examples
  - Write `docs/Input_API_Migration.md`
  - Write `docs/Error_Recovery_Patterns.md`
  - Create example migrations with error handling
  - Deprecation warnings

### Week 2: Pilot Migration (Simple Parsers with Error Recovery)
- [ ] Migrate simple parsers (10 files) with error recovery:
  - `input-json.cpp` ✅ (already using MarkBuilder) - add error recovery
    - Track unclosed braces/brackets
    - Report unexpected tokens with location
    - Recover at commas or closing delimiters
  - `input-csv.cpp` - add error recovery
    - Report mismatched column counts
    - Handle quote escaping errors
  - `input-ini.cpp` - add error recovery
    - Report invalid section headers
    - Handle missing = in key-value pairs
  - `input-prop.cpp` - add error recovery
    - Similar to INI
  - `input-yaml.cpp` ✅ (already using MarkBuilder) - add error recovery
    - Track indentation errors
    - Report invalid block scalars
    - Recover at next valid indent level
  - `input-mark.cpp` - add error recovery
    - Element syntax errors
    - Attribute parsing errors
  - `input-eml.cpp` - add error recovery
    - Invalid header formats
  - `input-vcf.cpp` - add error recovery
    - Invalid vCard properties
  - `input-ics.cpp` - add error recovery
    - Invalid iCalendar components
  - `input-man.cpp` ✅ (recently refactored) - add error recovery
    - Invalid man page directives

- [ ] Test each parser after migration
  - Verify error messages include line/column
  - Test error recovery (multiple errors reported)
  - Verify error limit works (stops at 100)
- [ ] Document error recovery patterns discovered

### Week 3: Medium Parsers + C to C++ Conversion
- [ ] Migrate medium parsers (15 files):
  - `input-xml.cpp`
  - `input-toml.cpp`
  - `input-rtf.cpp`
  - `input-latex.cpp`
  - `input-org.cpp`
  - `input-adoc.cpp`
  - `input-mdx.cpp`
  - `input-jsx.cpp`
  - `input-css.cpp`
  - `input-graph-dot.cpp`
  - `input-graph-mermaid.cpp`
  - `input-graph-d2.cpp`
  - `input-math-ascii.cpp`

- [ ] Convert C files to C++:
  - `input-html-context.cpp`
  - `input-html-tree.cpp`
  - `input-html-scan.cpp`

### Week 4: Large/Complex Parsers + Cleanup
- [ ] Migrate large parsers (5 files):
  - `input-html.cpp` (6000+ lines, complex state)
  - `input-markup.cpp` (6000+ lines, multiple formats)
  - `input-math.cpp` (5000+ lines, expression parsing)
  - `input-pdf.cpp` (complex binary format)

- [ ] Remove deprecated functions:
  - Remove `input_create_string()` implementation
  - Remove `map_put()` implementation
  - Remove other deprecated C functions
  - Clean up `input.hpp`

- [ ] Final testing:
  - Run all 2081+ tests
  - Performance benchmarks
  - Memory leak checks
  - Integration tests

---

## Success Metrics

### Code Quality
- ✅ **Consistency**: All parsers use same API pattern
- ✅ **Modern C++**: No extern "C" blocks, use RAII throughout
- ✅ **Reduced Duplication**: ~500 lines of duplicate code eliminated
- ✅ **Better Abstraction**: Input internals encapsulated behind InputContext

### Error Reporting Quality
- ✅ **Location tracking**: All errors report line and column numbers
- ✅ **Multiple errors**: Parsers can report up to 100 errors per document
- ✅ **Error recovery**: Parsers continue after errors to find more issues
- ✅ **Context display**: Errors show the problematic line with caret indicator
- ✅ **Helpful hints**: Errors include suggestions for fixing when possible
- ✅ **Consistent format**: All parsers use same error message format

### Performance
- ✅ **No regression**: Parsing speed within 5% of baseline
- ✅ **Memory efficiency**: No increase in memory usage
- ✅ **Build time**: No significant increase in compile time
- ✅ **Error tracking overhead**: SourceTracker adds < 1% overhead to parsing

### Maintainability
- ✅ **Clear patterns**: New contributor can add parser in < 200 LOC
- ✅ **Error handling**: Consistent error reporting across all parsers
- ✅ **Testing**: Easier to test parsers in isolation
- ✅ **Documentation**: Clear examples and migration guide

### Developer Experience
- ✅ **Fast iteration**: Fix multiple errors at once, not one at a time
- ✅ **IDE integration ready**: Error format compatible with LSP/diagnostic tools
- ✅ **Clear feedback**: Users know exactly where and what went wrong
- ✅ **Learning aid**: Hints help users understand format requirements

---

## Risk Mitigation

### Risk 1: Breaking Changes
**Mitigation**:
- Keep old API available with deprecation warnings
- Gradual migration (don't change everything at once)
- Extensive testing after each file migration

### Risk 2: Performance Regression
**Mitigation**:
- Benchmark before/after for each parser
- Profile hot paths in large parsers
- InputContext is zero-overhead (just wraps existing objects)

### Risk 3: Complex Parser Migration
**Mitigation**:
- Start with simple parsers to establish patterns
- Leave most complex parsers (HTML, PDF) for last
- ParserState base class helps complex parsers

### Risk 4: Developer Confusion During Transition
**Mitigation**:
- Clear migration guide with examples
- Deprecation warnings guide developers to new API
- Code reviews ensure quality during transition

---

## Long-Term Benefits

### 1. **Easier to Add New Parsers**
```cpp
// New parser using modern API with error recovery (< 150 LOC for simple format)
void parse_my_format(Input* input, const char* content) {
    InputContext ctx(input, content);  // Enable tracking
    SourceTracker& tracker = *ctx.tracker();

    while (!tracker.atEnd()) {
        if (error_condition) {
            ctx.addError("Expected token X, got '%c'", tracker.peek(),
                        "Try adding a comma separator");
            recover_to_next_token(tracker);
            if (ctx.shouldStopParsing()) break;
            continue;
        }
        // ... parsing
    }

    if (ctx.hasErrors()) {
        ctx.setErrorResult();
    } else {
        ctx.setRoot(result);
    }
}
```

### 2. **Excellent Error Messages**
```cpp
// Errors automatically include location and context
ctx.addError("Expected closing brace",
             "Unmatched '{' opened at line 15");

// Output:
// error at line 42, column 5: Expected closing brace
//   {"name": "John", "age": 30
//       ^
//   hint: Unmatched '{' opened at line 15
```### 3. **Shared Optimization**
```cpp
// Optimize string interning once, all parsers benefit
ctx.setInternStrings(true);  // dedup identical strings
```

### 4. **Future-Proof Architecture**
- Easy to add new features (e.g., streaming parsers)
- Easy to switch memory allocators (pool vs arena)
- Easy to add instrumentation (profiling, memory tracking)

### 5. **Better Testing**
```cpp
// Test error collection
TEST(JsonParser, MultipleErrors) {
    Input input = create_test_input();
    parse_json(&input, "{invalid json with multiple, errors}");

    InputContext ctx(&input);
    EXPECT_TRUE(ctx.hasErrors());
    EXPECT_GE(ctx.errorCount(), 2);

    // Check error locations
    auto& errors = ctx.errors().errors();
    EXPECT_EQ(errors[0].location.line, 1);
    EXPECT_EQ(errors[0].location.column, 2);
}

// Test error recovery
TEST(JsonParser, ContinuesAfterError) {
    Input input = create_test_input();
    parse_json(&input, "{\"a\": invalid, \"b\": \"valid\"}");

    InputContext ctx(&input);
    EXPECT_TRUE(ctx.hasErrors());

    // Parser should have recovered and parsed "b"
    auto result = input.root;
    EXPECT_NE(result.type, ITEM_ERROR);
}

// Test error limit
TEST(JsonParser, StopsAtMaxErrors) {
    Input input = create_test_input();
    std::string bad_json = generate_json_with_n_errors(200);

    InputContext ctx(&input, bad_json.c_str());
    ctx.setMaxErrors(50);

    parse_json(&input, bad_json.c_str());

    EXPECT_LE(ctx.errorCount(), 50);
    EXPECT_TRUE(ctx.shouldStopParsing());
}
```

### 6. **IDE and Tool Integration**
```cpp
// Error format is compatible with LSP (Language Server Protocol)
struct LSPDiagnostic {
    Range range;           // from SourceLocation
    Severity severity;     // from ParseErrorSeverity
    std::string message;   // from ParseError
    std::string source = "lambda";
};

// Easy to convert Lambda errors to LSP diagnostics
std::vector<LSPDiagnostic> to_lsp_diagnostics(const ParseErrorList& errors) {
    std::vector<LSPDiagnostic> result;
    for (auto& err : errors.errors()) {
        LSPDiagnostic diag;
        diag.range.start.line = err.location.line - 1;  // LSP is 0-based
        diag.range.start.character = err.location.column - 1;
        diag.message = err.message;
        // ... set severity
        result.push_back(diag);
    }
    return result;
}
```

---

## Conclusion

This refactoring will:
- ✅ **Modernize** the codebase (C → C++, legacy → modern API)
- ✅ **Unify** parser architecture (consistent patterns)
- ✅ **Simplify** maintenance (less duplication, better abstraction)
- ✅ **Dramatically improve** error reporting (location tracking, multiple errors, recovery)
- ✅ **Enhance** developer experience (clearer API, excellent error messages)
- ✅ **Enable** IDE integration (LSP-compatible diagnostics)
- ✅ **Enable** future improvements (streaming, optimization, instrumentation)

**Key Innovation**: The error tracking system with position tracking and error recovery will significantly improve the user experience when working with Lambda documents. Users will get comprehensive error reports with exact locations and helpful hints, dramatically reducing debugging time.

**Recommended**: Proceed with implementation starting Week 1.

---

## Appendix D: Error Recovery Examples

### JSON Parser Error Recovery

```cpp
// Recovery strategy: Skip to next comma or closing brace/bracket
void recover_json_error(SourceTracker& tracker) {
    int depth = 0;
    while (!tracker.atEnd()) {
        char c = tracker.peek();

        if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (depth == 0) break;
            depth--;
        } else if (c == ',' && depth == 0) {
            tracker.advance();  // consume comma
            break;
        }

        tracker.advance();
    }
}
```

### YAML Parser Error Recovery

```cpp
// Recovery strategy: Skip to next line at same or lower indent level
void recover_yaml_error(SourceTracker& tracker, int expected_indent) {
    tracker.skipToEndOfLine();
    tracker.advance();  // skip newline

    while (!tracker.atEnd()) {
        int indent = 0;
        while (tracker.peek() == ' ') {
            indent++;
            tracker.advance();
        }

        if (indent <= expected_indent && tracker.peek() != '\n') {
            break;  // found valid line
        }

        tracker.skipToEndOfLine();
        tracker.advance();
    }
}
```

### XML/HTML Parser Error Recovery

```cpp
// Recovery strategy: Skip to next tag
void recover_xml_error(SourceTracker& tracker) {
    while (!tracker.atEnd()) {
        if (tracker.peek() == '<') {
            break;
        }
        tracker.advance();
    }
}
```

### Error Recovery Pattern Summary

| Format | Recovery Strategy | Resync Points |
|--------|------------------|---------------|
| JSON | Skip to comma, closing brace/bracket | `,` `}` `]` |
| YAML | Skip to next line at valid indent | Start of line |
| XML/HTML | Skip to next tag | `<` |
| CSV | Skip to next line | Newline |
| INI/Properties | Skip to next section or key | `[` or start of line |
| LaTeX | Skip to next command or paragraph | `\` or blank line |
| Markdown | Skip to next block | Blank line |

**General Principle**: Find the next syntactic boundary that allows the parser to resynchronize with the document structure.

---

## Appendix A: Parser Inventory

| Parser File | Lines | Status | Complexity | Priority |
|-------------|-------|--------|------------|----------|
| input-json.cpp | 188 | ✅ Modern | Low | P1 |
| input-yaml.cpp | 404 | ✅ Modern | Medium | P1 |
| input-csv.cpp | 200 | Legacy | Low | P1 |
| input-ini.cpp | 350 | Legacy | Low | P1 |
| input-prop.cpp | 280 | Legacy | Low | P1 |
| input-mark.cpp | 560 | Hybrid | Medium | P1 |
| input-eml.cpp | 300 | Legacy | Low | P2 |
| input-vcf.cpp | 390 | Legacy | Low | P2 |
| input-ics.cpp | 570 | Legacy | Medium | P2 |
| input-man.cpp | 425 | ✅ Modern | Low | P2 |
| input-xml.cpp | 750 | Hybrid | Medium | P2 |
| input-toml.cpp | 720 | Legacy | Medium | P2 |
| input-rtf.cpp | 470 | Legacy | Medium | P2 |
| input-latex.cpp | 1100 | Hybrid | High | P3 |
| input-org.cpp | 2081 | Legacy | High | P3 |
| input-adoc.cpp | 631 | Legacy | Medium | P3 |
| input-mdx.cpp | 300 | Hybrid | Medium | P2 |
| input-jsx.cpp | 500 | Hybrid | Medium | P2 |
| input-css.cpp | 1400 | Legacy | High | P3 |
| input-graph-dot.cpp | 480 | Hybrid | Medium | P2 |
| input-graph-mermaid.cpp | 420 | Hybrid | Medium | P2 |
| input-graph-d2.cpp | 220 | Legacy | Low | P2 |
| input-math-ascii.cpp | 800 | Hybrid | High | P3 |
| input-html.cpp | 920 | Hybrid | Very High | P4 |
| input-markup.cpp | 6139 | Legacy | Very High | P4 |
| input-math.cpp | 5238 | Legacy | Very High | P4 |
| input-pdf.cpp | 1470 | Legacy | Very High | P4 |
| **TOTAL** | **~25,000** | | | |

**Status Legend**:
- ✅ Modern: Already uses MarkBuilder
- Hybrid: Uses some MarkBuilder, some legacy
- Legacy: All legacy API

**Priority**:
- P1: Week 2 (simple parsers)
- P2: Week 3 (medium parsers)
- P3: Week 3 (complex parsers)
- P4: Week 4 (very complex parsers)

---

## Appendix B: MarkBuilder Current Usage

**Already using MarkBuilder** (15 files):
- input-json.cpp ✅
- input-yaml.cpp ✅
- input-xml.cpp ✅
- input-html.cpp ✅
- input-toml.cpp ✅
- input-mark.cpp ✅
- input-rtf.cpp ✅
- input-latex.cpp ✅
- input-graph-dot.cpp ✅
- input-graph-mermaid.cpp ✅
- input-man.cpp ✅
- input-eml.cpp ✅
- input-jsx.cpp ✅
- input-mdx.cpp ✅
- input-css.cpp ✅

**Still using legacy API** (25 files):
- All others need migration

---

## Appendix C: Estimated Lines of Code Changes

| Category | Est. LOC |
|----------|----------|
| New InputContext implementation | ~800 |
| New input_utils.hpp utilities | ~500 |
| Parser migrations (40 files × ~50 LOC avg) | ~2,000 |
| C to C++ conversions (3 files) | ~300 |
| Deprecated API removal | -~500 |
| Documentation | ~200 |
| **NET TOTAL** | **~3,300** |

**Code reduction from deduplication**: ~500 LOC removed

**Final LOC**: ~2,800 net addition (mostly better abstraction)

---

*Document Version: 1.0*
*Author: AI Assistant*
*Date: 2025-01-17*
