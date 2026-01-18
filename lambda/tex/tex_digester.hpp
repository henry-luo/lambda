// tex_digester.hpp - TeX Digester (Stomach)
//
// The Digester processes expanded tokens from the Expander and builds
// the semantic intermediate representation (DigestedNode tree).
//
// This is the "Stomach" in TeX terminology. It:
// - Executes primitives (assignments, mode changes)
// - Builds Boxes/Whatsits with font state
// - Tracks counters, labels, cross-references
// - Manages mode (vertical, horizontal, math)
//
// Reference: TeXBook Chapter 24, Latex_Typeset_Design3.md Section 3

#ifndef TEX_DIGESTER_HPP
#define TEX_DIGESTER_HPP

#include "tex_digested.hpp"
#include "tex_expander.hpp"
#include "tex_token.hpp"
#include "../../lib/arena.h"
#include "../../lib/hashmap.h"
#include "../../lib/strbuf.h"
#include <cstddef>
#include <cstdint>

namespace tex {

// Forward declarations
struct CommandDef;
class CommandRegistry;

// ============================================================================
// Mode (TeX processing mode)
// ============================================================================

enum class DigesterMode : uint8_t {
    VERTICAL,           // Building vertical list (between paragraphs)
    INTERNAL_VERTICAL,  // Inside \vbox
    HORIZONTAL,         // Building horizontal list (paragraph text)
    RESTRICTED_HORIZONTAL,  // Inside \hbox
    MATH,               // Display math mode
    INLINE_MATH,        // Inline math mode
};

const char* mode_name(DigesterMode mode);

// ============================================================================
// Command Definition Types
// ============================================================================

enum class CommandType : uint8_t {
    MACRO,          // Simple text expansion
    PRIMITIVE,      // Side effect + optional box
    CONSTRUCTOR,    // Produces Whatsit for output
    ENVIRONMENT,    // Begin/end pair
    MATH,           // Math-mode command
};

// ============================================================================
// Command Definition
// ============================================================================

// Callback function types
class Digester;

typedef void (*PrimitiveFn)(Digester* digester);
typedef DigestedNode* (*ConstructorFn)(Digester* digester, 
                                        DigestedNode** args, 
                                        size_t arg_count);
typedef void (*DigestHookFn)(Digester* digester, DigestedNode* node);

struct CommandDef {
    const char* name;           // Command name (without \)
    size_t name_len;
    CommandType type;
    
    // Parameter specification (LaTeXML-style)
    // "{}{}" = two required args
    // "[Default]{}" = optional with default, then required
    const char* params;
    int param_count;
    
    // For MACRO type: replacement text
    const char* replacement;
    size_t replacement_len;
    
    // For CONSTRUCTOR type
    union {
        const char* pattern;        // Output pattern "<section>#1</section>"
        ConstructorFn callback;     // C++ function pointer
    };
    bool use_callback;
    
    // Hooks
    DigestHookFn before_digest;
    DigestHookFn after_digest;
    
    // Properties
    bool is_math;               // Only valid in math mode
    bool is_outer;              // Cannot be in arguments
    
    CommandDef()
        : name(nullptr), name_len(0)
        , type(CommandType::MACRO)
        , params(nullptr), param_count(0)
        , replacement(nullptr), replacement_len(0)
        , use_callback(false)
        , before_digest(nullptr), after_digest(nullptr)
        , is_math(false), is_outer(false)
    {
        pattern = nullptr;
    }
};

// ============================================================================
// Command Registry
// ============================================================================

class CommandRegistry {
public:
    CommandRegistry(Arena* arena);
    ~CommandRegistry();
    
    // Define commands
    void define_macro(const char* name, const char* params, const char* replacement);
    void define_primitive(const char* name, const char* params, PrimitiveFn fn);
    void define_constructor(const char* name, const char* params, const char* pattern);
    void define_constructor_fn(const char* name, const char* params, ConstructorFn fn);
    void define_environment(const char* name, const char* begin_pattern, const char* end_pattern);
    void define_math(const char* name, const char* meaning, const char* role);
    
    // Lookup
    const CommandDef* lookup(const char* name, size_t len) const;
    const CommandDef* lookup(const char* name) const;
    
    // Check if defined
    bool is_defined(const char* name, size_t len) const;
    
    // Scoping
    void begin_group();
    void end_group();
    void make_global(const char* name);
    
private:
    Arena* arena;
    
    // Simple linked list for commands
    struct CommandEntry {
        CommandDef def;
        CommandEntry* next;
    };
    CommandEntry* command_list;
    
    struct SaveEntry {
        const char* name;
        size_t name_len;
        CommandDef* old_def;
        SaveEntry* next;
    };
    SaveEntry* save_stack;
    int group_depth;
};

// ============================================================================
// Counter
// ============================================================================

struct Counter {
    const char* name;
    int value;
    Counter* parent;            // For \refstepcounter
    const char* format;         // Formatting (arabic, roman, etc.)
    
    Counter() : name(nullptr), value(0), parent(nullptr), format("arabic") {}
};

// ============================================================================
// Label Entry
// ============================================================================

struct LabelEntry {
    const char* label;
    const char* ref_text;       // Resolved reference text
    const char* page_text;      // Page reference
    int section_level;          // Section hierarchy level
    Counter* counter;           // Counter at time of \label
};

// ============================================================================
// Digester (Stomach)
// ============================================================================

class Digester {
public:
    Digester(Expander* expander, Arena* arena);
    ~Digester();
    
    // ========================================================================
    // Main Digestion Interface
    // ========================================================================
    
    // Digest all remaining tokens and return the document root
    DigestedNode* digest();
    
    // Digest a single token
    void digest_token(const Token& token);
    
    // Digest until a specific control sequence is found
    DigestedNode* digest_until(const char* end_cs);
    
    // Digest a group {...}
    DigestedNode* digest_group();
    
    // ========================================================================
    // Mode Management
    // ========================================================================
    
    DigesterMode mode() const { return current_mode; }
    void set_mode(DigesterMode m);
    
    bool is_horizontal() const;
    bool is_vertical() const;
    bool is_math() const;
    
    // Enter/leave paragraph (horizontal mode)
    void begin_paragraph();
    void end_paragraph();
    
    // Enter/leave math mode
    void begin_math(bool display);
    void end_math();
    
    // ========================================================================
    // Font State
    // ========================================================================
    
    const DigestedFontSpec& current_font() const { return font; }
    void set_font(const DigestedFontSpec& f);
    void set_font_family(const char* family);
    void set_font_size(float size_pt);
    void set_font_style(DigestedFontSpec::Flags flag, bool on);
    
    // ========================================================================
    // Counter Management
    // ========================================================================
    
    Counter* get_counter(const char* name);
    Counter* create_counter(const char* name, Counter* parent = nullptr);
    
    void step_counter(const char* name);
    void add_to_counter(const char* name, int delta);
    void set_counter(const char* name, int value);
    int get_counter_value(const char* name) const;
    
    // Format counter value as string
    const char* format_counter(const char* name);
    
    // ========================================================================
    // Label/Reference Management
    // ========================================================================
    
    void set_label(const char* label);
    const char* resolve_ref(const char* label) const;
    const char* resolve_pageref(const char* label) const;
    
    // ========================================================================
    // Footnotes
    // ========================================================================
    
    void add_footnote(DigestedNode* content);
    DigestedNode** get_footnotes(size_t* count);
    void clear_footnotes();
    
    // ========================================================================
    // Output Building
    // ========================================================================
    
    // Add node to current list
    void add_node(DigestedNode* node);
    
    // Add text at current position
    void add_text(const char* text, size_t len);
    void add_char(int32_t codepoint);
    
    // Add spacing
    void add_glue(const GlueSpec& spec);
    void add_kern(float amount);
    void add_penalty(int value);
    
    // Add special nodes
    void add_rule(float width, float height, float depth);
    void add_mark(const char* text, size_t len, int mark_class);
    void add_special(const char* command, size_t len);
    
    // ========================================================================
    // Grouping
    // ========================================================================
    
    void begin_group();
    void end_group();
    int group_depth() const { return group_level; }
    
    // ========================================================================
    // Command Registry
    // ========================================================================
    
    void set_registry(CommandRegistry* reg) { registry = reg; }
    CommandRegistry* get_registry() { return registry; }
    
    // ========================================================================
    // Error Handling
    // ========================================================================
    
    void error(const char* message);
    void warning(const char* message);
    
    // ========================================================================
    // Access to Expander (for reading arguments)
    // ========================================================================
    
    Expander* get_expander() { return expander; }
    Arena* get_arena() { return arena; }
    
    // Read a required argument {...}
    DigestedNode* read_argument();
    
    // Read an optional argument [...] (returns nullptr if not present)
    DigestedNode* read_optional_argument();
    
    // Read tokens until closing brace (without digesting)
    TokenList read_balanced_text();
    
private:
    Expander* expander;
    Arena* arena;
    CommandRegistry* registry;
    
    // Current mode
    DigesterMode current_mode;
    DigesterMode prev_mode;         // For nested modes
    
    // Current font
    DigestedFontSpec font;
    
    // Output stacks
    struct ListContext {
        DigestedNode* list;         // Current list being built
        DigesterMode mode;          // Mode when this list was started
        ListContext* prev;
    };
    ListContext* list_stack;
    DigestedNode* current_list;     // Shortcut to list_stack->list
    
    // Grouping
    int group_level;
    struct GroupSave {
        DigestedFontSpec font;
        DigesterMode mode;
        GroupSave* prev;
    };
    GroupSave* group_stack;
    
    // Counters (linked list)
    Counter* counter_list;
    
    // Labels (linked list)
    struct LabelList {
        LabelEntry entry;
        LabelList* next;
    };
    LabelList* label_list;
    const char* pending_label;      // Label waiting to be attached
    
    // Footnotes
    DigestedNode** footnotes;
    size_t footnote_count;
    size_t footnote_capacity;
    
    // String buffer for formatting
    StrBuf* strbuf;
    
    // ========================================================================
    // Internal Helpers
    // ========================================================================
    
    void push_list(DigestedNode* list);
    DigestedNode* pop_list();
    
    void save_font();
    void restore_font();
    
    void process_control_sequence(const Token& token);
    void process_character(const Token& token);
    void process_space();
    void process_math_shift();
    void process_begin_group();
    void process_end_group();
    
    void execute_primitive(const CommandDef* def);
    DigestedNode* execute_constructor(const CommandDef* def);
    void expand_macro(const CommandDef* def);
};

// ============================================================================
// Base Package Loader
// ============================================================================

class PackageLoader {
public:
    PackageLoader(CommandRegistry* registry, Arena* arena);
    
    // Load built-in packages
    void load_tex_base();           // TeX primitives
    void load_latex_base();         // LaTeX base commands
    void load_amsmath();            // AMS math
    
    // Load a named package
    bool load_package(const char* name);
    
    // Check if package is loaded
    bool is_loaded(const char* name) const;
    
private:
    CommandRegistry* registry;
    Arena* arena;
    
    // Simple bitmask for loaded packages
    enum LoadedPackages : uint32_t {
        PKG_TEX_BASE    = 0x01,
        PKG_LATEX_BASE  = 0x02,
        PKG_AMSMATH     = 0x04,
        PKG_AMSSYMB     = 0x08,
    };
    uint32_t loaded_packages;
    
    void register_tex_primitives();
    void register_latex_commands();
    void register_ams_commands();
};

} // namespace tex

#endif // TEX_DIGESTER_HPP
