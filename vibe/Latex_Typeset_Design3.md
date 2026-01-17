# Lambda LaTeX Pipeline Enhancement Proposal

This document proposes enhancements to Lambda's LaTeX processing pipeline, inspired by LaTeXML's architecture. The goal is to achieve comprehensive LaTeX support with extensible package bindings.

**Related Documents:**
- [Latexml.md](./Latexml.md) - LaTeXML architecture analysis
- `doc/Latex_Typeset_Design2.md` - Previous design document

---

## 1. Overview

### 1.1 Goals

1. **Full macro expansion** - Implement TeX-compatible macro expansion with catcodes, conditionals, and expansion primitives
2. **Extensible package system** - Allow LaTeX packages to be defined declaratively without C++ code changes
3. **Semantic intermediate representation** - Digestion phase to capture document semantics before output
4. **Comprehensive package coverage** - Support commonly used LaTeX packages

### 1.2 Proposed Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Enhanced Lambda LaTeX Pipeline                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   LaTeX Source                                                              │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Tokenizer (catcode-aware)                    tex_tokenizer.cpp   │     │
│   │  - Character → Token with catcode                                 │     │
│   │  - Ligatures, ^^notation                                          │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Expander (Gullet)                            tex_expander.cpp    │     │
│   │  - Macro expansion (\def, \newcommand)                            │     │
│   │  - Conditionals (\if, \ifx, \ifnum, etc.)                         │     │
│   │  - \expandafter, \noexpand, \the, \csname                         │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Digester (Stomach)                           tex_digester.cpp    │     │
│   │  - Execute primitives (assignments, mode changes)                 │     │
│   │  - Build Boxes/Whatsits with font state                           │     │
│   │  - Track counters, labels, cross-references                       │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Digested IR (Box/Whatsit)                    tex_digested.hpp    │     │
│   │  - Semantic document structure                                    │     │
│   │  - Deferred construction instructions                             │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ├────────────────────────┬─────────────────────┐                      │
│       ▼                        ▼                     ▼                      │
│   ┌──────────────┐      ┌──────────────┐      ┌──────────────┐              │
│   │ HTML Output  │      │ TexNode      │      │ XML/MathML   │              │
│   │ (browser     │      │ (DVI/PDF/SVG │      │ (semantic    │              │
│   │  layout)     │      │  typesetting)│      │  output)     │              │
│   └──────────────┘      └──────────────┘      └──────────────┘              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Full Macro Expansion Support

### 2.1 Catcode System

Implement TeX's category code system for proper tokenization:

```cpp
// tex_catcode.hpp

enum class CatCode : uint8_t {
    ESCAPE      = 0,   // \ - starts control sequence
    BEGIN_GROUP = 1,   // { - begins group
    END_GROUP   = 2,   // } - ends group
    MATH_SHIFT  = 3,   // $ - math mode toggle
    ALIGN_TAB   = 4,   // & - alignment tab
    END_LINE    = 5,   // \r - end of line
    PARAM       = 6,   // # - macro parameter
    SUPERSCRIPT = 7,   // ^ - superscript
    SUBSCRIPT   = 8,   // _ - subscript
    IGNORED     = 9,   // null - ignored
    SPACE       = 10,  // space - space
    LETTER      = 11,  // a-z, A-Z - letters
    OTHER       = 12,  // other characters
    ACTIVE      = 13,  // ~ - active character
    COMMENT     = 14,  // % - comment
    INVALID     = 15,  // delete - invalid
};

class CatCodeTable {
public:
    CatCodeTable();
    
    void set(char c, CatCode cat);
    CatCode get(char c) const;
    
    // Create standard LaTeX catcode table
    static CatCodeTable latex_default();
    
    // Modify for verbatim mode
    void set_verbatim_mode();
    void restore_normal_mode();
    
private:
    CatCode table[256];
};
```

### 2.2 Token Representation

```cpp
// tex_token.hpp

struct Token {
    enum class Type : uint8_t {
        CHAR,           // Character with catcode
        CS,             // Control sequence
        PARAM,          // Parameter token (#1, #2, etc.)
        BEGIN_GROUP,    // {
        END_GROUP,      // }
        MATH_SHIFT,     // $
        ALIGN_TAB,      // &
        SUPERSCRIPT,    // ^
        SUBSCRIPT,      // _
        SPACE,          // space token
        ACTIVE,         // active character (like ~)
    };
    
    Type type;
    union {
        struct { char ch; CatCode cat; } chr;  // For CHAR
        struct { const char* name; size_t len; } cs;  // For CS
        int param_num;  // For PARAM (#1 = 1, etc.)
    };
    
    // Source location
    SourceLoc loc;
    
    // Constructors
    static Token make_char(char c, CatCode cat, SourceLoc loc);
    static Token make_cs(const char* name, size_t len, SourceLoc loc);
    static Token make_param(int num, SourceLoc loc);
};

class TokenList {
public:
    void push_back(Token t);
    void push_front(Token t);
    Token pop_front();
    bool empty() const;
    size_t size() const;
    
    // Concatenation
    void append(const TokenList& other);
    
    // For macro replacement
    TokenList substitute(const TokenList* args, int arg_count) const;
    
private:
    std::vector<Token> tokens;  // Or arena-allocated list
};
```

### 2.3 Macro Definition Structure

```cpp
// tex_macro.hpp (enhanced)

struct MacroDef {
    const char* name;
    size_t name_len;
    
    // Parameter pattern (for parsing arguments)
    // e.g., "#1#2" or "#1.#2" (delimited) or "[#1]#2" (optional)
    TokenList param_pattern;
    int param_count;
    
    // Replacement tokens
    TokenList replacement;
    
    // Flags
    bool is_long;       // \long\def - can span paragraphs
    bool is_outer;      // \outer\def - cannot be in arguments
    bool is_protected;  // \protected - not expanded in \edef
    bool is_global;     // \gdef - global scope
    
    // For LaTeX optional arguments
    bool has_optional;
    TokenList default_value;
};
```

### 2.4 Expansion Primitives

```cpp
// tex_expander.hpp

class Expander {
public:
    Expander(CatCodeTable* catcodes, State* state);
    
    // Main expansion loop
    Token get_next();           // Get next unexpanded token
    Token expand_next();        // Get next token, expanding if expandable
    TokenList expand_fully();   // Expand until no more macros
    
    // ========================================================================
    // TeX Expansion Primitives
    // ========================================================================
    
    // \expandafter - expand token after next first
    void expandafter();
    
    // \noexpand - prevent expansion of next token
    Token noexpand();
    
    // \the - convert register to tokens
    TokenList the(RegisterType type, int reg);
    
    // \csname...\endcsname - construct control sequence from tokens
    Token csname();
    
    // \string - convert token to string tokens
    TokenList string(Token t);
    
    // \number - convert number to digit tokens
    TokenList number(int n);
    
    // \romannumeral - convert to roman numeral tokens
    TokenList romannumeral(int n);
    
    // \meaning - show meaning of token
    TokenList meaning(Token t);
    
    // \jobname - name of job
    TokenList jobname();
    
    // ========================================================================
    // Conditional Expansion
    // ========================================================================
    
    // \if - compare character codes
    bool if_char(Token a, Token b);
    
    // \ifcat - compare category codes
    bool if_cat(Token a, Token b);
    
    // \ifx - compare meanings
    bool if_x(Token a, Token b);
    
    // \ifnum - compare numbers
    bool if_num(int a, char rel, int b);  // rel: '<', '=', '>'
    
    // \ifdim - compare dimensions
    bool if_dim(float a, char rel, float b);
    
    // \ifodd - test if odd
    bool if_odd(int n);
    
    // \ifeof - test end of file
    bool if_eof(int stream);
    
    // \iftrue / \iffalse
    bool if_true();
    bool if_false();
    
    // \ifcase - multi-way branch
    void if_case(int n);
    
    // \ifdefined (e-TeX) - test if defined
    bool if_defined(Token cs);
    
    // \ifcsname (e-TeX) - test if \csname would define
    bool if_csname();
    
    // Process conditional branches
    void process_conditional(bool condition);
    void skip_to_else_or_fi();
    void skip_to_fi();
    
    // \else, \fi, \or handling
    void handle_else();
    void handle_fi();
    void handle_or();
    
    // ========================================================================
    // Input Stack
    // ========================================================================
    
    void push_input(TokenList tokens);
    void push_file(const char* filename);
    bool at_end() const;
    
private:
    CatCodeTable* catcodes;
    State* state;
    
    // Input stack (mouth stack in LaTeXML terms)
    std::vector<TokenSource> input_stack;
    
    // Conditional nesting
    std::vector<ConditionalState> cond_stack;
    
    // Expansion depth limit
    int expansion_depth;
    int expansion_limit;
};
```

### 2.5 e-TeX Extensions

```cpp
// Additional e-TeX primitives commonly used

class ETeXExpander : public Expander {
public:
    // \numexpr, \dimexpr - expression evaluation
    int numexpr();
    float dimexpr();
    
    // \unexpanded - prevent expansion in \edef
    TokenList unexpanded();
    
    // \detokenize - convert tokens to "other" characters
    TokenList detokenize(TokenList tokens);
    
    // \scantokens - re-tokenize string
    void scantokens(const char* str);
    
    // \protected - mark macro as protected
    void protected_def();
};
```

---

## 3. Digestion Phase

### 3.1 Digested Node Types

```cpp
// tex_digested.hpp

enum class DigestedType : uint8_t {
    BOX,            // Digested text with font
    LIST,           // Collection of boxes
    WHATSIT,        // Constructor result (carries construction instructions)
    GLUE,           // Space
    KERN,           // Fixed space
    PENALTY,        // Break penalty
    RULE,           // Line/rectangle
    MARK,           // Mark for headers/footers
    INSERT,         // Insertion (footnote, float)
    SPECIAL,        // \special command
};

struct DigestedNode {
    DigestedType type;
    
    // Source location (from tree-sitter)
    SourceLoc loc;
    
    // Font at time of digestion
    FontSpec font;
    
    // Linked list pointers (arena-allocated)
    DigestedNode* next;
    DigestedNode* prev;
    
    union {
        // BOX: text content
        struct {
            const char* text;
            size_t len;
            float width;    // Measured width (optional, for DVI)
        } box;
        
        // LIST: child nodes
        struct {
            DigestedNode* head;
            DigestedNode* tail;
            size_t count;
        } list;
        
        // WHATSIT: deferred construction
        struct {
            const CommandDef* definition;
            DigestedNode** args;
            size_t arg_count;
            PropertyMap* properties;
        } whatsit;
        
        // GLUE: stretchable space
        GlueSpec glue;
        
        // KERN: fixed space
        float kern;
        
        // PENALTY: break penalty
        int penalty;
        
        // RULE: filled rectangle
        struct {
            float width;
            float height;
            float depth;
        } rule;
        
        // INSERT: insertion
        struct {
            int insert_class;   // footnote=0, float=1, etc.
            DigestedNode* content;
        } insert;
    };
};
```

### 3.2 Digester (Stomach)

```cpp
// tex_digester.hpp

class Digester {
public:
    Digester(State* state, CommandRegistry* registry, Arena* arena);
    
    // Main digestion
    DigestedNode* digest(TokenList& tokens);
    
    // Mode management
    enum class Mode { VERTICAL, HORIZONTAL, MATH, INNER_MATH };
    Mode current_mode() const;
    void set_mode(Mode m);
    
    // Font state
    FontSpec current_font() const;
    void set_font(const FontSpec& font);
    
    // Counter management
    void step_counter(const char* name);
    int get_counter(const char* name) const;
    void set_counter(const char* name, int value);
    
    // Label/reference
    void set_label(const char* label);
    const char* resolve_ref(const char* label) const;
    
    // Footnote collection
    void add_footnote(DigestedNode* content);
    std::vector<DigestedNode*>& get_footnotes();
    
private:
    State* state;
    CommandRegistry* registry;
    Arena* arena;
    
    Mode mode;
    FontSpec font;
    
    // Counters
    std::unordered_map<std::string, int> counters;
    
    // Labels
    std::unordered_map<std::string, std::string> labels;
    
    // Footnotes for current page
    std::vector<DigestedNode*> footnotes;
};
```

---

## 4. Extensible Package Binding System

### 4.1 Command Definition Registry

```cpp
// tex_command_registry.hpp

enum class CommandType {
    MACRO,          // Simple text expansion (like DefMacro)
    PRIMITIVE,      // Side effect + optional box (like DefPrimitive)
    CONSTRUCTOR,    // Produces Whatsit for output (like DefConstructor)
    ENVIRONMENT,    // Begin/end pair (like DefEnvironment)
    MATH,           // Math-mode command (like DefMath)
};

struct CommandDef {
    const char* name;           // Command name (without \)
    CommandType type;
    
    // Parameter specification (LaTeXML-style)
    const char* params;         // "{}{}" or "[Default]{}" etc.
    
    // For MACRO type: replacement text
    const char* replacement;
    
    // For CONSTRUCTOR type: output pattern or callback
    union {
        const char* pattern;    // "<section>#1</section>"
        ConstructorFn callback; // C++ function pointer
    };
    
    // Hooks (like LaTeXML's afterDigest, beforeDigest)
    DigestHookFn before_digest;
    DigestHookFn after_digest;
    
    // Properties
    bool is_math;               // Only valid in math mode
    bool is_outer;              // Cannot be in arguments
};

class CommandRegistry {
public:
    void define_macro(const char* name, const char* params, const char* replacement);
    void define_primitive(const char* name, const char* params, PrimitiveFn fn);
    void define_constructor(const char* name, const char* params, const char* pattern);
    void define_environment(const char* name, const char* begin_pattern, const char* end_pattern);
    void define_math(const char* name, const char* meaning, const char* role);
    
    const CommandDef* lookup(const char* name) const;
    
    // Scoping
    void begin_group();
    void end_group();
    void make_global(const char* name);
};
```

### 4.2 Lambda Script Package Bindings

Allow packages to be defined in Lambda Script (`.pkg.ls` files):

```javascript
// packages/amsmath.pkg.ls

package "amsmath" {
    // Symbol definitions
    math_symbol "implies" => "⟹", role: "ARROW"
    math_symbol "iff" => "⟺", role: "ARROW"
    
    // Simple macro
    macro "text" [1] => "<text>#1</text>"
    
    // Environment with pattern
    environment "align" {
        begin => '<math-align display="block">'
        end => '</math-align>'
        properties => { numbered: true, multiline: true }
    }
    
    // Constructor with callback (references C++ function)
    constructor "frac" [2] => callback: math_fraction
    
    // Math operator
    math_operator "sum" {
        meaning: "sum"
        role: "SUMOP"
        limits: true
    }
}
```

### 4.3 Package Loading Mechanism

```cpp
// tex_package_loader.hpp

class PackageLoader {
public:
    PackageLoader(CommandRegistry* registry);
    
    void load_base_packages();
    void load_class(const char* class_name);
    void require_package(const char* pkg_name, const std::vector<std::string>& options);
    
    // Search paths
    void add_search_path(const char* path);
    
private:
    CommandRegistry* registry;
    std::vector<std::string> search_paths;
    std::set<std::string> loaded_packages;
    
    // Load order:
    // 1. Built-in TeX primitives (tex_base.pkg.ls)
    // 2. LaTeX base (latex_base.pkg.ls)
    // 3. Document class (article.cls.ls, book.cls.ls)
    // 4. Requested packages (amsmath.pkg.ls, etc.)
};
```

---

## 5. Package Implementation Plan

### 5.1 Priority Packages

Implement these commonly used packages in order:

| Priority | Package | Description | Complexity |
|----------|---------|-------------|------------|
| **P0** | TeX base | Primitives (\def, \if, \count, etc.) | High |
| **P0** | LaTeX base | \newcommand, environments, sectioning | High |
| **P0** | article/report/book | Document classes | Medium |
| **P1** | amsmath | Math environments, \frac, \binom, etc. | High |
| **P1** | amssymb | Math symbols (⊆, ∀, ∃, etc.) | Low |
| **P1** | amsfonts | Math fonts | Low |
| **P1** | graphicx | \includegraphics | Medium |
| **P1** | hyperref | \href, \url, \hyperref | Medium |
| **P2** | geometry | Page margins | Low |
| **P2** | xcolor | Color support | Medium |
| **P2** | listings/minted | Code listings | Medium |
| **P2** | tikz/pgf | Graphics | Very High |
| **P2** | biblatex/natbib | Bibliography | High |
| **P3** | booktabs | Better tables | Low |
| **P3** | siunitx | SI units | Medium |
| **P3** | cleveref | Cross-references | Medium |
| **P3** | enumitem | List customization | Low |
| **P3** | fancyhdr | Headers/footers | Low |

### 5.2 Base Package Definitions

#### 5.2.1 TeX Base (`tex_base.pkg.ls`)

```javascript
// packages/tex_base.pkg.ls

package "tex_base" {
    // ========================================================================
    // Grouping
    // ========================================================================
    primitive "begingroup" => callback: tex_begingroup
    primitive "endgroup" => callback: tex_endgroup
    primitive "bgroup" => callback: tex_begingroup
    primitive "egroup" => callback: tex_endgroup
    
    // ========================================================================
    // Definitions
    // ========================================================================
    primitive "def" => callback: tex_def
    primitive "edef" => callback: tex_edef
    primitive "gdef" => callback: tex_gdef
    primitive "xdef" => callback: tex_xdef
    primitive "let" => callback: tex_let
    primitive "futurelet" => callback: tex_futurelet
    
    // ========================================================================
    // Conditionals
    // ========================================================================
    primitive "if" => callback: tex_if
    primitive "ifx" => callback: tex_ifx
    primitive "ifnum" => callback: tex_ifnum
    primitive "ifdim" => callback: tex_ifdim
    primitive "ifodd" => callback: tex_ifodd
    primitive "ifeof" => callback: tex_ifeof
    primitive "iftrue" => callback: tex_iftrue
    primitive "iffalse" => callback: tex_iffalse
    primitive "ifcase" => callback: tex_ifcase
    primitive "else" => callback: tex_else
    primitive "fi" => callback: tex_fi
    primitive "or" => callback: tex_or
    
    // ========================================================================
    // Expansion Control
    // ========================================================================
    primitive "expandafter" => callback: tex_expandafter
    primitive "noexpand" => callback: tex_noexpand
    primitive "the" => callback: tex_the
    primitive "csname" => callback: tex_csname
    primitive "endcsname" => callback: tex_endcsname
    primitive "string" => callback: tex_string
    primitive "number" => callback: tex_number
    primitive "romannumeral" => callback: tex_romannumeral
    
    // ========================================================================
    // Registers
    // ========================================================================
    primitive "count" => callback: tex_count
    primitive "dimen" => callback: tex_dimen
    primitive "skip" => callback: tex_skip
    primitive "toks" => callback: tex_toks
    primitive "advance" => callback: tex_advance
    primitive "multiply" => callback: tex_multiply
    primitive "divide" => callback: tex_divide
    
    // ========================================================================
    // Boxes
    // ========================================================================
    primitive "hbox" => callback: tex_hbox
    primitive "vbox" => callback: tex_vbox
    primitive "vtop" => callback: tex_vtop
    primitive "box" => callback: tex_box
    primitive "setbox" => callback: tex_setbox
    primitive "wd" => callback: tex_wd
    primitive "ht" => callback: tex_ht
    primitive "dp" => callback: tex_dp
    
    // ========================================================================
    // Spacing
    // ========================================================================
    primitive "hskip" => callback: tex_hskip
    primitive "vskip" => callback: tex_vskip
    primitive "kern" => callback: tex_kern
    primitive "hfil" => callback: tex_hfil
    primitive "hfill" => callback: tex_hfill
    primitive "vfil" => callback: tex_vfil
    primitive "vfill" => callback: tex_vfill
    primitive "hss" => callback: tex_hss
    primitive "vss" => callback: tex_vss
    
    // ========================================================================
    // Rules
    // ========================================================================
    primitive "hrule" => callback: tex_hrule
    primitive "vrule" => callback: tex_vrule
    
    // ========================================================================
    // Penalties
    // ========================================================================
    primitive "penalty" => callback: tex_penalty
    
    // ========================================================================
    // Fonts
    // ========================================================================
    primitive "font" => callback: tex_font
    
    // ========================================================================
    // Special
    // ========================================================================
    primitive "special" => callback: tex_special
    primitive "write" => callback: tex_write
    primitive "message" => callback: tex_message
    primitive "errmessage" => callback: tex_errmessage
    primitive "relax" => callback: tex_relax
    primitive "ignorespaces" => callback: tex_ignorespaces
}
```

#### 5.2.2 LaTeX Base (`latex_base.pkg.ls`)

```javascript
// packages/latex_base.pkg.ls

package "latex_base" {
    requires "tex_base"
    
    // ========================================================================
    // Document Structure
    // ========================================================================
    constructor "documentclass" [1] {
        pattern => ""
        before_digest => callback: latex_documentclass
    }
    
    environment "document" {
        begin => '<document>'
        end => '</document>'
    }
    
    // ========================================================================
    // Sectioning
    // ========================================================================
    constructor "part" [1] {
        pattern => '<part><title>#1</title>'
        after_digest => callback: latex_section_counter
    }
    
    constructor "chapter" [1] {
        pattern => '<chapter><title>#1</title>'
        after_digest => callback: latex_section_counter
    }
    
    constructor "section" [1] {
        pattern => '<section><title>#1</title>'
        after_digest => callback: latex_section_counter
    }
    
    constructor "subsection" [1] {
        pattern => '<subsection><title>#1</title>'
        after_digest => callback: latex_section_counter
    }
    
    constructor "subsubsection" [1] {
        pattern => '<subsubsection><title>#1</title>'
        after_digest => callback: latex_section_counter
    }
    
    constructor "paragraph" [1] {
        pattern => '<paragraph><title>#1</title>'
    }
    
    // Starred variants (no number)
    constructor "section*" [1] {
        pattern => '<section class="unnumbered"><title>#1</title>'
    }
    
    // ========================================================================
    // Text Formatting
    // ========================================================================
    constructor "textbf" [1] => '<b>#1</b>'
    constructor "textit" [1] => '<i>#1</i>'
    constructor "texttt" [1] => '<code>#1</code>'
    constructor "textrm" [1] => '<span class="rm">#1</span>'
    constructor "textsf" [1] => '<span class="sf">#1</span>'
    constructor "textsc" [1] => '<span class="sc">#1</span>'
    constructor "emph" [1] => '<em>#1</em>'
    constructor "underline" [1] => '<u>#1</u>'
    
    // ========================================================================
    // Font Switches
    // ========================================================================
    primitive "bfseries" => callback: latex_bfseries
    primitive "itshape" => callback: latex_itshape
    primitive "ttfamily" => callback: latex_ttfamily
    primitive "rmfamily" => callback: latex_rmfamily
    primitive "sffamily" => callback: latex_sffamily
    primitive "scshape" => callback: latex_scshape
    primitive "normalfont" => callback: latex_normalfont
    
    // ========================================================================
    // Font Size
    // ========================================================================
    primitive "tiny" => callback: latex_fontsize, size: 5
    primitive "scriptsize" => callback: latex_fontsize, size: 7
    primitive "footnotesize" => callback: latex_fontsize, size: 8
    primitive "small" => callback: latex_fontsize, size: 9
    primitive "normalsize" => callback: latex_fontsize, size: 10
    primitive "large" => callback: latex_fontsize, size: 12
    primitive "Large" => callback: latex_fontsize, size: 14
    primitive "LARGE" => callback: latex_fontsize, size: 17
    primitive "huge" => callback: latex_fontsize, size: 20
    primitive "Huge" => callback: latex_fontsize, size: 25
    
    // ========================================================================
    // Lists
    // ========================================================================
    environment "itemize" {
        begin => '<ul>'
        end => '</ul>'
    }
    
    environment "enumerate" {
        begin => '<ol>'
        end => '</ol>'
    }
    
    environment "description" {
        begin => '<dl>'
        end => '</dl>'
    }
    
    constructor "item" [""] {
        pattern => '?#1(<dt>#1</dt><dd>)(<li>)'
    }
    
    // ========================================================================
    // Environments
    // ========================================================================
    environment "quote" {
        begin => '<blockquote class="quote">'
        end => '</blockquote>'
    }
    
    environment "quotation" {
        begin => '<blockquote class="quotation">'
        end => '</blockquote>'
    }
    
    environment "center" {
        begin => '<div class="center">'
        end => '</div>'
    }
    
    environment "flushleft" {
        begin => '<div class="left">'
        end => '</div>'
    }
    
    environment "flushright" {
        begin => '<div class="right">'
        end => '</div>'
    }
    
    environment "verbatim" {
        begin => '<pre>'
        end => '</pre>'
        verbatim => true
    }
    
    // ========================================================================
    // Math
    // ========================================================================
    environment "equation" {
        begin => '<math-block numbered="true">'
        end => '</math-block>'
        mode => "math"
    }
    
    environment "equation*" {
        begin => '<math-block>'
        end => '</math-block>'
        mode => "math"
    }
    
    // ========================================================================
    // Floats
    // ========================================================================
    environment "figure" {
        begin => '<figure>'
        end => '</figure>'
    }
    
    environment "table" {
        begin => '<table-float>'
        end => '</table-float>'
    }
    
    constructor "caption" [1] => '<caption>#1</caption>'
    
    // ========================================================================
    // Cross-references
    // ========================================================================
    constructor "label" [1] {
        pattern => ''
        after_digest => callback: latex_label
    }
    
    constructor "ref" [1] {
        pattern => '<ref target="#1"/>'
        after_digest => callback: latex_ref
    }
    
    constructor "pageref" [1] {
        pattern => '<pageref target="#1"/>'
    }
    
    // ========================================================================
    // Footnotes
    // ========================================================================
    constructor "footnote" [1] {
        pattern => '^<footnote>#1</footnote>'
        after_digest => callback: latex_footnote
    }
    
    // ========================================================================
    // Title
    // ========================================================================
    constructor "title" [1] {
        pattern => ''
        after_digest => callback: latex_store_title
    }
    
    constructor "author" [1] {
        pattern => ''
        after_digest => callback: latex_store_author
    }
    
    constructor "date" [1] {
        pattern => ''
        after_digest => callback: latex_store_date
    }
    
    constructor "maketitle" {
        pattern => '<titlepage/>'
        after_digest => callback: latex_maketitle
    }
    
    // ========================================================================
    // Spacing
    // ========================================================================
    macro "quad" => " "    // Em space
    macro "qquad" => "  "  // Two em spaces
    macro "," => " "       // Thin space
    macro ";" => " "       // Medium space
    macro ":" => " "       // Thick space (in math)
    macro "!" => ""        // Negative thin space
    
    constructor "hspace" [1] => '<hspace width="#1"/>'
    constructor "vspace" [1] => '<vspace height="#1"/>'
    
    // ========================================================================
    // Special Characters
    // ========================================================================
    macro "%" => "%"
    macro "&" => "&"
    macro "#" => "#"
    macro "$" => "$"
    macro "_" => "_"
    macro "{" => "{"
    macro "}" => "}"
    macro "~" => " "    // Non-breaking space
    macro "\\" => '<br/>'
    
    // ========================================================================
    // Newcommand (processed specially)
    // ========================================================================
    primitive "newcommand" => callback: latex_newcommand
    primitive "renewcommand" => callback: latex_renewcommand
    primitive "providecommand" => callback: latex_providecommand
    primitive "newenvironment" => callback: latex_newenvironment
}
```

#### 5.2.3 AMS Math (`amsmath.pkg.ls`)

```javascript
// packages/amsmath.pkg.ls

package "amsmath" {
    requires "latex_base"
    
    // ========================================================================
    // Display Math Environments
    // ========================================================================
    environment "align" {
        begin => '<math-align numbered="true">'
        end => '</math-align>'
        mode => "math"
    }
    
    environment "align*" {
        begin => '<math-align>'
        end => '</math-align>'
        mode => "math"
    }
    
    environment "gather" {
        begin => '<math-gather numbered="true">'
        end => '</math-gather>'
        mode => "math"
    }
    
    environment "gather*" {
        begin => '<math-gather>'
        end => '</math-gather>'
        mode => "math"
    }
    
    environment "multline" {
        begin => '<math-multline numbered="true">'
        end => '</math-multline>'
        mode => "math"
    }
    
    environment "split" {
        begin => '<math-split>'
        end => '</math-split>'
        mode => "math"
    }
    
    environment "cases" {
        begin => '<math-cases>'
        end => '</math-cases>'
        mode => "math"
    }
    
    environment "matrix" {
        begin => '<matrix>'
        end => '</matrix>'
        mode => "math"
    }
    
    environment "pmatrix" {
        begin => '<matrix delimiters="()">'
        end => '</matrix>'
        mode => "math"
    }
    
    environment "bmatrix" {
        begin => '<matrix delimiters="[]">'
        end => '</matrix>'
        mode => "math"
    }
    
    environment "Bmatrix" {
        begin => '<matrix delimiters="{}">'
        end => '</matrix>'
        mode => "math"
    }
    
    environment "vmatrix" {
        begin => '<matrix delimiters="||">'
        end => '</matrix>'
        mode => "math"
    }
    
    environment "Vmatrix" {
        begin => '<matrix delimiters="‖‖">'
        end => '</matrix>'
        mode => "math"
    }
    
    // ========================================================================
    // Fractions and Binomials
    // ========================================================================
    constructor "frac" [2] {
        pattern => '<frac><numer>#1</numer><denom>#2</denom></frac>'
        mode => "math"
    }
    
    constructor "dfrac" [2] {
        pattern => '<frac style="display"><numer>#1</numer><denom>#2</denom></frac>'
        mode => "math"
    }
    
    constructor "tfrac" [2] {
        pattern => '<frac style="text"><numer>#1</numer><denom>#2</denom></frac>'
        mode => "math"
    }
    
    constructor "binom" [2] {
        pattern => '<binom><top>#1</top><bottom>#2</bottom></binom>'
        mode => "math"
    }
    
    constructor "dbinom" [2] {
        pattern => '<binom style="display"><top>#1</top><bottom>#2</bottom></binom>'
        mode => "math"
    }
    
    constructor "tbinom" [2] {
        pattern => '<binom style="text"><top>#1</top><bottom>#2</bottom></binom>'
        mode => "math"
    }
    
    // ========================================================================
    // Roots
    // ========================================================================
    constructor "sqrt" [""][1] {
        pattern => '?#1(<root index="#1">#2</root>)(<sqrt>#2</sqrt>)'
        mode => "math"
    }
    
    // ========================================================================
    // Delimiters
    // ========================================================================
    constructor "left" => callback: ams_left
    constructor "right" => callback: ams_right
    constructor "big" [1] => '<delimiter size="big">#1</delimiter>'
    constructor "Big" [1] => '<delimiter size="Big">#1</delimiter>'
    constructor "bigg" [1] => '<delimiter size="bigg">#1</delimiter>'
    constructor "Bigg" [1] => '<delimiter size="Bigg">#1</delimiter>'
    
    // ========================================================================
    // Operators
    // ========================================================================
    math_operator "sin" { meaning: "sin", role: "TRIGFUNCTION" }
    math_operator "cos" { meaning: "cos", role: "TRIGFUNCTION" }
    math_operator "tan" { meaning: "tan", role: "TRIGFUNCTION" }
    math_operator "log" { meaning: "log", role: "FUNCTION" }
    math_operator "ln" { meaning: "ln", role: "FUNCTION" }
    math_operator "exp" { meaning: "exp", role: "FUNCTION" }
    math_operator "lim" { meaning: "limit", role: "LIMITOP", limits: true }
    math_operator "sum" { meaning: "sum", role: "SUMOP", limits: true }
    math_operator "prod" { meaning: "product", role: "SUMOP", limits: true }
    math_operator "int" { meaning: "integral", role: "INTOP" }
    math_operator "oint" { meaning: "contour-integral", role: "INTOP" }
    math_operator "max" { meaning: "maximum", role: "LIMITOP", limits: true }
    math_operator "min" { meaning: "minimum", role: "LIMITOP", limits: true }
    math_operator "sup" { meaning: "supremum", role: "LIMITOP", limits: true }
    math_operator "inf" { meaning: "infimum", role: "LIMITOP", limits: true }
    
    // ========================================================================
    // Arrows
    // ========================================================================
    math_symbol "implies" => "⟹", role: "ARROW"
    math_symbol "impliedby" => "⟸", role: "ARROW"
    math_symbol "iff" => "⟺", role: "ARROW"
    math_symbol "to" => "→", role: "ARROW"
    math_symbol "gets" => "←", role: "ARROW"
    math_symbol "leftrightarrow" => "↔", role: "ARROW"
    math_symbol "Leftrightarrow" => "⇔", role: "ARROW"
    
    // ========================================================================
    // Text in Math
    // ========================================================================
    constructor "text" [1] {
        pattern => '<text>#1</text>'
        mode => "math"
    }
    
    constructor "operatorname" [1] {
        pattern => '<operator>#1</operator>'
        mode => "math"
    }
    
    // ========================================================================
    // Alignment
    // ========================================================================
    macro "notag" => ""  // Suppress equation number
    macro "tag" [1] => ""  // Custom equation tag
    macro "intertext" [1] => ""  // Text between align rows
}
```

---

## 6. Constructor Pattern Language

### 6.1 Pattern Syntax

```cpp
// Pattern syntax (similar to LaTeXML):
// #1, #2, ...  - argument substitution
// ?#1(yes)(no) - conditional (if #1 non-empty)
// &fn(#1)      - function call
// ^<elem>      - float up to parent
// %#1          - expand map as attributes
// @attr=#1     - set attribute from argument

class PatternCompiler {
public:
    CompiledPattern* compile(const char* pattern);
    
    void execute(CompiledPattern* pattern, 
                 DigestedNode** args, 
                 size_t arg_count,
                 OutputBuilder* output);
};
```

### 6.2 Examples

```cpp
// Simple substitution
// '\textbf{}' → '<b>#1</b>'

// Conditional
// '\section[]{}' → '?#1(<section title="#1">#2</section>)(<section>#2</section>)'

// Float up (footnotes)
// '\footnote{}' → '^<footnote>#1</footnote>'

// Function call
// '\includegraphics{}' → '<img src="&resolve_path(#1)"/>'

// Attributes from map
// '\hyperref[opts]{}' → '<a %#1>#2</a>'
```

---

## 7. Testing Strategy

### 7.1 Test Fixtures from LaTeXML

Copy test files from LaTeXML to validate implementation:

```bash
# Create test directory
mkdir -p test/latexml/fixtures

# Copy key test files
cp -r latexml/t/expansion/*.tex test/latexml/fixtures/expansion/
cp -r latexml/t/math/*.tex test/latexml/fixtures/math/
cp -r latexml/t/structure/*.tex test/latexml/fixtures/structure/
cp -r latexml/t/ams/*.tex test/latexml/fixtures/ams/
cp -r latexml/t/digestion/*.tex test/latexml/fixtures/digestion/
cp -r latexml/t/grouping/*.tex test/latexml/fixtures/grouping/
```

### 7.2 Recommended Test Files

**Expansion Tests** (`test/latexml/fixtures/expansion/`):
| File | Tests |
|------|-------|
| `testexpand.tex` | Basic macro expansion |
| `testif.tex` | Conditional expansion |
| `noexpand.tex` | \noexpand, \expandafter |
| `noexpand_conditional.tex` | Conditionals with noexpand |
| `toks.tex` | Token register expansion |
| `env.tex` | Environment expansion |
| `etex.tex` | e-TeX extensions |
| `for.tex` | Loops |
| `ifthen.tex` | ifthen package |

**Math Tests** (`test/latexml/fixtures/math/`):
| File | Tests |
|------|-------|
| `simplemath.tex` | Basic inline/display math |
| `fracs.tex` | Fractions, binomials |
| `testscripts.tex` | Subscripts/superscripts |
| `array.tex` | Math arrays |
| `testover.tex` | \over, \atop, \choose |
| `arrows.tex` | Arrow symbols |
| `choose.tex` | Binomial coefficients |

**Structure Tests** (`test/latexml/fixtures/structure/`):
| File | Tests |
|------|-------|
| `article.tex` | Article document class |
| `book.tex` | Book document class |
| `sec.tex` | Sectioning commands |
| `itemize.tex` | Lists |
| `footnote.tex` | Footnotes |
| `figures.tex` | Figures and captions |
| `hyperref.tex` | Hyperlinks |
| `enum.tex` | Enumeration |

**AMS Tests** (`test/latexml/fixtures/ams/`):
| File | Tests |
|------|-------|
| `amsdisplay.tex` | align, gather, etc. |
| `matrix.tex` | Matrix environments |
| `genfracs.tex` | Generalized fractions |
| `sideset.tex` | Sideset operators |

### 7.3 Reference Generation Script

```bash
#!/bin/bash
# scripts/generate_latexml_references.sh
#
# Generate XML and HTML references using LaTeXML

FIXTURE_DIR="test/latexml/fixtures"
OUTPUT_DIR="test/latexml/expected"

mkdir -p "$OUTPUT_DIR"/{expansion,math,structure,ams}

for category in expansion math structure ams; do
    for tex_file in "$FIXTURE_DIR/$category"/*.tex; do
        base=$(basename "$tex_file" .tex)
        
        echo "Processing $category/$base..."
        
        # Generate XML
        latexml --dest="$OUTPUT_DIR/$category/$base.xml" "$tex_file" 2>/dev/null
        
        # Generate HTML
        latexmlpost --dest="$OUTPUT_DIR/$category/$base.html" \
            --format=html5 \
            "$OUTPUT_DIR/$category/$base.xml" 2>/dev/null
    done
done

echo "Reference generation complete."
```

### 7.4 Test Runner

```cpp
// test/test_latex_pipeline_gtest.cpp

#include <gtest/gtest.h>
#include "tex_tokenizer.hpp"
#include "tex_expander.hpp"
#include "tex_digester.hpp"

class LaTeXPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        arena = arena_create(1024 * 1024);
        catcodes = new CatCodeTable(CatCodeTable::latex_default());
        state = new State(arena);
        registry = new CommandRegistry();
        
        // Load base packages
        loader = new PackageLoader(registry);
        loader->load_base_packages();
    }
    
    void TearDown() override {
        delete loader;
        delete registry;
        delete state;
        delete catcodes;
        arena_destroy(arena);
    }
    
    // Compare output with LaTeXML reference
    void compare_with_reference(const char* fixture_path);
    
    Arena* arena;
    CatCodeTable* catcodes;
    State* state;
    CommandRegistry* registry;
    PackageLoader* loader;
};

TEST_F(LaTeXPipelineTest, TestExpand_BasicMacro) {
    compare_with_reference("test/latexml/fixtures/expansion/testexpand.tex");
}

TEST_F(LaTeXPipelineTest, TestExpand_Conditionals) {
    compare_with_reference("test/latexml/fixtures/expansion/testif.tex");
}

TEST_F(LaTeXPipelineTest, TestMath_Simple) {
    compare_with_reference("test/latexml/fixtures/math/simplemath.tex");
}

TEST_F(LaTeXPipelineTest, TestMath_Fractions) {
    compare_with_reference("test/latexml/fixtures/math/fracs.tex");
}

TEST_F(LaTeXPipelineTest, TestStructure_Article) {
    compare_with_reference("test/latexml/fixtures/structure/article.tex");
}

TEST_F(LaTeXPipelineTest, TestAMS_Display) {
    compare_with_reference("test/latexml/fixtures/ams/amsdisplay.tex");
}
```

---

## 8. Implementation Roadmap

### Phase 1: Tokenizer & Expander (4 weeks)
1. Implement `CatCodeTable` and `Token` types
2. Implement `Tokenizer` (Mouth)
3. Implement basic `Expander` (Gullet) with macro expansion
4. Add conditionals (\if, \ifx, \ifnum, etc.)
5. Add expansion primitives (\expandafter, \noexpand, \the)
6. **Test**: expansion fixture tests pass

### Phase 2: Command Registry (2 weeks)
1. Implement `CommandRegistry` with basic types
2. Define TeX primitives in data tables
3. Create `tex_base.pkg.ls`
4. **Test**: primitive commands work

### Phase 3: Digester (3 weeks)
1. Implement `DigestedNode` types
2. Implement `Digester` (Stomach)
3. Font state tracking
4. Counter management
5. **Test**: digestion produces correct IR

### Phase 4: LaTeX Base (3 weeks)
1. Create `latex_base.pkg.ls`
2. Sectioning commands
3. Text formatting
4. Lists and environments
5. Cross-references
6. **Test**: structure fixture tests pass

### Phase 5: Math Support (3 weeks)
1. Create `amsmath.pkg.ls`
2. Math environments (align, gather, etc.)
3. Fractions, binomials
4. Matrix environments
5. Math operators and symbols
6. **Test**: math fixture tests pass

### Phase 6: Output Generation (2 weeks)
1. HTML output from digested IR
2. Pattern compiler for constructors
3. **Test**: HTML matches LaTeXML output

### Phase 7: Additional Packages (ongoing)
1. graphicx
2. hyperref
3. xcolor
4. listings
5. More as needed

---

## 9. Document Model Enhancement

### 9.1 Schema Awareness

```cpp
// tex_document_model_v2.hpp

struct ElementSchema {
    const char* name;
    
    // Content model
    std::vector<const char*> allowed_children;
    std::vector<const char*> required_children;
    
    // Auto behavior (like LaTeXML Tag properties)
    bool auto_open;         // Can be auto-opened when needed
    bool auto_close;        // Can be auto-closed when parent closes
    
    // Hooks
    ElementHookFn after_open;
    ElementHookFn after_close;
};

class DocumentModel {
public:
    void register_element(const ElementSchema& schema);
    
    // Smart insertion (auto-open/close as needed)
    void insert_element(const char* name, DigestedNode* content);
    
    // Validates structure during construction
    bool is_valid_child(const char* parent, const char* child) const;
};
```

---

## 10. Error Handling

```cpp
// tex_errors.hpp

enum class ErrorCategory {
    PARSE,          // Syntax errors
    UNDEFINED,      // Undefined command/environment
    EXPANSION,      // Macro expansion errors
    DIGESTION,      // Digestion phase errors
    CONSTRUCTION,   // Output construction errors
    PACKAGE,        // Package loading errors
};

struct TeXError {
    ErrorCategory category;
    const char* item;       // Command or token name
    SourceLoc loc;          // Source location
    const char* message;
    
    // Chain for "caused by" errors
    TeXError* cause;
};

void report_error(ErrorCategory cat, const char* item, SourceLoc loc, 
                  const char* format, ...);
void report_warning(ErrorCategory cat, const char* item, SourceLoc loc,
                    const char* format, ...);
```

---

## 11. Summary

This proposal outlines a comprehensive enhancement to Lambda's LaTeX pipeline:

1. **Full macro expansion** with catcode-aware tokenization, conditionals, and TeX expansion primitives
2. **Digestion phase** to capture document semantics before output
3. **Extensible package system** using declarative `.pkg.ls` files
4. **Pattern language** for constructor output specification
5. **Comprehensive testing** using LaTeXML test fixtures as reference

The implementation is phased over approximately 17 weeks, with each phase producing testable, incremental functionality.
