# Mathematical Spacing Normalization System

## Current Issues

The current spacing implementation in `format-math.cpp` has several problems:
- Hardcoded arrays for element types (relational_ops, comparison_ops, spacing_cmds, etc.)
- Complex nested conditional logic for spacing decisions
- Inconsistent spacing rules across different mathematical contexts
- No centralized configuration for spacing behavior

## Proposed Config-Driven System

### 1. Spacing Rule Definitions

```c
typedef enum {
    SPACE_NONE = 0,        // No space
    SPACE_THIN = 1,        // \, (0.167em)
    SPACE_MEDIUM = 2,      // \: (0.222em) 
    SPACE_THICK = 3,       // \; (0.278em)
    SPACE_QUAD = 4,        // \quad (1em)
    SPACE_NEGATIVE = -1    // \! (-0.167em)
} SpaceType;

typedef struct {
    const char* element_name;
    SpaceType space_before;
    SpaceType space_after;
    bool inherits_context;    // Whether spacing depends on parent context
} ElementSpacingRule;

typedef struct {
    const char* left_element;
    const char* right_element;
    SpaceType override_space;
    bool is_pattern;          // Whether elements are patterns (e.g., "*_function")
} SpacingPairRule;
```

### 2. Centralized Spacing Configuration

```c
// Element-specific spacing rules
static const ElementSpacingRule element_spacing[] = {
    // Functions
    {"sin", SPACE_NONE, SPACE_THIN, true},
    {"cos", SPACE_NONE, SPACE_THIN, true},
    {"log", SPACE_NONE, SPACE_THIN, true},
    {"*_function", SPACE_NONE, SPACE_THIN, true},  // Pattern for all functions
    
    // Operators
    {"add", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"sub", SPACE_MEDIUM, SPACE_MEDIUM, false},
    {"eq", SPACE_THICK, SPACE_THICK, false},
    {"*_relation", SPACE_THICK, SPACE_THICK, false},  // Pattern for relations
    
    // Integrals
    {"int", SPACE_NONE, SPACE_THIN, true},
    {"iint", SPACE_NONE, SPACE_THIN, true},
    {"*_integral", SPACE_NONE, SPACE_THIN, true},
    
    // Delimiters
    {"abs", SPACE_NONE, SPACE_NONE, false},
    {"paren_group", SPACE_NONE, SPACE_NONE, false},
    
    // Spacing commands
    {"thin_space", SPACE_NONE, SPACE_NONE, false},
    {"med_space", SPACE_NONE, SPACE_NONE, false},
    {"thick_space", SPACE_NONE, SPACE_NONE, false},
    {"neg_space", SPACE_NONE, SPACE_NONE, false},
    
    {NULL, SPACE_NONE, SPACE_NONE, false}
};

// Pair-specific spacing overrides
static const SpacingPairRule pair_spacing[] = {
    // Function applications
    {"*_function", "*", SPACE_THIN, true},
    {"*_function", "*_variable", SPACE_THIN, true},
    
    // Implicit multiplication
    {"*_variable", "*_variable", SPACE_NONE, true},
    {"*_number", "*_variable", SPACE_THIN, true},
    
    // Derivatives and differentials
    {"partial", "*", SPACE_THIN, false},
    {"*", "partial", SPACE_THIN, false},
    
    // Subscripts and superscripts
    {"subscript", "*", SPACE_NONE, false},
    {"pow", "*", SPACE_NONE, false},
    
    // Matrix elements
    {"*", "&", SPACE_MEDIUM, false},
    
    {NULL, NULL, SPACE_NONE, false}
};
```

### 3. Context-Aware Spacing

```c
typedef enum {
    MATH_CONTEXT_NORMAL,
    MATH_CONTEXT_SUBSCRIPT,
    MATH_CONTEXT_SUPERSCRIPT,
    MATH_CONTEXT_FRACTION_NUM,
    MATH_CONTEXT_FRACTION_DEN,
    MATH_CONTEXT_MATRIX,
    MATH_CONTEXT_CASES,
    MATH_CONTEXT_INTEGRAL_BOUNDS
} MathContext;

typedef struct {
    MathContext context;
    SpaceType default_space;
    bool compress_spaces;     // Whether to reduce spacing in this context
    float space_multiplier;   // Scale factor for spaces
} ContextSpacingRule;

static const ContextSpacingRule context_spacing[] = {
    {MATH_CONTEXT_NORMAL, SPACE_NONE, false, 1.0f},
    {MATH_CONTEXT_SUBSCRIPT, SPACE_NONE, true, 0.8f},
    {MATH_CONTEXT_SUPERSCRIPT, SPACE_NONE, true, 0.8f},
    {MATH_CONTEXT_FRACTION_NUM, SPACE_NONE, true, 0.9f},
    {MATH_CONTEXT_FRACTION_DEN, SPACE_NONE, true, 0.9f},
    {MATH_CONTEXT_MATRIX, SPACE_MEDIUM, false, 1.0f},
    {MATH_CONTEXT_CASES, SPACE_MEDIUM, false, 1.0f},
    {MATH_CONTEXT_INTEGRAL_BOUNDS, SPACE_NONE, true, 0.7f}
};
```

### 4. Unified Spacing API

```c
// Main spacing decision function
SpaceType determine_spacing(const char* left_element, const char* right_element, 
                           MathContext context, int depth) {
    // 1. Check pair-specific overrides first
    SpaceType pair_space = lookup_pair_spacing(left_element, right_element);
    if (pair_space != SPACE_NONE) {
        return apply_context_scaling(pair_space, context);
    }
    
    // 2. Check element-specific rules
    ElementSpacingRule* left_rule = lookup_element_spacing(left_element);
    ElementSpacingRule* right_rule = lookup_element_spacing(right_element);
    
    SpaceType left_after = left_rule ? left_rule->space_after : SPACE_NONE;
    SpaceType right_before = right_rule ? right_rule->space_before : SPACE_NONE;
    
    // 3. Apply context rules
    SpaceType result_space = max(left_after, right_before);
    return apply_context_scaling(result_space, context);
}

// Pattern matching for element types
bool matches_pattern(const char* element_name, const char* pattern) {
    if (pattern[0] == '*' && pattern[1] == '_') {
        // Pattern matching: *_function, *_variable, etc.
        const char* suffix = pattern + 2;
        return element_has_category(element_name, suffix);
    }
    return strcmp(element_name, pattern) == 0;
}
```

### 5. Implementation Strategy

#### Phase 1: Extract Current Logic
- Move hardcoded arrays to configuration structures
- Create lookup functions for spacing rules
- Maintain backward compatibility

#### Phase 2: Implement Pattern Matching
- Add element categorization system
- Implement pattern-based rules (*_function, *_relation, etc.)
- Add context detection logic

#### Phase 3: Context-Aware Spacing
- Implement context tracking during formatting
- Add context-specific spacing adjustments
- Handle nested contexts (subscripts in fractions, etc.)

#### Phase 4: Configuration Integration
- Move spacing rules to external configuration
- Add runtime rule modification capability
- Implement spacing rule validation

### 6. Benefits

1. **Maintainability**: Centralized spacing configuration
2. **Consistency**: Uniform spacing rules across all math expressions
3. **Flexibility**: Easy to adjust spacing behavior without code changes
4. **Extensibility**: Pattern-based rules handle new element types automatically
5. **Context Awareness**: Proper spacing in subscripts, fractions, matrices
6. **Performance**: Efficient lookup with O(1) hash-based element categorization

### 7. Migration Path

The new system can be implemented incrementally:
1. Create new spacing API alongside existing code
2. Gradually migrate spacing decisions to new system
3. Remove old hardcoded logic once fully migrated
4. Add configuration file support for external rule definitions

This approach will solve the current spacing inconsistencies in:
- Integral expressions (`\iint_D f(x,y) dA`)
- Function applications (`\cos\theta` vs `\cos \theta`)
- Limit expressions (`f(x+h)` vs `f(x + h)`)
- Spacing commands (`c\:d` should become `c d` with medium space)
