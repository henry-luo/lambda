// tex_expander.hpp - TeX Expander (Gullet)
//
// The expander handles macro expansion, conditionals, and expansion primitives.
// This is the "Gullet" in TeX terminology (TeXBook Chapter 20).
//
// Features:
// - Macro expansion (\def, \edef, \gdef, \xdef)
// - Conditionals (\if, \ifx, \ifnum, etc.)
// - Expansion primitives (\expandafter, \noexpand, \the, etc.)
// - Let assignments (\let, \futurelet)
//
// Reference: TeXBook Chapter 20

#ifndef TEX_EXPANDER_HPP
#define TEX_EXPANDER_HPP

#include "tex_tokenizer.hpp"
#include "tex_catcode.hpp"
#include "tex_token.hpp"
#include "../../lib/arena.h"
#include "../../lib/hashmap.h"
#include <cstddef>

namespace tex {

// Forward declarations
struct MacroDef2;  // Enhanced macro definition

// ============================================================================
// Conditional State
// ============================================================================

struct CondState {
    enum class Type : uint8_t {
        IF,         // Normal \if...
        IFCASE,     // \ifcase (multiway)
    };
    
    Type type;
    bool result;        // True branch taken?
    bool else_seen;     // \else encountered?
    int case_value;     // For \ifcase
    int or_count;       // Number of \or seen
};

// ============================================================================
// Macro Definition (Enhanced)
// ============================================================================

struct MacroDef2 {
    const char* name;           // Macro name (without backslash)
    uint16_t name_len;
    
    // Parameter specification
    TokenList param_text;       // Parameter pattern (delimited args)
    int8_t param_count;         // Number of parameters (0-9)
    
    // Replacement text
    TokenList replacement;
    
    // Flags
    bool is_long;               // \long\def - can span paragraphs
    bool is_outer;              // \outer\def - can't be in arguments
    bool is_protected;          // \protected\def - not expanded in \edef
    bool is_expandable;         // Is this an expandable macro?
    
    MacroDef2() 
        : name(nullptr), name_len(0)
        , param_count(0)
        , is_long(false), is_outer(false)
        , is_protected(false), is_expandable(true)
    {}
};

// ============================================================================
// Built-in Command Types
// ============================================================================

enum class PrimitiveType : uint16_t {
    NONE = 0,
    
    // Expansion primitives
    EXPANDAFTER,
    NOEXPAND,
    CSNAME,
    ENDCSNAME,
    STRING,
    NUMBER,
    ROMANNUMERAL,
    THE,
    MEANING,
    JOBNAME,
    
    // e-TeX expansion
    UNEXPANDED,
    DETOKENIZE,
    NUMEXPR,
    DIMEXPR,
    GLUEEXPR,
    MUEXPR,
    IFDEFINED,
    IFCSNAME,
    
    // Conditionals
    IF,
    IFCAT,
    IFX,
    IFNUM,
    IFDIM,
    IFODD,
    IFVMODE,
    IFHMODE,
    IFMMODE,
    IFINNER,
    IFVOID,
    IFHBOX,
    IFVBOX,
    IFEOF,
    IFTRUE,
    IFFALSE,
    IFCASE,
    ELSE,
    FI,
    OR,
    
    // Definitions
    DEF,
    EDEF,
    GDEF,
    XDEF,
    LET,
    FUTURELET,
    
    // LaTeX definitions
    NEWCOMMAND,
    RENEWCOMMAND,
    PROVIDECOMMAND,
    
    // Registers
    COUNT,
    DIMEN,
    SKIP,
    TOKS,
    ADVANCE,
    MULTIPLY,
    DIVIDE,
    
    // Grouping
    BEGINGROUP,
    ENDGROUP,
    BGROUP,
    EGROUP,
    GLOBAL,
    LONG,
    OUTER,
    PROTECTED,
    
    // Special
    RELAX,
    CATCODE,
    LCCODE,
    UCCODE,
    MATHCODE,
    ENDLINECHAR,
    ESCAPECHAR,
    NEWCOUNT,
    INPUT,
    ENDINPUT,
    
    // For passive commands (not expandable)
    PASSIVE,
};

// Check if a primitive is expandable
bool is_expandable_primitive(PrimitiveType type);

// ============================================================================
// Command Entry
// ============================================================================

struct CommandEntry {
    enum class Type : uint8_t {
        UNDEFINED,      // Not defined
        PRIMITIVE,      // Built-in primitive
        MACRO,          // User-defined macro
        CHAR_DEF,       // \chardef token
        LET,            // \let to another token
        ACTIVE_CHAR,    // Active character
    };
    
    Type type;
    PrimitiveType primitive;
    MacroDef2* macro;
    struct {
        char code;
        CatCode catcode;
    } chardef;
    Token let_target;
    
    CommandEntry() : type(Type::UNDEFINED), primitive(PrimitiveType::NONE), macro(nullptr) {
        chardef.code = 0;
        chardef.catcode = CatCode::OTHER;
    }
    
    // Is this command expandable?
    bool is_expandable() const;
};

// ============================================================================
// Expander (Gullet)
// ============================================================================

class Expander {
public:
    Expander(Arena* arena);
    ~Expander();
    
    // ========================================================================
    // Input Management
    // ========================================================================
    
    // Push input source
    void push_input(const char* data, size_t len, const char* filename = nullptr);
    
    // Push tokens to be read
    void push_tokens(TokenList* list);
    
    // Check if at end
    bool at_end() const;
    
    // ========================================================================
    // Token Reading (with expansion)
    // ========================================================================
    
    // Get next unexpanded token
    Token get_token();
    
    // Get next token, expanding expandable tokens
    Token expand_token();
    
    // Fully expand until no more expansions possible
    TokenList expand_fully();
    
    // Put token back to be read again
    void push_back(const Token& t);
    
    // ========================================================================
    // Catcodes
    // ========================================================================
    
    CatCodeTable& catcodes() { return tokenizer.catcodes(); }
    
    // ========================================================================
    // Command Lookup
    // ========================================================================
    
    // Get meaning of a control sequence
    const CommandEntry* lookup(const char* name, size_t len) const;
    const CommandEntry* lookup(const Token& t) const;
    
    // Check if a control sequence is defined
    bool is_defined(const char* name, size_t len) const;
    bool is_defined(const Token& t) const;
    
    // ========================================================================
    // Definitions
    // ========================================================================
    
    // Define a macro
    void define_macro(const char* name, size_t name_len,
                      const TokenList& param_text, int param_count,
                      const TokenList& replacement,
                      bool is_global = false);
    
    // \let command
    void let(const char* name, size_t name_len, const Token& target, bool is_global = false);
    
    // Register a primitive
    void register_primitive(const char* name, PrimitiveType type);
    
    // ========================================================================
    // Grouping
    // ========================================================================
    
    void begin_group();
    void end_group();
    int group_depth() const;
    void save_command(const char* name, size_t name_len);
    
    // ========================================================================
    // Registers
    // ========================================================================
    
    int get_count(int reg) const;
    void set_count(int reg, int value, bool global = false);
    void advance_count(int reg, int by);
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    void set_expansion_limit(int limit) { expansion_limit = limit; }
    int get_expansion_depth() const { return expansion_depth; }
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    // Register all TeX primitives
    void init_primitives();
    
    // Register LaTeX base commands
    void init_latex_base();
    
private:
    Arena* arena;
    Tokenizer tokenizer;
    
    // Command hash table
    HashMap* commands;
    
    // Saved commands for grouping
    struct GroupSave {
        HashMap* saved_commands;
        int* saved_counts;
        GroupSave* prev;
    };
    GroupSave* group_stack;
    
    // Count registers (256 registers)
    int count_regs[256];
    
    // Conditional stack
    static constexpr int MAX_COND_STACK = 256;
    CondState cond_stack[MAX_COND_STACK];
    int cond_depth;
    
    // Expansion control
    int expansion_depth;
    int expansion_limit;
    
    // Currently expanding (to detect recursion)
    const char* currently_expanding;
    
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    // Expand a single macro call
    void expand_macro(const Token& cs, const MacroDef2* macro);
    
    // Parse macro arguments
    bool parse_macro_args(const MacroDef2* macro, TokenList* args);
    
    // Read a single argument (braced group or single token)
    TokenList read_argument();
    
    // Read delimited argument (up to delimiter tokens)
    TokenList read_delimited_argument(const TokenList& delimiter);
    
    // Read a balanced text {...}
    TokenList read_balanced_text();
    
    // Read until \fi, \else, or \or (for skipping conditional branches)
    void skip_conditional_branch(bool skip_else);
    
    // ========================================================================
    // Primitive Execution
    // ========================================================================
    
    // Execute a primitive
    void execute_primitive(PrimitiveType type, const Token& cs);
    
    // Expansion primitives
    void do_expandafter();
    void do_noexpand();
    TokenList do_csname();
    TokenList do_string(const Token& t);
    TokenList do_number(int n);
    TokenList do_romannumeral(int n);
    TokenList do_the(int reg_type, int reg_num);
    TokenList do_meaning(const Token& t);
    
    // e-TeX primitives
    TokenList do_unexpanded();
    TokenList do_detokenize(const TokenList& toks);
    int do_numexpr();
    
    // Conditionals
    void do_if();
    void do_ifcat();
    void do_ifx();
    void do_ifnum();
    void do_ifdim();
    void do_ifodd();
    void do_iftrue();
    void do_iffalse();
    void do_ifcase();
    void do_ifdefined();
    void do_else();
    void do_fi();
    void do_or();
    
    // Definitions
    void do_def(bool is_global, bool is_edef);
    void do_let(bool is_global);
    void do_futurelet();
    
    // Process conditional result
    void process_conditional(bool result);
    
    // Read a number
    int scan_int();
    
    // Read a dimension
    float scan_dimen();
    
    // Read keyword
    bool scan_keyword(const char* keyword);
    
    // Read register number
    int scan_register_num();
};

} // namespace tex

#endif // TEX_EXPANDER_HPP
