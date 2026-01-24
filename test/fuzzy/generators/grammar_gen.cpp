/**
 * Lambda Grammar-Based Test Generator
 * 
 * Generates random Lambda code based on grammar rules.
 */

#include <string>
#include <vector>
#include <random>

// Token categories
enum class TokenType {
    KEYWORD,
    OPERATOR,
    LITERAL_INT,
    LITERAL_FLOAT,
    LITERAL_STRING,
    LITERAL_SYMBOL,
    LITERAL_BINARY,
    LITERAL_DATETIME,
    DELIMITER,
    IDENTIFIER,
    WHITESPACE,
    COMMENT,
    NEWLINE
};

static const std::vector<std::string> KEYWORDS = {
    "fn", "pn", "let", "var", "if", "else", "for", "in", "while",
    "return", "break", "continue", "type", "import", "pub", "true", "false", "null"
};

static const std::vector<std::string> OPERATORS = {
    "+", "-", "*", "/", "_/", "%", "^",
    "==", "!=", "<", ">", "<=", ">=",
    "and", "or", "not",
    "is", "in", "to",
    "|", "&", "!"
};

static const std::vector<std::string> DELIMITERS = {
    "(", ")", "[", "]", "{", "}", "<", ">",
    ";", ":", ",", ".", "=>", "->", "?", "="
};

static std::string random_identifier(std::mt19937& rng) {
    static const char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    static const char* chars_num = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
    
    int len = std::uniform_int_distribution<>(1, 15)(rng);
    std::string result;
    result += chars[std::uniform_int_distribution<>(0, 52)(rng)];
    for (int i = 1; i < len; i++) {
        result += chars_num[std::uniform_int_distribution<>(0, 62)(rng)];
    }
    return result;
}

static std::string random_int(std::mt19937& rng) {
    int type = std::uniform_int_distribution<>(0, 10)(rng);
    
    if (type == 0) return "0";
    if (type == 1) return "-0";
    if (type == 2) return "inf";
    if (type == 3) return "-inf";
    if (type == 4) return "nan";
    
    // Regular integer
    int64_t val = std::uniform_int_distribution<int64_t>(-1000000, 1000000)(rng);
    return std::to_string(val);
}

static std::string random_float(std::mt19937& rng) {
    int type = std::uniform_int_distribution<>(0, 5)(rng);
    
    if (type == 0) {
        // Scientific notation
        double mantissa = std::uniform_real_distribution<>(-10.0, 10.0)(rng);
        int exp = std::uniform_int_distribution<>(-20, 20)(rng);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.3fe%d", mantissa, exp);
        return buf;
    }
    
    // Regular float
    double val = std::uniform_real_distribution<>(-1000.0, 1000.0)(rng);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.6f", val);
    return buf;
}

static std::string random_string(std::mt19937& rng) {
    int len = std::uniform_int_distribution<>(0, 30)(rng);
    std::string result = "\"";
    
    for (int i = 0; i < len; i++) {
        int c = std::uniform_int_distribution<>(0, 100)(rng);
        if (c < 90) {
            // Regular printable ASCII
            result += (char)std::uniform_int_distribution<>(32, 126)(rng);
        } else if (c < 95) {
            // Escape sequences
            static const char* escapes[] = {"\\n", "\\t", "\\r", "\\\\", "\\\""};
            result += escapes[std::uniform_int_distribution<>(0, 4)(rng)];
        } else {
            // Unicode
            result += "\\u{";
            result += std::to_string(std::uniform_int_distribution<>(0, 0xFFFF)(rng));
            result += "}";
        }
    }
    
    result += "\"";
    return result;
}

static std::string random_symbol(std::mt19937& rng) {
    return "'" + random_identifier(rng);
}

static std::string random_binary(std::mt19937& rng) {
    int type = std::uniform_int_distribution<>(0, 1)(rng);
    std::string result = "b'";
    
    if (type == 0) {
        // Hex
        result += "\\x";
        int len = std::uniform_int_distribution<>(1, 8)(rng);
        static const char* hex = "0123456789ABCDEF";
        for (int i = 0; i < len; i++) {
            result += hex[std::uniform_int_distribution<>(0, 15)(rng)];
        }
    } else {
        // Base64
        result += "\\64";
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int len = std::uniform_int_distribution<>(1, 16)(rng);
        for (int i = 0; i < len; i++) {
            result += b64[std::uniform_int_distribution<>(0, 63)(rng)];
        }
        // Padding
        int pad = len % 4;
        for (int i = 0; i < pad; i++) result += "=";
    }
    
    result += "'";
    return result;
}

static std::string random_datetime(std::mt19937& rng) {
    int type = std::uniform_int_distribution<>(0, 2)(rng);
    std::string result = "t'";
    
    int year = std::uniform_int_distribution<>(1900, 2100)(rng);
    int month = std::uniform_int_distribution<>(1, 12)(rng);
    int day = std::uniform_int_distribution<>(1, 28)(rng);
    int hour = std::uniform_int_distribution<>(0, 23)(rng);
    int min = std::uniform_int_distribution<>(0, 59)(rng);
    int sec = std::uniform_int_distribution<>(0, 59)(rng);
    
    char buf[64];
    if (type == 0) {
        // Date only
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    } else if (type == 1) {
        // Time only
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, min, sec);
    } else {
        // Full datetime
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour, min, sec);
    }
    
    result += buf;
    result += "'";
    return result;
}

static std::string random_whitespace(std::mt19937& rng) {
    int count = std::uniform_int_distribution<>(1, 3)(rng);
    std::string result;
    for (int i = 0; i < count; i++) {
        int type = std::uniform_int_distribution<>(0, 2)(rng);
        if (type == 0) result += " ";
        else if (type == 1) result += "\t";
        else result += "\n";
    }
    return result;
}

static std::string random_comment(std::mt19937& rng) {
    std::string result = "// ";
    int len = std::uniform_int_distribution<>(0, 30)(rng);
    for (int i = 0; i < len; i++) {
        result += (char)std::uniform_int_distribution<>(32, 126)(rng);
    }
    return result;
}

static std::string random_token(std::mt19937& rng) {
    int type = std::uniform_int_distribution<>(0, 100)(rng);
    
    if (type < 15) {
        // Keyword
        return KEYWORDS[std::uniform_int_distribution<>(0, (int)KEYWORDS.size() - 1)(rng)];
    } else if (type < 30) {
        // Operator
        return OPERATORS[std::uniform_int_distribution<>(0, (int)OPERATORS.size() - 1)(rng)];
    } else if (type < 40) {
        // Integer
        return random_int(rng);
    } else if (type < 45) {
        // Float
        return random_float(rng);
    } else if (type < 55) {
        // String
        return random_string(rng);
    } else if (type < 60) {
        // Symbol
        return random_symbol(rng);
    } else if (type < 62) {
        // Binary
        return random_binary(rng);
    } else if (type < 64) {
        // DateTime
        return random_datetime(rng);
    } else if (type < 80) {
        // Delimiter
        return DELIMITERS[std::uniform_int_distribution<>(0, (int)DELIMITERS.size() - 1)(rng)];
    } else if (type < 95) {
        // Identifier
        return random_identifier(rng);
    } else if (type < 98) {
        // Whitespace
        return random_whitespace(rng);
    } else {
        // Comment
        return random_comment(rng);
    }
}

std::string generate_random_tokens(std::mt19937& rng, int length) {
    std::string result;
    
    for (int i = 0; i < length; i++) {
        if (i > 0 && std::uniform_int_distribution<>(0, 3)(rng) == 0) {
            result += " ";
        }
        result += random_token(rng);
    }
    
    return result;
}

// Generate a structurally valid (but possibly semantically invalid) expression
std::string generate_expression(std::mt19937& rng, int depth = 0);
std::string generate_statement(std::mt19937& rng, int depth = 0);

std::string generate_expression(std::mt19937& rng, int depth) {
    if (depth > 5) {
        // Base case: simple literal or identifier
        int type = std::uniform_int_distribution<>(0, 3)(rng);
        if (type == 0) return random_int(rng);
        if (type == 1) return random_float(rng);
        if (type == 2) return random_string(rng);
        return random_identifier(rng);
    }
    
    int type = std::uniform_int_distribution<>(0, 20)(rng);
    
    if (type < 5) {
        // Binary operation
        std::string op = OPERATORS[std::uniform_int_distribution<>(0, 6)(rng)]; // Arithmetic only
        return "(" + generate_expression(rng, depth + 1) + " " + op + " " + generate_expression(rng, depth + 1) + ")";
    } else if (type < 8) {
        // Function call
        return random_identifier(rng) + "(" + generate_expression(rng, depth + 1) + ")";
    } else if (type < 10) {
        // Array literal
        int count = std::uniform_int_distribution<>(0, 3)(rng);
        std::string result = "[";
        for (int i = 0; i < count; i++) {
            if (i > 0) result += ", ";
            result += generate_expression(rng, depth + 1);
        }
        result += "]";
        return result;
    } else if (type < 12) {
        // If expression
        return "if (" + generate_expression(rng, depth + 1) + ") " + 
               generate_expression(rng, depth + 1) + " else " + 
               generate_expression(rng, depth + 1);
    } else if (type < 14) {
        // For expression
        return "for (" + random_identifier(rng) + " in " + generate_expression(rng, depth + 1) + ") " +
               generate_expression(rng, depth + 1);
    } else if (type < 16) {
        // Map literal
        int count = std::uniform_int_distribution<>(0, 3)(rng);
        std::string result = "{";
        for (int i = 0; i < count; i++) {
            if (i > 0) result += ", ";
            result += random_identifier(rng) + ": " + generate_expression(rng, depth + 1);
        }
        result += "}";
        return result;
    } else {
        // Simple literal
        int lit = std::uniform_int_distribution<>(0, 5)(rng);
        if (lit == 0) return random_int(rng);
        if (lit == 1) return random_float(rng);
        if (lit == 2) return random_string(rng);
        if (lit == 3) return random_symbol(rng);
        if (lit == 4) return "true";
        return random_identifier(rng);
    }
}

std::string generate_statement(std::mt19937& rng, int depth) {
    if (depth > 3) {
        return "let " + random_identifier(rng) + " = " + generate_expression(rng, 0) + ";";
    }
    
    int type = std::uniform_int_distribution<>(0, 10)(rng);
    
    if (type < 3) {
        // Let statement
        return "let " + random_identifier(rng) + " = " + generate_expression(rng, 0) + ";";
    } else if (type < 5) {
        // Function definition
        std::string params;
        int param_count = std::uniform_int_distribution<>(0, 3)(rng);
        for (int i = 0; i < param_count; i++) {
            if (i > 0) params += ", ";
            params += random_identifier(rng);
        }
        return "fn " + random_identifier(rng) + "(" + params + ") => " + generate_expression(rng, 0) + ";";
    } else if (type < 7) {
        // If statement
        return "if (" + generate_expression(rng, 0) + ") { " + generate_statement(rng, depth + 1) + " }";
    } else if (type < 9) {
        // For statement
        return "for " + random_identifier(rng) + " in " + generate_expression(rng, 0) + " { " + 
               generate_statement(rng, depth + 1) + " }";
    } else {
        // Expression statement
        return generate_expression(rng, 0) + ";";
    }
}

std::string generate_valid_program(std::mt19937& rng, int statement_count) {
    std::string result;
    
    for (int i = 0; i < statement_count; i++) {
        result += generate_statement(rng, 0) + "\n";
    }
    
    return result;
}
