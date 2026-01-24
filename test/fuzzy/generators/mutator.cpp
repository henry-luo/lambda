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
    BOUNDARY_VALUES
};

static const std::vector<std::string> KEYWORDS = {
    "fn", "pn", "let", "var", "if", "else", "for", "in", "while",
    "return", "break", "continue", "type", "import", "pub", "true", "false", "null"
};

static const std::vector<std::string> OPERATORS = {
    "+", "-", "*", "/", "_/", "%", "^",
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

std::string mutate_program(const std::string& input, std::mt19937& rng) {
    // Apply 1-3 random mutations
    int num_mutations = std::uniform_int_distribution<>(1, 3)(rng);
    std::string result = input;
    
    for (int i = 0; i < num_mutations; i++) {
        MutationType type = static_cast<MutationType>(
            std::uniform_int_distribution<>(0, 14)(rng)
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
        }
    }
    
    return result;
}
