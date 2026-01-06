// math_node.hpp - Math node type definitions for LaTeX math parsing
//
// This file defines the semantic structures for representing parsed LaTeX math.
// Math nodes are Lambda elements that form a tree, later converted to MathBox
// trees during layout.

#ifndef LAMBDA_MATH_NODE_HPP
#define LAMBDA_MATH_NODE_HPP

#include "lambda-data.hpp"
#include "mark_builder.hpp"

namespace lambda {

// ============================================================================
// Atom Types (TeXBook classification)
// Used for inter-box spacing during layout
// ============================================================================

enum class MathAtomType {
    Ord = 0,      // Ordinary: variables, constants, Greek letters
    Op = 1,       // Large operators: \sum, \int, \prod
    Bin = 2,      // Binary operators: +, -, \times
    Rel = 3,      // Relations: =, <, \leq
    Open = 4,     // Opening delimiters: (, [, \{
    Close = 5,    // Closing delimiters: ), ], \}
    Punct = 6,    // Punctuation: ,
    Inner = 7,    // Fractions, delimited subformulas
};

inline const char* math_atom_type_name(MathAtomType t) {
    static const char* names[] = {
        "ord", "op", "bin", "rel", "open", "close", "punct", "inner"
    };
    return names[(int)t];
}

// ============================================================================
// Node Types (structural)
// Determines which layout algorithm to use
// ============================================================================

enum class MathNodeType {
    // Atomic elements
    Symbol,       // Single character: a, b, x, y
    Number,       // Numeric literal: 123, 3.14
    Command,      // Command symbol: \alpha, \times, etc.
    
    // Structural elements
    Group,        // Grouping braces: {expr}
    Row,          // Horizontal sequence of expressions
    Subsup,       // Subscript/superscript: x_1^2
    Fraction,     // Fractions: \frac{a}{b}
    Binomial,     // Binomial: \binom{n}{k}
    Radical,      // Square root: \sqrt{x}, \sqrt[n]{x}
    Delimiter,    // Delimited group: \left( ... \right)
    Accent,       // Accents: \hat{x}, \vec{v}
    BigOperator,  // Large operator with limits: \sum_{i=1}^{n}
    Array,        // Matrix/array environments
    
    // Special elements
    Text,         // Text mode: \text{hello}
    Style,        // Style change: \mathbf{x}, \displaystyle
    Space,        // Explicit spacing: \quad, \,
    Error,        // Parse error
};

inline const char* math_node_type_name(MathNodeType t) {
    static const char* names[] = {
        "symbol", "number", "command",
        "group", "row", "subsup", "frac", "binom", "radical",
        "delimiter", "accent", "bigop", "array",
        "text", "style", "space", "error"
    };
    return names[(int)t];
}

// ============================================================================
// MathNodeBuilder - Helper for building math node trees
// ============================================================================

class MathNodeBuilder {
public:
    MathNodeBuilder(Input* input) : builder(input) {}
    
    // Create a symbol node (single character)
    Item symbol(const char* value, MathAtomType atom_type = MathAtomType::Ord) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("symbol"));
        mb.put("value", builder.createStringItem(value));
        mb.put("atom", builder.createSymbolItem(math_atom_type_name(atom_type)));
        return mb.final();
    }
    
    // Create a symbol node from a single character
    Item symbol(char c, MathAtomType atom_type = MathAtomType::Ord) {
        char buf[2] = {c, 0};
        return symbol(buf, atom_type);
    }
    
    // Create a number node
    Item number(const char* value) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("number"));
        mb.put("value", builder.createStringItem(value));
        mb.put("atom", builder.createSymbolItem("ord"));
        return mb.final();
    }
    
    // Create a command node (resolved symbol or operator)
    Item command(const char* cmd, int codepoint, MathAtomType atom_type) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("command"));
        mb.put("cmd", builder.createStringItem(cmd));
        mb.put("codepoint", builder.createInt(codepoint));
        mb.put("atom", builder.createSymbolItem(math_atom_type_name(atom_type)));
        return mb.final();
    }
    
    // Create a row (horizontal sequence)
    Item row(Item* items, int count) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("row"));
        
        ListBuilder lb = builder.list();
        for (int i = 0; i < count; i++) {
            lb.push(items[i]);
        }
        mb.put("items", lb.final());
        
        return mb.final();
    }
    
    // Create a group node
    Item group(Item content) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("group"));
        mb.put("content", content);
        return mb.final();
    }
    
    // Create a subscript/superscript node
    Item subsup(Item base, Item sub, Item sup) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("subsup"));
        mb.put("base", base);
        if (sub.item != ItemNull.item) mb.put("sub", sub);
        if (sup.item != ItemNull.item) mb.put("sup", sup);
        return mb.final();
    }
    
    // Create a fraction node
    Item fraction(Item numer, Item denom, const char* cmd = "\\frac") {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("frac"));
        mb.put("cmd", builder.createStringItem(cmd));
        mb.put("numer", numer);
        mb.put("denom", denom);
        mb.put("atom", builder.createSymbolItem("inner"));
        return mb.final();
    }
    
    // Create a binomial node
    Item binomial(Item top, Item bottom, const char* cmd = "\\binom") {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("binom"));
        mb.put("cmd", builder.createStringItem(cmd));
        mb.put("top", top);
        mb.put("bottom", bottom);
        mb.put("atom", builder.createSymbolItem("inner"));
        return mb.final();
    }
    
    // Create a radical node
    Item radical(Item radicand, Item index = ItemNull) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("radical"));
        mb.put("radicand", radicand);
        if (index.item != ItemNull.item) mb.put("index", index);
        mb.put("atom", builder.createSymbolItem("ord"));
        return mb.final();
    }
    
    // Create a delimiter group node
    Item delimiter(const char* left, const char* right, Item content) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("delimiter"));
        mb.put("left", builder.createStringItem(left));
        mb.put("right", builder.createStringItem(right));
        mb.put("content", content);
        mb.put("atom", builder.createSymbolItem("inner"));
        return mb.final();
    }
    
    // Create an accent node
    Item accent(const char* cmd, Item base) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("accent"));
        mb.put("cmd", builder.createStringItem(cmd));
        mb.put("base", base);
        mb.put("atom", builder.createSymbolItem("ord"));
        return mb.final();
    }
    
    // Create a big operator node
    Item bigOperator(const char* op, Item lower = ItemNull, Item upper = ItemNull) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("bigop"));
        mb.put("op", builder.createStringItem(op));
        if (lower.item != ItemNull.item) mb.put("lower", lower);
        if (upper.item != ItemNull.item) mb.put("upper", upper);
        mb.put("atom", builder.createSymbolItem("op"));
        return mb.final();
    }
    
    // Create a text node
    Item text(const char* content, const char* cmd = "\\text") {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("text"));
        mb.put("cmd", builder.createStringItem(cmd));
        mb.put("content", builder.createStringItem(content));
        return mb.final();
    }
    
    // Create a style node
    Item style(const char* cmd, Item content = ItemNull) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("style"));
        mb.put("cmd", builder.createStringItem(cmd));
        if (content.item != ItemNull.item) mb.put("content", content);
        return mb.final();
    }
    
    // Create a space node
    Item space(const char* cmd) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("space"));
        mb.put("cmd", builder.createStringItem(cmd));
        return mb.final();
    }
    
    // Create an environment node (matrix, aligned, cases, etc.)
    Item environment(const char* name, Item rows) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("environment"));
        mb.put("name", builder.createStringItem(name));
        mb.put("rows", rows);
        mb.put("atom", builder.createSymbolItem("inner"));
        return mb.final();
    }
    
    // Create an error node
    Item error(const char* message, const char* source = nullptr) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("error"));
        mb.put("message", builder.createStringItem(message));
        if (source) mb.put("source", builder.createStringItem(source));
        return mb.final();
    }
    
    // Create a raw operator node (for +, -, *, etc.)
    Item op(const char* value, MathAtomType atom_type = MathAtomType::Bin) {
        MapBuilder mb = builder.map();
        mb.put("node", builder.createSymbolItem("symbol"));
        mb.put("value", builder.createStringItem(value));
        mb.put("atom", builder.createSymbolItem(math_atom_type_name(atom_type)));
        return mb.final();
    }
    
    // Create a relation node (for =, <, >, etc.)
    Item rel(const char* value) {
        return op(value, MathAtomType::Rel);
    }
    
    // Create punctuation node
    Item punct(const char* value) {
        return op(value, MathAtomType::Punct);
    }

private:
    MarkBuilder builder;
};

// ============================================================================
// Utility functions for working with math nodes
// Implementations are in input-math2.cpp to access runtime functions
// ============================================================================

// Get the node type from a math node item
MathNodeType get_math_node_type(Item node);

// Get the atom type from a math node
MathAtomType get_math_atom_type(Item node);

} // namespace lambda

#endif // LAMBDA_MATH_NODE_HPP