# Structured Parser Design for Hang Prevention

## Overview
This document outlines systematic design patterns and implementation strategies to prevent parser hangs through structured approaches rather than ad-hoc fixes.

## 1. Parser Architecture Patterns

### 1.1 Finite State Machine (FSM) Design
**Principle**: Every parser state must have defined transitions and exit conditions.

```cpp
// Example: Structured FSM for markup parsing
enum ParserState {
    STATE_TEXT,
    STATE_MARKUP_START,
    STATE_MARKUP_CONTENT,
    STATE_MARKUP_END,
    STATE_ERROR,
    STATE_COMPLETE
};

struct ParserContext {
    ParserState state;
    size_t position;
    size_t max_position;
    int iteration_count;
    int max_iterations;
};

bool advance_parser_state(ParserContext* ctx, const char* input) {
    size_t start_pos = ctx->position;
    
    switch (ctx->state) {
        case STATE_TEXT:
            // Process text, guaranteed to advance position
            break;
        case STATE_MARKUP_START:
            // Process markup start, with defined exit conditions
            break;
        // ... other states
    }
    
    // Guarantee: Every state transition MUST advance position or change state
    if (ctx->position == start_pos && ctx->state == previous_state) {
        ctx->state = STATE_ERROR;  // Force state change if no progress
        return false;
    }
    
    return true;
}
```

### 1.2 Bounded Context Design
**Principle**: Every parsing operation operates within defined bounds.

```cpp
// Example: Bounded parsing context
struct BoundedParser {
    const char* input;
    size_t start_pos;
    size_t end_pos;        // Hard boundary
    size_t current_pos;
    size_t max_operations; // Operation limit
    size_t operation_count;
};

typedef struct {
    bool success;
    size_t consumed;       // How much input was consumed
    void* result;
} ParseResult;

ParseResult parse_bounded(BoundedParser* parser, ParseFunction func) {
    if (parser->current_pos >= parser->end_pos) {
        return (ParseResult){false, 0, NULL};
    }
    
    if (parser->operation_count >= parser->max_operations) {
        return (ParseResult){false, 0, NULL};  // Operation limit hit
    }
    
    size_t start = parser->current_pos;
    ParseResult result = func(parser);
    
    // Guarantee: Parser must consume input or fail
    if (result.success && result.consumed == 0) {
        result.success = false;  // Prevent zero-progress success
    }
    
    parser->operation_count++;
    return result;
}
```

## 2. Recursive Descent with Safety

### 2.1 Stack-Safe Recursive Design
**Principle**: Convert deep recursion to iteration with explicit stack.

```cpp
// Instead of recursive descent:
// ParseResult parse_nested(input, depth) {
//     if (complex_condition) return parse_nested(input, depth + 1);
// }

// Use explicit stack:
typedef struct {
    ParserState state;
    size_t position;
    int nesting_level;
} ParseFrame;

ParseResult parse_nested_iterative(const char* input, size_t len) {
    #define MAX_STACK_DEPTH 100
    ParseFrame stack[MAX_STACK_DEPTH];
    int stack_top = 0;
    
    // Initialize
    stack[0] = (ParseFrame){STATE_START, 0, 0};
    
    while (stack_top >= 0 && stack_top < MAX_STACK_DEPTH) {
        ParseFrame* current = &stack[stack_top];
        
        switch (current->state) {
            case STATE_START:
                // Process start state
                if (needs_nesting) {
                    if (stack_top + 1 >= MAX_STACK_DEPTH) {
                        return (ParseResult){false, 0, NULL}; // Stack overflow protection
                    }
                    stack[++stack_top] = (ParseFrame){STATE_NESTED, current->position, current->nesting_level + 1};
                } else {
                    current->state = STATE_COMPLETE;
                }
                break;
                
            case STATE_NESTED:
                // Process nested content
                current->state = STATE_COMPLETE;
                break;
                
            case STATE_COMPLETE:
                stack_top--;  // Pop frame
                break;
        }
    }
    
    return stack_top < 0 ? (ParseResult){true, 0, NULL} : (ParseResult){false, 0, NULL};
    #undef MAX_STACK_DEPTH
}
```

### 2.2 Progressive Parsing Pattern
**Principle**: Break complex parsing into progressive stages with validation.

```cpp
// Example: Multi-stage table parsing
typedef enum {
    TABLE_STAGE_DETECT,
    TABLE_STAGE_HEADERS,
    TABLE_STAGE_ROWS,
    TABLE_STAGE_VALIDATE,
    TABLE_STAGE_COMPLETE
} TableParseStage;

ParseResult parse_table_progressive(const char* input, size_t len) {
    TableParseStage stage = TABLE_STAGE_DETECT;
    size_t position = 0;
    int stage_iterations = 0;
    const int MAX_STAGE_ITERATIONS = 1000;
    
    while (stage != TABLE_STAGE_COMPLETE && position < len) {
        size_t start_pos = position;
        stage_iterations++;
        
        if (stage_iterations > MAX_STAGE_ITERATIONS) {
            // Stage took too long, fail gracefully
            return (ParseResult){false, position, NULL};
        }
        
        switch (stage) {
            case TABLE_STAGE_DETECT:
                if (detect_table_start(input, &position, len)) {
                    stage = TABLE_STAGE_HEADERS;
                    stage_iterations = 0;  // Reset for new stage
                } else {
                    return (ParseResult){false, position, NULL};
                }
                break;
                
            case TABLE_STAGE_HEADERS:
                if (parse_table_headers(input, &position, len)) {
                    stage = TABLE_STAGE_ROWS;
                    stage_iterations = 0;
                } else {
                    return (ParseResult){false, position, NULL};
                }
                break;
                
            case TABLE_STAGE_ROWS:
                if (!parse_table_row(input, &position, len)) {
                    stage = TABLE_STAGE_VALIDATE;
                    stage_iterations = 0;
                }
                break;
                
            case TABLE_STAGE_VALIDATE:
                stage = TABLE_STAGE_COMPLETE;
                break;
        }
        
        // Guarantee: Each stage must make progress
        if (position == start_pos && stage == previous_stage) {
            position++;  // Force advancement to prevent infinite loop
        }
    }
    
    return (ParseResult){stage == TABLE_STAGE_COMPLETE, position, NULL};
}
```

## 3. Resource Management Patterns

### 3.1 Parser Resource Budgeting
**Principle**: Allocate finite resources to parsing operations.

```cpp
typedef struct {
    size_t memory_budget;     // Maximum memory allocation
    size_t memory_used;
    size_t time_budget_ms;    // Maximum time allowed
    clock_t start_time;
    size_t operation_budget;  // Maximum operations
    size_t operations_used;
} ResourceBudget;

bool check_resource_budget(ResourceBudget* budget) {
    // Check time budget
    if (budget->time_budget_ms > 0) {
        clock_t current_time = clock();
        double elapsed_ms = ((double)(current_time - budget->start_time) / CLOCKS_PER_SEC) * 1000;
        if (elapsed_ms > budget->time_budget_ms) {
            return false;  // Time budget exceeded
        }
    }
    
    // Check memory budget
    if (budget->memory_used > budget->memory_budget) {
        return false;  // Memory budget exceeded
    }
    
    // Check operation budget
    if (budget->operations_used > budget->operation_budget) {
        return false;  // Operation budget exceeded
    }
    
    return true;
}

void* budget_malloc(ResourceBudget* budget, size_t size) {
    if (budget->memory_used + size > budget->memory_budget) {
        return NULL;  // Would exceed budget
    }
    
    void* ptr = malloc(size);
    if (ptr) {
        budget->memory_used += size;
    }
    return ptr;
}
```

### 3.2 Cooperative Parsing
**Principle**: Parser yields control periodically to prevent monopolizing resources.

```cpp
typedef struct {
    bool should_yield;
    size_t operations_since_yield;
    size_t yield_threshold;
    void (*yield_callback)(void* context);
    void* yield_context;
} CooperativeParser;

bool parser_should_yield(CooperativeParser* parser) {
    parser->operations_since_yield++;
    
    if (parser->operations_since_yield >= parser->yield_threshold) {
        parser->operations_since_yield = 0;
        parser->should_yield = true;
        
        if (parser->yield_callback) {
            parser->yield_callback(parser->yield_context);
        }
        
        return true;
    }
    
    return false;
}

ParseResult parse_cooperative(const char* input, size_t len, CooperativeParser* coop) {
    size_t position = 0;
    
    while (position < len) {
        // Check if we should yield control
        if (parser_should_yield(coop)) {
            // Return partial result, can be resumed later
            return (ParseResult){false, position, NULL};  // Indicates "yield"
        }
        
        // Continue parsing
        position += parse_single_element(input + position, len - position);
    }
    
    return (ParseResult){true, position, NULL};
}
```

## 4. Input Validation and Sanitization

### 4.1 Layered Validation
**Principle**: Validate input at multiple layers before deep parsing.

```cpp
typedef enum {
    VALIDATION_BASIC,      // Basic format checks
    VALIDATION_STRUCTURE,  // Structural integrity
    VALIDATION_SEMANTIC,   // Semantic correctness
    VALIDATION_COMPLETE
} ValidationLevel;

typedef struct {
    bool valid;
    ValidationLevel level_reached;
    size_t error_position;
    const char* error_message;
} ValidationResult;

ValidationResult validate_input_layered(const char* input, size_t len) {
    ValidationResult result = {false, VALIDATION_BASIC, 0, NULL};
    
    // Layer 1: Basic validation (length, encoding, etc.)
    if (!validate_basic_format(input, len)) {
        result.error_message = "Basic format validation failed";
        return result;
    }
    result.level_reached = VALIDATION_STRUCTURE;
    
    // Layer 2: Structure validation (balanced delimiters, etc.)
    if (!validate_structure(input, len)) {
        result.error_message = "Structure validation failed";
        return result;
    }
    result.level_reached = VALIDATION_SEMANTIC;
    
    // Layer 3: Semantic validation (references, etc.)
    if (!validate_semantics(input, len)) {
        result.error_message = "Semantic validation failed";
        return result;
    }
    result.level_reached = VALIDATION_COMPLETE;
    result.valid = true;
    
    return result;
}

ParseResult parse_with_validation(const char* input, size_t len) {
    ValidationResult validation = validate_input_layered(input, len);
    
    if (!validation.valid) {
        // Use simplified parsing for invalid input
        return parse_as_plain_text(input, len);
    }
    
    // Use full parsing for validated input
    return parse_full_markup(input, len);
}
```

### 4.2 Input Complexity Analysis
**Principle**: Analyze input complexity before parsing to choose appropriate strategy.

```cpp
typedef struct {
    size_t nesting_depth;
    size_t total_markup_elements;
    size_t table_count;
    size_t inline_formatting_density;
    bool has_complex_structures;
} ComplexityAnalysis;

ComplexityAnalysis analyze_input_complexity(const char* input, size_t len) {
    ComplexityAnalysis analysis = {0};
    
    // Quick scan to assess complexity
    size_t current_nesting = 0;
    size_t max_nesting = 0;
    
    for (size_t i = 0; i < len; i++) {
        switch (input[i]) {
            case '[': case '{': case '(':
                current_nesting++;
                if (current_nesting > max_nesting) {
                    max_nesting = current_nesting;
                }
                break;
            case ']': case '}': case ')':
                if (current_nesting > 0) current_nesting--;
                break;
            case '|':
                if (i + 3 < len && strncmp(&input[i], "|===", 4) == 0) {
                    analysis.table_count++;
                }
                break;
            case '*': case '_': case '`':
                analysis.inline_formatting_density++;
                break;
        }
    }
    
    analysis.nesting_depth = max_nesting;
    analysis.has_complex_structures = (max_nesting > 5 || analysis.table_count > 3);
    
    return analysis;
}

ParseResult parse_adaptive(const char* input, size_t len) {
    ComplexityAnalysis complexity = analyze_input_complexity(input, len);
    
    if (complexity.has_complex_structures) {
        // Use conservative parsing with strict limits
        return parse_with_strict_limits(input, len);
    } else if (complexity.nesting_depth > 3) {
        // Use iterative instead of recursive parsing
        return parse_iterative(input, len);
    } else {
        // Use full-featured parsing
        return parse_full_features(input, len);
    }
}
```

## 5. Error Recovery and Graceful Degradation

### 5.1 Hierarchical Fallback Strategy
**Principle**: Multiple levels of parsing sophistication with fallbacks.

```cpp
typedef enum {
    PARSE_STRATEGY_FULL,      // Full feature parsing
    PARSE_STRATEGY_SAFE,      // Safe subset parsing
    PARSE_STRATEGY_MINIMAL,   // Minimal parsing
    PARSE_STRATEGY_PLAIN_TEXT // Plain text fallback
} ParseStrategy;

ParseResult parse_with_fallback(const char* input, size_t len) {
    ParseStrategy strategies[] = {
        PARSE_STRATEGY_FULL,
        PARSE_STRATEGY_SAFE,
        PARSE_STRATEGY_MINIMAL,
        PARSE_STRATEGY_PLAIN_TEXT
    };
    
    for (int i = 0; i < sizeof(strategies) / sizeof(strategies[0]); i++) {
        ParseResult result = parse_with_strategy(input, len, strategies[i]);
        
        if (result.success || strategies[i] == PARSE_STRATEGY_PLAIN_TEXT) {
            // Log which strategy was used for debugging
            fprintf(stderr, "Parsing completed with strategy: %d\n", strategies[i]);
            return result;
        }
    }
    
    // Should never reach here due to plain text fallback
    return (ParseResult){false, 0, NULL};
}
```

### 5.2 Circuit Breaker Pattern
**Principle**: Disable problematic parsing features when they consistently fail.

```cpp
typedef struct {
    int failure_count;
    int failure_threshold;
    bool is_open;  // Circuit is "open" when disabled
    time_t last_failure;
    int cooldown_seconds;
} CircuitBreaker;

bool circuit_breaker_should_parse(CircuitBreaker* cb, const char* feature_name) {
    if (!cb->is_open) {
        return true;  // Circuit closed, feature enabled
    }
    
    // Check if cooldown period has passed
    time_t now = time(NULL);
    if (now - cb->last_failure > cb->cooldown_seconds) {
        cb->is_open = false;  // Reset circuit breaker
        cb->failure_count = 0;
        fprintf(stderr, "Circuit breaker reset for %s\n", feature_name);
        return true;
    }
    
    return false;  // Still in cooldown
}

void circuit_breaker_record_failure(CircuitBreaker* cb, const char* feature_name) {
    cb->failure_count++;
    cb->last_failure = time(NULL);
    
    if (cb->failure_count >= cb->failure_threshold) {
        cb->is_open = true;
        fprintf(stderr, "Circuit breaker opened for %s after %d failures\n", 
                feature_name, cb->failure_count);
    }
}

// Usage in parser:
ParseResult parse_table_with_breaker(const char* input, size_t len) {
    static CircuitBreaker table_breaker = {0, 3, false, 0, 60}; // 3 failures, 60s cooldown
    
    if (!circuit_breaker_should_parse(&table_breaker, "table_parsing")) {
        // Parse table as plain text instead
        return parse_as_plain_text(input, len);
    }
    
    ParseResult result = parse_table_full(input, len);
    
    if (!result.success) {
        circuit_breaker_record_failure(&table_breaker, "table_parsing");
        // Fallback to plain text
        return parse_as_plain_text(input, len);
    }
    
    return result;
}
```

## 6. Monitoring and Observability

### 6.1 Parser Telemetry
**Principle**: Instrument parsers to collect performance and reliability data.

```cpp
typedef struct {
    size_t total_parses;
    size_t successful_parses;
    size_t failed_parses;
    size_t timeout_parses;
    double avg_parse_time_ms;
    double max_parse_time_ms;
    size_t avg_memory_usage;
    size_t max_memory_usage;
} ParserTelemetry;

void record_parse_attempt(ParserTelemetry* telemetry, ParseResult result, 
                         double elapsed_ms, size_t memory_used) {
    telemetry->total_parses++;
    
    if (result.success) {
        telemetry->successful_parses++;
    } else {
        telemetry->failed_parses++;
    }
    
    // Update timing statistics
    telemetry->avg_parse_time_ms = 
        ((telemetry->avg_parse_time_ms * (telemetry->total_parses - 1)) + elapsed_ms) / telemetry->total_parses;
    
    if (elapsed_ms > telemetry->max_parse_time_ms) {
        telemetry->max_parse_time_ms = elapsed_ms;
    }
    
    // Update memory statistics
    telemetry->avg_memory_usage = 
        ((telemetry->avg_memory_usage * (telemetry->total_parses - 1)) + memory_used) / telemetry->total_parses;
    
    if (memory_used > telemetry->max_memory_usage) {
        telemetry->max_memory_usage = memory_used;
    }
}
```

## 7. Implementation Checklist

### Design Phase
- [ ] Define finite state machine with explicit exit conditions
- [ ] Identify all recursive parsing functions
- [ ] Plan resource budgets (time, memory, operations)
- [ ] Design fallback strategies for each parsing feature
- [ ] Define input complexity metrics

### Implementation Phase
- [ ] Add position advancement verification to all loops
- [ ] Implement iteration limits for all parsing loops
- [ ] Convert deep recursion to iterative with explicit stack
- [ ] Add resource usage tracking
- [ ] Implement layered input validation
- [ ] Add telemetry collection points

### Testing Phase
- [ ] Create timeout-based test harness
- [ ] Test with progressively complex inputs
- [ ] Test with malformed inputs
- [ ] Validate fallback strategies work
- [ ] Monitor resource usage during tests
- [ ] Test circuit breaker functionality

### Monitoring Phase
- [ ] Set up parsing performance dashboards
- [ ] Configure alerts for parsing failures
- [ ] Monitor resource usage trends
- [ ] Track fallback strategy usage
- [ ] Review telemetry for optimization opportunities

## Conclusion

Structured parser design prevents hangs through systematic approaches:

1. **Architectural Patterns**: FSM design and bounded contexts ensure controlled parsing
2. **Resource Management**: Budgeting and cooperative parsing prevent resource exhaustion
3. **Progressive Strategies**: Multi-stage and adaptive parsing handle complexity gracefully
4. **Error Recovery**: Circuit breakers and hierarchical fallbacks ensure robustness
5. **Observability**: Telemetry and monitoring enable continuous improvement

This approach transforms parser reliability from reactive bug-fixing to proactive design for robustness.
