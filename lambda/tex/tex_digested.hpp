// tex_digested.hpp - Digested Intermediate Representation
//
// This file defines the semantic IR produced by the Digester (Stomach).
// The digestion phase takes expanded tokens and builds a structured
// representation that captures document semantics while deferring
// output-specific formatting decisions.
//
// Key node types:
// - BOX: Digested text with font info
// - LIST: Collection of boxes
// - WHATSIT: Constructor results with deferred construction
// - GLUE/KERN/PENALTY/RULE: Spacing and break control
//
// Reference: Latex_Typeset_Design3.md Section 3

#ifndef TEX_DIGESTED_HPP
#define TEX_DIGESTED_HPP

#include "tex_glue.hpp"
#include "../../lib/arena.h"
#include "../../lib/hashmap.h"
#include <cstdint>
#include <cstddef>

namespace tex {

// Forward declarations
struct DigestedNode;
struct CommandDef;

// ============================================================================
// Digested Node Types
// ============================================================================

enum class DigestedType : uint8_t {
    BOX,            // Digested text with font
    LIST,           // Collection of boxes
    WHATSIT,        // Constructor result (carries construction instructions)
    GLUE,           // Stretchable space
    KERN,           // Fixed space
    PENALTY,        // Break penalty
    RULE,           // Line/rectangle
    MARK,           // Mark for headers/footers
    INSERT,         // Insertion (footnote, float)
    SPECIAL,        // \special command
    MATH,           // Math content
    CHAR,           // Single character
    DISC,           // Discretionary break
};

// String name for debugging
const char* digested_type_name(DigestedType type);

// ============================================================================
// Font Specification for Digested Nodes
// ============================================================================

struct DigestedFontSpec {
    const char* family;         // Font family name
    float size_pt;              // Size in points
    
    // Style flags
    enum Flags : uint16_t {
        NONE        = 0x0000,
        BOLD        = 0x0001,
        ITALIC      = 0x0002,
        SMALLCAPS   = 0x0004,
        MONOSPACE   = 0x0008,
        SANS_SERIF  = 0x0010,
    };
    uint16_t flags;
    
    DigestedFontSpec() 
        : family("cmr"), size_pt(10.0f), flags(NONE) {}
    
    bool has(Flags f) const { return (flags & f) != 0; }
    void set(Flags f) { flags |= f; }
    void clear(Flags f) { flags &= ~f; }
    
    // Create common font specs
    static DigestedFontSpec roman(float size) {
        DigestedFontSpec f;
        f.family = "cmr";
        f.size_pt = size;
        return f;
    }
    
    static DigestedFontSpec bold(float size) {
        DigestedFontSpec f;
        f.family = "cmbx";
        f.size_pt = size;
        f.flags = BOLD;
        return f;
    }
    
    static DigestedFontSpec italic(float size) {
        DigestedFontSpec f;
        f.family = "cmti";
        f.size_pt = size;
        f.flags = ITALIC;
        return f;
    }
};

// ============================================================================
// Property Entry (for WHATSIT nodes)
// ============================================================================

struct PropertyEntry {
    const char* key;
    const char* value;
    PropertyEntry* next;
};

// Simple linked list based property storage (arena allocated)
struct PropertyMap {
    PropertyEntry* head;
    Arena* arena;
    
    void init(Arena* a) {
        arena = a;
        head = nullptr;
    }
    
    void set(const char* key, const char* value);
    const char* get(const char* key) const;
    bool has(const char* key) const;
};

// ============================================================================
// Glue Specification (for GLUE nodes)
// ============================================================================

struct GlueSpec {
    float space;            // Natural size (points)
    float stretch;          // Stretch amount
    float shrink;           // Shrink amount
    GlueOrder stretch_order;
    GlueOrder shrink_order;
    
    GlueSpec() 
        : space(0), stretch(0), shrink(0)
        , stretch_order(GlueOrder::Normal)
        , shrink_order(GlueOrder::Normal) {}
    
    static GlueSpec fixed(float s) {
        GlueSpec g;
        g.space = s;
        return g;
    }
    
    static GlueSpec flexible(float s, float str, float shr) {
        GlueSpec g;
        g.space = s;
        g.stretch = str;
        g.shrink = shr;
        return g;
    }
    
    // Common glue values (in points)
    static GlueSpec parfillskip();      // End of paragraph fill
    static GlueSpec parskip();          // Between paragraphs
    static GlueSpec baselineskip();     // Between baselines
    static GlueSpec lineskip();         // Minimum between lines
    static GlueSpec topskip();          // Top of page
    static GlueSpec abovedisplayskip(); // Above display math
    static GlueSpec belowdisplayskip(); // Below display math
};

// ============================================================================
// Source Location
// ============================================================================

struct DigestedSourceLoc {
    uint32_t start;         // Byte offset
    uint32_t end;           // Byte offset
    uint16_t line;          // Line number (1-based)
    uint16_t column;        // Column (1-based)
    
    DigestedSourceLoc() : start(0), end(0), line(0), column(0) {}
};

// ============================================================================
// Digested Node
// ============================================================================

struct DigestedNode {
    DigestedType type;
    uint8_t flags;
    
    // Flag bits
    static constexpr uint8_t FLAG_IMPLICIT = 0x01;  // Implicitly created
    static constexpr uint8_t FLAG_HORIZONTAL = 0x02; // In horizontal mode
    static constexpr uint8_t FLAG_VERTICAL = 0x04;   // In vertical mode
    static constexpr uint8_t FLAG_MATH = 0x08;       // In math mode
    
    // Source location (from tokenizer)
    DigestedSourceLoc loc;
    
    // Font at time of digestion
    DigestedFontSpec font;
    
    // Linked list pointers (arena-allocated)
    DigestedNode* next;
    DigestedNode* prev;
    
    union Content {
        // BOX: text content
        struct {
            const char* text;
            size_t len;
            float width;        // Measured width (optional, for DVI)
            float height;       // Height above baseline
            float depth;        // Depth below baseline
        } box;
        
        // CHAR: single character
        struct {
            int32_t codepoint;
            float width;
            float height;
            float depth;
        } chr;
        
        // LIST: child nodes
        struct {
            DigestedNode* head;
            DigestedNode* tail;
            size_t count;
            bool is_horizontal; // hlist vs vlist
        } list;
        
        // WHATSIT: deferred construction
        struct {
            const char* name;           // Constructor name
            size_t name_len;
            const CommandDef* definition;
            DigestedNode** args;        // Array of argument nodes
            size_t arg_count;
            PropertyMap* properties;
        } whatsit;
        
        // GLUE: stretchable space
        GlueSpec glue;
        
        // KERN: fixed space
        struct {
            float amount;
        } kern;
        
        // PENALTY: break penalty
        struct {
            int value;          // -10000 to +10000
        } penalty;
        
        // RULE: filled rectangle
        struct {
            float width;        // -1 = running
            float height;       // -1 = running
            float depth;        // -1 = running
        } rule;
        
        // MARK: page marker
        struct {
            const char* text;
            size_t len;
            int mark_class;     // 0 = normal, 1 = first, 2 = bot
        } mark;
        
        // INSERT: insertion
        struct {
            int insert_class;   // footnote=0, float=1, etc.
            DigestedNode* content;
            float natural_height;
            float split_max;
        } insert;
        
        // SPECIAL: \special command
        struct {
            const char* command;
            size_t len;
        } special;
        
        // MATH: math content
        struct {
            DigestedNode* content;  // Math formula content
            bool display;           // Display vs inline
        } math;
        
        // DISC: discretionary break
        struct {
            DigestedNode* pre;      // Pre-break text
            DigestedNode* post;     // Post-break text
            DigestedNode* nobreak;  // No-break text
        } disc;
        
        Content() { /* zero initialize */ }
    } content;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    static DigestedNode* make_box(Arena* arena, const char* text, size_t len,
                                   const DigestedFontSpec& font);
    static DigestedNode* make_char(Arena* arena, int32_t codepoint,
                                    const DigestedFontSpec& font);
    static DigestedNode* make_list(Arena* arena, bool is_horizontal);
    static DigestedNode* make_whatsit(Arena* arena, const char* name, size_t name_len);
    static DigestedNode* make_glue(Arena* arena, const GlueSpec& spec);
    static DigestedNode* make_kern(Arena* arena, float amount);
    static DigestedNode* make_penalty(Arena* arena, int value);
    static DigestedNode* make_rule(Arena* arena, float width, float height, float depth);
    static DigestedNode* make_mark(Arena* arena, const char* text, size_t len, int mark_class);
    static DigestedNode* make_insert(Arena* arena, int insert_class, DigestedNode* content);
    static DigestedNode* make_special(Arena* arena, const char* command, size_t len);
    static DigestedNode* make_math(Arena* arena, DigestedNode* content, bool display);
    static DigestedNode* make_disc(Arena* arena, DigestedNode* pre, 
                                    DigestedNode* post, DigestedNode* nobreak);
    
    // ========================================================================
    // List Operations
    // ========================================================================
    
    // Append a node to this list (only valid for LIST type)
    void append(DigestedNode* node);
    
    // Prepend a node to this list
    void prepend(DigestedNode* node);
    
    // Get list length
    size_t list_length() const;
    
    // ========================================================================
    // Whatsit Operations
    // ========================================================================
    
    // Set whatsit property
    void set_property(const char* key, const char* value);
    
    // Get whatsit property
    const char* get_property(const char* key) const;
};

// ============================================================================
// Common Penalty Values
// ============================================================================

constexpr int PENALTY_INFINITE = 10000;     // Never break here
constexpr int PENALTY_EJECT = -10000;       // Always break here
constexpr int PENALTY_HYPHEN = 50;          // Hyphenation penalty
constexpr int PENALTY_EXHYPHEN = 50;        // Explicit hyphen penalty
constexpr int PENALTY_BINOP = 700;          // After binary operator
constexpr int PENALTY_RELOP = 500;          // After relation
constexpr int PENALTY_CLUB = 150;           // Club line penalty
constexpr int PENALTY_WIDOW = 150;          // Widow line penalty

// ============================================================================
// Insert Classes
// ============================================================================

constexpr int INSERT_FOOTNOTE = 0;
constexpr int INSERT_TOPFLOAT = 1;
constexpr int INSERT_MIDFLOAT = 2;
constexpr int INSERT_BOTFLOAT = 3;
constexpr int INSERT_MARGINAL = 254;

} // namespace tex

#endif // TEX_DIGESTED_HPP
