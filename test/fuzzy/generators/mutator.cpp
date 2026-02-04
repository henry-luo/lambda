/**
 * Lambda Program Mutator
 * 
 * Applies various mutations to valid Lambda programs for fuzzy testing.
 */

#include <string>
#include <vector>
#include <random>
#include <algorithm>

enum class MutationType {
    DELETE_CHAR,
    INSERT_CHAR,
    SWAP_CHARS,
    DUPLICATE_SECTION,
    DELETE_LINE,
    DUPLICATE_LINE,
    REPLACE_KEYWORD,
    CORRUPT_STRING,
    CORRUPT_NUMBER,
    UNBALANCE_PARENS,
    INSERT_RANDOM_TOKEN,
    FLIP_OPERATOR,
    DEEP_NESTING,
    EMPTY_CONSTRUCTS,
    BOUNDARY_VALUES,
    TYPE_CONFUSION,
    CLOSURE_PATTERN,
    CONTEXT_SENSITIVE,
    INVARIANT_VIOLATION,
    // Type pattern mutations
    TYPE_PATTERN_CORRUPT,
    TYPE_PATTERN_NESTED,
    TYPE_PATTERN_UNION_INTERSECT,
    TYPE_PATTERN_OCCURRENCE,
    TYPE_PATTERN_FUNCTION,
    TYPE_PATTERN_ELEMENT,
    // String pattern integration mutations
    STRING_PATTERN_BASIC,
    STRING_PATTERN_IN_TYPE,
    STRING_PATTERN_COMPLEX
};

static const std::vector<std::string> KEYWORDS = {
    "fn", "pn", "let", "var", "if", "else", "for", "in", "while",
    "return", "break", "continue", "type", "import", "pub", "true", "false", "null"
};

static const std::vector<std::string> OPERATORS = {
    "+", "-", "*", "/", "div", "%", "^",
    "==", "!=", "<", ">", "<=", ">=",
    "and", "or", "not"
};

static const std::vector<std::string> DELIMITERS = {
    "(", ")", "[", "]", "{", "}", "<", ">",
    ";", ":", ",", "."
};

// Delete a random character
static std::string mutate_delete_char(const std::string& input, std::mt19937& rng) {
    if (input.empty()) return input;
    
    size_t pos = std::uniform_int_distribution<size_t>(0, input.size() - 1)(rng);
    std::string result = input;
    result.erase(pos, 1);
    return result;
}

// Insert a random character
static std::string mutate_insert_char(const std::string& input, std::mt19937& rng) {
    size_t pos = std::uniform_int_distribution<size_t>(0, input.size())(rng);
    char c = (char)std::uniform_int_distribution<>(32, 126)(rng);
    
    std::string result = input;
    result.insert(pos, 1, c);
    return result;
}

// Swap two adjacent characters
static std::string mutate_swap_chars(const std::string& input, std::mt19937& rng) {
    if (input.size() < 2) return input;
    
    size_t pos = std::uniform_int_distribution<size_t>(0, input.size() - 2)(rng);
    std::string result = input;
    std::swap(result[pos], result[pos + 1]);
    return result;
}

// Duplicate a section
static std::string mutate_duplicate_section(const std::string& input, std::mt19937& rng) {
    if (input.size() < 2) return input;
    
    size_t start = std::uniform_int_distribution<size_t>(0, input.size() - 1)(rng);
    size_t len = std::uniform_int_distribution<size_t>(1, std::min((size_t)50, input.size() - start))(rng);
    
    std::string section = input.substr(start, len);
    size_t insert_pos = std::uniform_int_distribution<size_t>(0, input.size())(rng);
    
    std::string result = input;
    result.insert(insert_pos, section);
    return result;
}

// Split into lines and operations
static std::vector<std::string> split_lines(const std::string& input) {
    std::vector<std::string> lines;
    std::string current;
    
    for (char c : input) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    
    return lines;
}

static std::string join_lines(const std::vector<std::string>& lines) {
    std::string result;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) result += '\n';
        result += lines[i];
    }
    return result;
}

// Delete a random line
static std::string mutate_delete_line(const std::string& input, std::mt19937& rng) {
    auto lines = split_lines(input);
    if (lines.size() <= 1) return input;
    
    size_t idx = std::uniform_int_distribution<size_t>(0, lines.size() - 1)(rng);
    lines.erase(lines.begin() + idx);
    
    return join_lines(lines);
}

// Duplicate a random line
static std::string mutate_duplicate_line(const std::string& input, std::mt19937& rng) {
    auto lines = split_lines(input);
    if (lines.empty()) return input;
    
    size_t idx = std::uniform_int_distribution<size_t>(0, lines.size() - 1)(rng);
    lines.insert(lines.begin() + idx, lines[idx]);
    
    return join_lines(lines);
}

// Replace a keyword with another keyword
static std::string mutate_replace_keyword(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    // Find a keyword
    for (const auto& kw : KEYWORDS) {
        size_t pos = result.find(kw);
        if (pos != std::string::npos) {
            // Check it's a whole word
            bool before_ok = (pos == 0 || !isalnum(result[pos - 1]));
            bool after_ok = (pos + kw.size() >= result.size() || !isalnum(result[pos + kw.size()]));
            
            if (before_ok && after_ok) {
                // Replace with random keyword
                const std::string& replacement = KEYWORDS[std::uniform_int_distribution<>(0, (int)KEYWORDS.size() - 1)(rng)];
                result.replace(pos, kw.size(), replacement);
                return result;
            }
        }
    }
    
    return result;
}

// Corrupt a string literal
static std::string mutate_corrupt_string(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    // Find a string
    size_t start = result.find('"');
    if (start == std::string::npos) return result;
    
    int mutation = std::uniform_int_distribution<>(0, 4)(rng);
    
    if (mutation == 0) {
        // Remove closing quote
        size_t end = result.find('"', start + 1);
        if (end != std::string::npos) {
            result.erase(end, 1);
        }
    } else if (mutation == 1) {
        // Insert invalid escape
        size_t pos = start + 1;
        result.insert(pos, "\\z");
    } else if (mutation == 2) {
        // Insert null byte representation
        size_t pos = start + 1;
        result.insert(pos, "\\x00");
    } else if (mutation == 3) {
        // Very long string
        size_t end = result.find('"', start + 1);
        if (end != std::string::npos) {
            std::string filler(1000, 'x');
            result.insert(end, filler);
        }
    } else {
        // Invalid unicode
        size_t pos = start + 1;
        result.insert(pos, "\\u{FFFFFF}");
    }
    
    return result;
}

// Corrupt a number
static std::string mutate_corrupt_number(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    // Find a number
    for (size_t i = 0; i < result.size(); i++) {
        if (isdigit(result[i])) {
            int mutation = std::uniform_int_distribution<>(0, 4)(rng);
            
            if (mutation == 0) {
                // Multiple decimal points
                result.insert(i + 1, "...");
            } else if (mutation == 1) {
                // Invalid exponent
                result.insert(i + 1, "e+e-");
            } else if (mutation == 2) {
                // Extremely large number
                result.insert(i + 1, "99999999999999999999999999999999");
            } else if (mutation == 3) {
                // Mixed formats
                result.insert(i + 1, "n.5e2n");
            } else {
                // Leading zeros with invalid suffix
                result.insert(i, "00000");
            }
            return result;
        }
    }
    
    return result;
}

// Unbalance parentheses/brackets
static std::string mutate_unbalance_parens(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    int mutation = std::uniform_int_distribution<>(0, 5)(rng);
    
    if (mutation == 0) {
        // Add extra opening
        size_t pos = std::uniform_int_distribution<size_t>(0, result.size())(rng);
        result.insert(pos, "(");
    } else if (mutation == 1) {
        // Add extra closing
        size_t pos = std::uniform_int_distribution<size_t>(0, result.size())(rng);
        result.insert(pos, ")");
    } else if (mutation == 2) {
        // Mismatched types
        for (size_t i = 0; i < result.size(); i++) {
            if (result[i] == ')') {
                result[i] = ']';
                return result;
            }
        }
    } else if (mutation == 3) {
        // Remove opening
        for (size_t i = 0; i < result.size(); i++) {
            if (result[i] == '(' || result[i] == '[' || result[i] == '{') {
                result.erase(i, 1);
                return result;
            }
        }
    } else if (mutation == 4) {
        // Remove closing
        for (size_t i = result.size(); i > 0; i--) {
            if (result[i-1] == ')' || result[i-1] == ']' || result[i-1] == '}') {
                result.erase(i-1, 1);
                return result;
            }
        }
    } else {
        // Deep nesting
        std::string nested = "((((((((((";
        size_t pos = std::uniform_int_distribution<size_t>(0, result.size())(rng);
        result.insert(pos, nested);
    }
    
    return result;
}

// Insert random token
static std::string mutate_insert_random_token(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    int type = std::uniform_int_distribution<>(0, 2)(rng);
    std::string token;
    
    if (type == 0) {
        token = KEYWORDS[std::uniform_int_distribution<>(0, (int)KEYWORDS.size() - 1)(rng)];
    } else if (type == 1) {
        token = OPERATORS[std::uniform_int_distribution<>(0, (int)OPERATORS.size() - 1)(rng)];
    } else {
        token = DELIMITERS[std::uniform_int_distribution<>(0, (int)DELIMITERS.size() - 1)(rng)];
    }
    
    size_t pos = std::uniform_int_distribution<size_t>(0, result.size())(rng);
    result.insert(pos, " " + token + " ");
    
    return result;
}

// Flip an operator
static std::string mutate_flip_operator(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    for (const auto& op : OPERATORS) {
        size_t pos = result.find(op);
        if (pos != std::string::npos) {
            const std::string& replacement = OPERATORS[std::uniform_int_distribution<>(0, (int)OPERATORS.size() - 1)(rng)];
            result.replace(pos, op.size(), replacement);
            return result;
        }
    }
    
    return result;
}

// Add deep nesting
static std::string mutate_deep_nesting(const std::string& input, std::mt19937& rng) {
    int depth = std::uniform_int_distribution<>(10, 50)(rng);
    std::string prefix, suffix;
    
    int type = std::uniform_int_distribution<>(0, 2)(rng);
    char open, close;
    
    if (type == 0) { open = '('; close = ')'; }
    else if (type == 1) { open = '['; close = ']'; }
    else { open = '{'; close = '}'; }
    
    for (int i = 0; i < depth; i++) {
        prefix += open;
        suffix += close;
    }
    
    return prefix + input + suffix;
}

// Create empty constructs
static std::string mutate_empty_constructs(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> empty_constructs = {
        "[]", "()", "{}", "\"\"", "''",
        "fn f() => null",
        "let x = [];",
        "for (x in []) null",
        "if (true) null else null"
    };
    
    size_t pos = std::uniform_int_distribution<size_t>(0, input.size())(rng);
    const std::string& construct = empty_constructs[std::uniform_int_distribution<>(0, (int)empty_constructs.size() - 1)(rng)];
    
    std::string result = input;
    result.insert(pos, " " + construct + " ");
    return result;
}

// Insert boundary values
static std::string mutate_boundary_values(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> boundary_values = {
        "0", "-0", "2147483647", "-2147483648", "2147483648",
        "9223372036854775807", "-9223372036854775808",
        "inf", "-inf", "nan",
        "1e308", "1e-308", "1e309",
        "0.0", "-0.0",
        "\"\"", "null", "true", "false"
    };
    
    size_t pos = std::uniform_int_distribution<size_t>(0, input.size())(rng);
    const std::string& value = boundary_values[std::uniform_int_distribution<>(0, (int)boundary_values.size() - 1)(rng)];
    
    std::string result = input;
    result.insert(pos, " " + value + " ");
    return result;
}

// Type confusion mutations - replace literals with incompatible types
static std::string mutate_type_confusion(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    // Define replacements for type confusion
    std::vector<std::pair<std::string, std::vector<std::string>>> replacements = {
        {"true", {"false", "null", "1", "\"true\"", "[]", "{}"}},
        {"false", {"true", "null", "0", "\"false\"", "[]", "{}"}},
        {"null", {"0", "false", "\"\"", "[]", "{}"}},
        {"[]", {"{}", "null", "\"\"", "0"}},
        {"{}", {"[]", "null", "\"\"", "0"}},
        {"\"\"", {"null", "0", "false", "[]"}}
    };
    
    for (const auto& pair : replacements) {
        size_t pos = result.find(pair.first);
        if (pos != std::string::npos) {
            const auto& options = pair.second;
            const std::string& replacement = options[
                std::uniform_int_distribution<>(0, (int)options.size() - 1)(rng)
            ];
            result.replace(pos, pair.first.size(), replacement);
            return result;
        }
    }
    
    return result;
}

// Mutate closure patterns - target function definitions
static std::string mutate_closure_pattern(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    // Find "fn " pattern
    size_t fn_pos = result.find("fn ");
    if (fn_pos == std::string::npos) return result;
    
    int mutation = std::uniform_int_distribution<>(0, 5)(rng);
    
    if (mutation == 0) {
        // Duplicate nested fn inside function body
        size_t open_brace = result.find("{", fn_pos);
        if (open_brace != std::string::npos) {
            result.insert(open_brace + 1, " fn inner() => 42; ");
        }
    } else if (mutation == 1) {
        // Add extra parameter
        size_t open_paren = result.find("(", fn_pos);
        size_t close_paren = result.find(")", open_paren);
        if (open_paren != std::string::npos && close_paren != std::string::npos) {
            result.insert(close_paren, ", extra");
        }
    } else if (mutation == 2) {
        // Remove function name
        size_t space = result.find(" ", fn_pos + 3);
        if (space != std::string::npos) {
            size_t paren = result.find("(", space);
            if (paren != std::string::npos && paren - space < 20) {
                result.erase(space + 1, paren - space - 1);
            }
        }
    } else if (mutation == 3) {
        // Change => to invalid syntax
        size_t arrow = result.find("=>", fn_pos);
        if (arrow != std::string::npos) {
            result.replace(arrow, 2, "==");
        }
    } else if (mutation == 4) {
        // Add recursive call without base case
        size_t arrow = result.find("=>", fn_pos);
        if (arrow != std::string::npos) {
            result.insert(arrow + 2, " recurse() + ");
        }
    } else {
        // Wrap closure in expression context
        result.insert(fn_pos, "1 + ");
    }
    
    return result;
}

// Context-sensitive mutations - violate scoping rules
static std::string mutate_context_sensitive(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    int mutation = std::uniform_int_distribution<>(0, 4)(rng);
    
    if (mutation == 0) {
        // Use undefined variable
        result.insert(0, "undefined_var + ");
    } else if (mutation == 1) {
        // Duplicate let declaration
        size_t let_pos = result.find("let ");
        if (let_pos != std::string::npos) {
            size_t newline = result.find("\n", let_pos);
            if (newline != std::string::npos) {
                std::string decl = result.substr(let_pos, newline - let_pos);
                result.insert(newline + 1, decl + "\n");
            }
        }
    } else if (mutation == 2) {
        // Use variable before declaration
        size_t let_pos = result.find("let ");
        if (let_pos != std::string::npos) {
            size_t eq = result.find("=", let_pos);
            if (eq != std::string::npos) {
                std::string var_name = result.substr(let_pos + 4, eq - let_pos - 5);
                result.insert(0, var_name + "\n");
            }
        }
    } else if (mutation == 3) {
        // Reference local variable outside its scope
        size_t open_brace = result.find("{");
        size_t close_brace = result.find("}");
        if (open_brace != std::string::npos && close_brace != std::string::npos) {
            result.insert(close_brace + 1, "\nlocal_var");
        }
    } else {
        // Assign to immutable let binding
        size_t let_pos = result.find("let ");
        if (let_pos != std::string::npos) {
            size_t newline = result.find("\n", let_pos);
            if (newline != std::string::npos) {
                size_t eq = result.find("=", let_pos);
                if (eq != std::string::npos) {
                    std::string var_name = result.substr(let_pos + 4, eq - let_pos - 5);
                    result.insert(newline + 1, var_name + " = 999\n");
                }
            }
        }
    }
    
    return result;
}

// Invariant violation - generate inputs that violate semantic rules
static std::string mutate_invariant_violation(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> violations = {
        // Array invariants
        "let arr = [1, 2]; arr[-arr.length]",
        "let arr = []; arr[0/0]",
        "let arr = [1, 2]; arr[inf]",
        "let arr = [1, 2]; arr[null]",
        
        // Map invariants
        "let m = {}; m[null]",
        "let m = {}; m[{}]",
        "let m = {}; m[[]]",
        
        // Function invariants
        "let f = (x) => x; f()",
        "let f = () => 1; f(1, 2, 3)",
        "let f = (x, y) => x + y; f(1)",
        
        // Type invariants
        "true + false",
        "\"hello\" / \"world\"",
        "[1, 2] * [3, 4]",
        "null * null",
        "true ^ false",
        
        // Division by zero
        "5 / 0",
        "10 % 0",
        
        // Recursive without base
        "fn loop(n) => loop(n); loop(1)",
        
        // String operations on non-strings
        "123.length",
        "true.split()",
        "null.trim()"
    };
    
    const std::string& violation = violations[
        std::uniform_int_distribution<>(0, (int)violations.size() - 1)(rng)
    ];
    
    return input + "\n" + violation;
}

// Type pattern corruption - corrupt type declarations
static std::string mutate_type_pattern_corrupt(const std::string& input, std::mt19937& rng) {
    std::string result = input;
    
    int mutation = std::uniform_int_distribution<>(0, 9)(rng);
    
    if (mutation == 0) {
        // Invalid type name (starts with digit)
        result += "\ntype 123Invalid = int";
    } else if (mutation == 1) {
        // Missing type definition
        result += "\ntype EmptyType";
    } else if (mutation == 2) {
        // Circular type reference
        result += "\ntype A = B\ntype B = A";
    } else if (mutation == 3) {
        // Invalid occurrence operator position
        result += "\ntype BadOccur = *int";
    } else if (mutation == 4) {
        // Multiple occurrence operators
        result += "\ntype MultiOccur = [int*+?]";
    } else if (mutation == 5) {
        // Unbalanced brackets in type
        result += "\ntype Unbalanced = [int";
    } else if (mutation == 6) {
        // Empty union/intersection
        result += "\ntype EmptyUnion = |";
    } else if (mutation == 7) {
        // Dangling operator
        result += "\ntype Dangling = int |";
    } else if (mutation == 8) {
        // Invalid map field syntax
        result += "\ntype BadMap = {: int}";
    } else {
        // Type keyword as type name
        result += "\ntype type = string";
    }
    
    return result;
}

// Deeply nested type patterns
static std::string mutate_type_pattern_nested(const std::string& input, std::mt19937& rng) {
    int depth = std::uniform_int_distribution<>(5, 20)(rng);
    int type_kind = std::uniform_int_distribution<>(0, 4)(rng);
    
    std::string type_expr;
    std::string suffix;
    
    if (type_kind == 0) {
        // Deep array nesting
        for (int i = 0; i < depth; i++) type_expr += "[";
        type_expr += "int";
        for (int i = 0; i < depth; i++) type_expr += "]";
    } else if (type_kind == 1) {
        // Deep map nesting
        for (int i = 0; i < depth; i++) type_expr += "{x" + std::to_string(i) + ": ";
        type_expr += "int";
        for (int i = 0; i < depth; i++) type_expr += "}";
    } else if (type_kind == 2) {
        // Deep optional nesting
        type_expr = "string";
        for (int i = 0; i < depth; i++) type_expr += "?";
    } else if (type_kind == 3) {
        // Deep tuple nesting
        for (int i = 0; i < depth; i++) type_expr += "(int, ";
        type_expr += "string";
        for (int i = 0; i < depth; i++) type_expr += ")";
    } else {
        // Deep element nesting
        for (int i = 0; i < depth; i++) type_expr += "<div; ";
        type_expr += "string";
        for (int i = 0; i < depth; i++) type_expr += ">";
    }
    
    return input + "\ntype DeepNested" + std::to_string(std::uniform_int_distribution<>(0, 999)(rng)) + 
           " = " + type_expr;
}

// Union and intersection type mutations
static std::string mutate_type_pattern_union_intersect(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> type_patterns = {
        // Long union chains
        "type LongUnion = int | string | bool | null | float | int64 | decimal | datetime | symbol | binary",
        // Mixed union and intersection
        "type MixedOps = (int | string) & (bool | null)",
        // Conflicting intersection
        "type ConflictIntersect = {a: int} & {a: string}",
        // Union of complex types
        "type ComplexUnion = [int*] | {x: string} | <div; string>",
        // Nested union
        "type NestedUnion = ((int | string) | (bool | null)) | float",
        // Union with optional
        "type UnionOptional = (int? | string?)? | null",
        // Repeated union members
        "type RepeatedUnion = int | int | string | string",
        // Empty type in union
        "type EmptyInUnion = int | {} | []",
        // Function union
        "type FuncUnion = fn(int) int | fn(string) string",
        // Intersection of incompatible
        "type IncompatIntersect = int & string",
        // Triple operator chain
        "type TripleChain = int | string & bool | null",
        // Parenthesized vs non-parenthesized
        "type ParenDiff = int | (string & bool)",
        // Union of recursive types
        "type RecursiveUnion = {left: RecursiveUnion?} | {right: RecursiveUnion?}"
    };
    
    return input + "\n" + type_patterns[std::uniform_int_distribution<>(0, (int)type_patterns.size() - 1)(rng)];
}

// Occurrence operator mutations
static std::string mutate_type_pattern_occurrence(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> patterns = {
        // Various occurrence combos
        "type Occur1 = [int*]",
        "type Occur2 = [int+]",
        "type Occur3 = [int?]",
        "type Occur4 = [int*]*",
        "type Occur5 = [int+]+",
        "type Occur6 = [int?]?",
        "type Occur7 = [[int*]+]?",
        "type Occur8 = [string*]* | [int+]+",
        // Optional on non-array
        "type OptPrim = int?",
        "type OptMap = {x: int}?",
        "type OptFunc = fn(int) int?",
        // Occurrence in map values
        "type MapOccur = {items: [int*], required: [string+]}",
        // Occurrence in element content
        "type ElmtOccur = <ul; <li; string>*>",
        // Multiple nested occurrences
        "type MultiNest = [[[int?]*]+]?",
        // Occurrence on union
        "type OccurUnion = (int | string)*",
        // Complex combination
        "type Complex = {data: [({id: int, name: string?})*]+}"
    };
    
    return input + "\n" + patterns[std::uniform_int_distribution<>(0, (int)patterns.size() - 1)(rng)];
}

// Function type mutations
static std::string mutate_type_pattern_function(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> patterns = {
        // Basic function types
        "type F1 = fn() int",
        "type F2 = fn(int) string",
        "type F3 = fn(int, string) bool",
        // Optional parameters
        "type F4 = fn(int, string?) int",
        "type F5 = fn(a?: int) int",
        // Variadic
        "type F6 = fn(int, ...) int",
        // Function returning function
        "type F7 = fn(int) fn(string) bool",
        "type F8 = fn(fn(int) int) fn(int) int",
        // Array of functions
        "type F9 = [fn(int) int*]",
        // Map with function values
        "type F10 = {handler: fn(string) int, validator: fn(int) bool}",
        // Function with complex param types
        "type F11 = fn({x: int, y: string}) [int*]",
        // Function with union return
        "type F12 = fn(int) (string | int | null)",
        // Deeply nested function types
        "type F13 = fn(fn(fn(int) int) int) fn(fn(int) int) int",
        // Function type with named params
        "type F14 = fn(x: int, y: string, z: bool?) int",
        // Void/null returning
        "type F15 = fn(int) null",
        // Function accepting function array
        "type F16 = fn([fn(int) int*]) int"
    };
    
    return input + "\n" + patterns[std::uniform_int_distribution<>(0, (int)patterns.size() - 1)(rng)];
}

// Element type mutations
static std::string mutate_type_pattern_element(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> patterns = {
        // Basic elements
        "type E1 = <div>",
        "type E2 = <span class: string>",
        "type E3 = <p; string>",
        // Element with multiple attributes
        "type E4 = <a href: string, target: string?, title: string?>",
        // Element with typed content
        "type E5 = <article; <header; string>, <section; string>*>",
        // Self-closing element
        "type E6 = <br>",
        "type E7 = <img src: string, alt: string?>",
        // Nested elements
        "type E8 = <html; <head; <title; string>>, <body; <div; string>*>>",
        // Element with mixed content
        "type E9 = <p; string | <span; string> | <a href: string; string>>*",
        // Element with occurrence
        "type E10 = <ul; <li; string>+>",
        // Complex document structure
        "type E11 = <doc; <meta name: string, content: string;>*, <body; string>>",
        // Element with array attribute
        "type E12 = <select options: [string*]>",
        // Recursive element
        "type E13 = <tree; <node value: int; E13*>>",
        // Element union
        "type E14 = <div> | <span> | <p>",
        // Empty vs content elements
        "type E15 = <section; <header>?, string*, <footer>?>",
        // Element with all attribute types
        "type E16 = <data id: int, name: string, active: bool, score: float?>"
    };
    
    return input + "\n" + patterns[std::uniform_int_distribution<>(0, (int)patterns.size() - 1)(rng)];
}

// Basic string pattern mutations
static std::string mutate_string_pattern_basic(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> patterns = {
        // Basic pattern definitions
        R"(string digit = "0" to "9")",
        R"(string letter = "a" to "z" | "A" to "Z")",
        R"(string alphanumeric = ("a" to "z" | "A" to "Z" | "0" to "9")+)",
        R"(string identifier = ("a" to "z" | "_") ("a" to "z" | "0" to "9" | "_")*)",
        // Format patterns
        R"(string email = ("a" to "z")+ "@" ("a" to "z")+ "." ("a" to "z")+)",
        R"(string phone = ("0" to "9")[3] "-" ("0" to "9")[3] "-" ("0" to "9")[4])",
        R"(string date = ("0" to "9")[4] "-" ("0" to "9")[2] "-" ("0" to "9")[2])",
        R"(string url = ("a" to "z")+ "://" ("a" to "z" | ".")+ ("/" ("a" to "z")*)*)",
        // Character classes
        R"(string ws = " " | "\t" | "\n")",
        R"(string hex = "0" to "9" | "a" to "f" | "A" to "F")",
        // Occurrence operators
        R"(string digits = ("0" to "9")+)",
        R"(string opt_sign = ("+" | "-")?)",
        R"(string word = ("a" to "z")[1, 20])",
        R"(string code = ("A" to "Z")[3+])",
        // Pattern composition
        R"(string d = "0" to "9"
string num = d+)",
        R"(string l = "a" to "z"
string word = l+)"
    };
    
    return input + "\n" + patterns[std::uniform_int_distribution<>(0, (int)patterns.size() - 1)(rng)];
}

// String pattern used in type patterns
static std::string mutate_string_pattern_in_type(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> patterns = {
        // Pattern as field type
        R"(string email = ("a" to "z")+ "@" ("a" to "z")+ "." ("a" to "z")+
type User = {name: string, email: email})",
        // Pattern in nested type
        R"(string phone = ("0" to "9")[3] "-" ("0" to "9")[4]
type Contact = {name: string, phones: [phone*]})",
        // Optional pattern field
        R"(string url = ("a" to "z")+ "://" ("a" to "z")+
type Profile = {name: string, website: url?})",
        // Pattern in union type
        R"(string email = ("a" to "z")+ "@" ("a" to "z")+
string phone = ("0" to "9")+
type ContactMethod = email | phone)",
        // Pattern in element type
        R"(string url = ("a" to "z")+ "://" ("a" to "z")+
type Link = <a href: url; string>)",
        // Multiple patterns in schema
        R"(string email = ("a" to "z")+ "@" ("a" to "z")+
string date = ("0" to "9")[4] "-" ("0" to "9")[2] "-" ("0" to "9")[2]
type Record = {email: email, created: date, modified: date?})",
        // Pattern in deeply nested schema
        R"(string id = ("a" to "z" | "0" to "9")+
type Nested = {data: {items: [{id: id, name: string}*]}})",
        // Pattern array types
        R"(string tag = ("a" to "z")+
type TagList = [tag+])",
        // Pattern with occurrence in type
        R"(string code = ("A" to "Z")[3]
type Codes = [code*])"
    };
    
    return input + "\n" + patterns[std::uniform_int_distribution<>(0, (int)patterns.size() - 1)(rng)];
}

// Complex string pattern integration
static std::string mutate_string_pattern_complex(const std::string& input, std::mt19937& rng) {
    std::vector<std::string> patterns = {
        // String enum vs string pattern distinction
        R"(// String enum (exact values)
type Status = "active" | "inactive"
// String pattern (format constraint)
string code = ("A" to "Z")[3] ("0" to "9")[3]
type Record = {status: Status, code: code})",
        // Pattern is check
        R"(string email = ("a" to "z")+ "@" ("a" to "z")+
"test@example" is email)",
        // Pattern with special chars
        R"(string quoted = '"' ("a" to "z" | " ")* '"'
type Config = {value: quoted})",
        // Deeply composed patterns
        R"(string d = "0" to "9"
string l = "a" to "z"
string an = d | l
string id = l an*
type Entity = {id: id})",
        // Pattern in function type
        R"(string email = ("a" to "z")+ "@" ("a" to "z")+
type EmailValidator = fn(email) bool)",
        // Pattern intersection/union edge case
        R"(string hex = "0" to "9" | "a" to "f"
string upper_hex = "0" to "9" | "A" to "F"
type HexValue = {lower: [hex*], upper: [upper_hex*]})",
        // Real-world schema with patterns
        R"(string email = ("a" to "z" | "0" to "9" | "." | "_")+ "@" ("a" to "z" | "0" to "9")+ ("." ("a" to "z")+)+
string phone = ("0" to "9")[3] "-" ("0" to "9")[3] "-" ("0" to "9")[4]
string zip = ("0" to "9")[5] ("-" ("0" to "9")[4])?
string url = ("a" to "z")+ "://" ("a" to "z" | "0" to "9" | "." | "/" | "-")+
type Customer = {
    name: string,
    email: email,
    phone: phone?,
    address: {street: string, city: string, zip: zip},
    website: url?
})",
        // Invalid patterns (error handling test)
        R"(string bad = "a" to)",  // incomplete
        R"(string empty = )",  // missing pattern
        R"(type T = {f: undefined_pattern})"  // reference to undefined pattern
    };
    
    return input + "\n" + patterns[std::uniform_int_distribution<>(0, (int)patterns.size() - 1)(rng)];
}

std::string mutate_program(const std::string& input, std::mt19937& rng) {
    // Apply 1-3 random mutations
    int num_mutations = std::uniform_int_distribution<>(1, 3)(rng);
    std::string result = input;
    
    for (int i = 0; i < num_mutations; i++) {
        MutationType type = static_cast<MutationType>(
            std::uniform_int_distribution<>(0, 27)(rng)  // 28 mutation types (0-27)
        );
        
        switch (type) {
            case MutationType::DELETE_CHAR:
                result = mutate_delete_char(result, rng);
                break;
            case MutationType::INSERT_CHAR:
                result = mutate_insert_char(result, rng);
                break;
            case MutationType::SWAP_CHARS:
                result = mutate_swap_chars(result, rng);
                break;
            case MutationType::DUPLICATE_SECTION:
                result = mutate_duplicate_section(result, rng);
                break;
            case MutationType::DELETE_LINE:
                result = mutate_delete_line(result, rng);
                break;
            case MutationType::DUPLICATE_LINE:
                result = mutate_duplicate_line(result, rng);
                break;
            case MutationType::REPLACE_KEYWORD:
                result = mutate_replace_keyword(result, rng);
                break;
            case MutationType::CORRUPT_STRING:
                result = mutate_corrupt_string(result, rng);
                break;
            case MutationType::CORRUPT_NUMBER:
                result = mutate_corrupt_number(result, rng);
                break;
            case MutationType::UNBALANCE_PARENS:
                result = mutate_unbalance_parens(result, rng);
                break;
            case MutationType::INSERT_RANDOM_TOKEN:
                result = mutate_insert_random_token(result, rng);
                break;
            case MutationType::FLIP_OPERATOR:
                result = mutate_flip_operator(result, rng);
                break;
            case MutationType::DEEP_NESTING:
                result = mutate_deep_nesting(result, rng);
                break;
            case MutationType::EMPTY_CONSTRUCTS:
                result = mutate_empty_constructs(result, rng);
                break;
            case MutationType::BOUNDARY_VALUES:
                result = mutate_boundary_values(result, rng);
                break;
            case MutationType::TYPE_CONFUSION:
                result = mutate_type_confusion(result, rng);
                break;
            case MutationType::CLOSURE_PATTERN:
                result = mutate_closure_pattern(result, rng);
                break;
            case MutationType::CONTEXT_SENSITIVE:
                result = mutate_context_sensitive(result, rng);
                break;
            case MutationType::INVARIANT_VIOLATION:
                result = mutate_invariant_violation(result, rng);
                break;
            case MutationType::TYPE_PATTERN_CORRUPT:
                result = mutate_type_pattern_corrupt(result, rng);
                break;
            case MutationType::TYPE_PATTERN_NESTED:
                result = mutate_type_pattern_nested(result, rng);
                break;
            case MutationType::TYPE_PATTERN_UNION_INTERSECT:
                result = mutate_type_pattern_union_intersect(result, rng);
                break;
            case MutationType::TYPE_PATTERN_OCCURRENCE:
                result = mutate_type_pattern_occurrence(result, rng);
                break;
            case MutationType::TYPE_PATTERN_FUNCTION:
                result = mutate_type_pattern_function(result, rng);
                break;
            case MutationType::TYPE_PATTERN_ELEMENT:
                result = mutate_type_pattern_element(result, rng);
                break;
            case MutationType::STRING_PATTERN_BASIC:
                result = mutate_string_pattern_basic(result, rng);
                break;
            case MutationType::STRING_PATTERN_IN_TYPE:
                result = mutate_string_pattern_in_type(result, rng);
                break;
            case MutationType::STRING_PATTERN_COMPLEX:
                result = mutate_string_pattern_complex(result, rng);
                break;
        }
    }
    
    return result;
}
