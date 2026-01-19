# LaTeX to HTML V2 - Design Document

**Date**: December 23, 2025  
**Status**: ✅ **Complete** (**Baseline: 97/101 tests pass, 4 skipped**)  
**Objective**: Translate LaTeX.js formatting logic to C++ for Lambda runtime

**Recent Progress** (Dec 23 - Session 3):
- **environments_tex_10 FIXED**: Font environment class stacking now produces sibling spans with ZWS
  - Problem: `\itshape{\bfseries\slshape text}` was producing nested `<span>` hierarchy
  - Solution: Implemented font environment class stack (`pushFontEnvClass()`/`popFontEnvClass()`)
    - Track current font environment class in a stack
    - When entering nested font environment, close current span with ZWS
    - Start new span with combined classes (parent + child)
    - On exit, restore previous class with new span
  - Impact: environments_tex_10 now passes
- **environments_tex_14 FIXED**: Comment environment now correctly handles content
  - Problem: Content between `\begin{comment}` and `\end{comment}` was being output
  - Solution: Updated `env_comment` handler to skip all children (produce no output)
  - Impact: environments_tex_14 now passes
- **whitespace_tex_5 FIXED**: `\mbox{}` content starting with newline now gets ZWS prefix
  - Problem: `\mbox{\n...}` should have ZWS at start of output, didn't
  - Solution: 
    - Modified input-latex-ts.cpp `space` node handling to preserve `"\n"` vs `" "` distinction
    - Added `restricted_h_mode_first_text_` flag to track first text in restricted h-mode
    - Output ZWS when first text in mbox starts with newline
  - Impact: whitespace_tex_5 now passes, moved from extended to baseline
- **Test Suite Finalization**:
  - All tests consolidated into single baseline suite (extended test suite deleted)
  - Baseline: 97 tests pass, 4 skipped (match original LaTeX.js skipped tests)
  - Skipped tests: environments_tex_13, whitespace_tex_6, whitespace_tex_7, whitespace_tex_8
- **Skipped Tests** (4 tests - all marked with `!` in original LaTeX.js):
  - **environments_tex_13**: `\unskip` breaks out of groups (complex TeX primitive)
  - **whitespace_tex_6**: `\unskip`/`\ignorespaces` group semantics (complex TeX primitive)
  - **whitespace_tex_7** ("a space is added after macros that take arguments"):
    - Expected output has 3 parts but input has 5 lines
    - `\echoOGO{}` and `\echoO[]` lines completely missing from expected
    - Doesn't match documented latex-js echo package behavior
    - Appears to be aspirational test that latex-js itself doesn't pass
  - **whitespace_tex_8** ("space hack to prevent multiple spaces"):
    - Expects `\vspace{1mm}` and `\marginpar{test}` to produce NO visible HTML
    - But passing `spacing.tex` tests expect `\vspace` to produce `<span>` output
    - Conflicting requirements - cannot satisfy both without mode-specific behavior

**Previous Progress** (Dec 23 - Session 2):
- **text_tex_10 FIXED**: Paragraph alignment now applies at paragraph END, not declaration time
  - Problem: `\centering` was immediately ending paragraph and setting alignment for next one
  - Solution: Implemented paragraph content buffering using HtmlGenerator's capture mode
    - `ensureParagraph()` now starts capture mode instead of writing `<p>` immediately
    - Content is buffered until `endParagraph()` is called
    - At paragraph close, content is wrapped with `<p class="alignment">...</p>`
  - Added stacked capture support in HtmlGenerator (nested `startCapture()`/`endCapture()`)
  - Fixed cmd_abstract to call `closeParagraphIfOpen()` before closing div
  - Impact: text_tex_10 now passes

**Previous Progress** (Dec 23 - Session 1):
- **Parbreak Symbol Handling**: Fixed `parbreak` symbols inside groups not converting to `<br>`
  - Problem: `\@hangfrom{\textbf{Some text.}}More text` was outputting `parbreak` as literal text
  - Solution: Added `outputGroupContent()` helper that processes group children with parbreak→`<br>` conversion
  - Related fix: `ItemReader::cstring()` now returns symbol content (was returning null for symbols)
  - Impact: `macros_tex_6` now passes
- **Test Suite Reorganization**: Moved 5 passing tests from extended to baseline
  - `groups.tex` tests 2, 3 (error brack_group handling, paragraph breaks in groups)
  - `macros.tex` tests 4, 5, 6 (sibling lookahead, ensureParagraph, parbreak handling)

**Previous Progress** (Dec 22):
- **Typographic Hyphen Conversion**: Single ASCII hyphens now converted to Unicode typographic hyphen (U+2010)
  - Problem: Text like `daughter-in-law` was keeping ASCII hyphens instead of using proper typographic hyphens
  - Solution: Updated `convertApostrophes()` to convert single `-` to `‐` (U+2010) when not in monospace mode
  - Impact: `text_tex_4` and `text_tex_6` now pass (dashes and special characters tests)
  - Note: `spacing_tex_1` moved to extended due to fixture needing Unicode thin space updates
- **Test Suite Reorganization**: Updated baseline/extended test classifications
  - `text.tex` tests 4, 6 promoted from extended to baseline (now passing)
  - `spacing.tex` test 1 moved to extended (fixture format issue with Unicode spaces)

**Previous Progress** (Jan 8):
- **Zero-Width Space (ZWS) for Empty Curly Groups**: Added ZWS output for commands with empty curly group terminators
  - Problem: Commands like `\^{}`, `\~{}`, `\textbackslash{}` were not outputting ZWS for visual separation
  - Solution: Updated `processDiacritic()` and `processChildren()` diacritic handling to output ZWS (U+200B) after empty curly_group
  - Impact: `text_tex_5` (special characters) moved from extended to baseline
  - Note: `basic_text_tex_4` expects no ZWS (older test fixture) - kept in extended for now
- **Special Characters - Curly Group Terminator Fix**: Fixed space consumption after commands with empty curly group terminators
  - Problem: `\textbackslash{} \%` was outputting `\%` instead of `\ %` (space stripped)
  - Root cause: `processChildren()` was consuming leading space after all commands, ignoring curly group terminators
  - Solution: Added `hasEmptyCurlyGroupChild()` check to `skip_space_consumption` logic in processChildren
  - Impact: Commands like `\textbackslash{}`, `\ss{}`, `\i{}` now correctly preserve following space when using `{}` terminator
  - Test promoted: `basic_text_tex_4` (special characters) moved from extended to baseline
- **Test Status**: Baseline improved from 65/65 to 66/66, extended reduced from 42 to 41

**Previous Progress** (Dec 18):
- **Test Suite Split**: Separated tests into baseline (must pass 100%) and extended (work-in-progress)
- **Whitespace Commands**: Fixed 5 of 6 "space hack" commands in whitespace_tex_8
  - Made pagebreak, nopagebreak, enlargethispage completely silent (no HTML output)
  - Implemented marginpar, index, glossary as no-ops (previously undefined)
  - vspace conflict: baseline requires output, extended requires silence (prioritized baseline)
- Previous fixes: `\char` numeric parsing, controlspace parser rule, paragraph breaks
- Main remaining issues: vspace context-awareness, macro argument space consumption

---

## 1. Overview

### Project Goal

Port the [LaTeX.js](https://github.com/michael-brade/LaTeX.js) HTML formatter from JavaScript to C++ as `format_latex_html_v2()`, enabling Lambda to convert LaTeX documents to HTML with high fidelity.

### Translation Approach

**File-by-file, function-by-function translation** from LaTeX.js to C++ for:
- Easy verification against original JavaScript code
- Straightforward debugging (compare outputs at each stage)
- Incremental feature implementation
- Clear mapping between source and target

### Architecture Stack

```
LaTeX Source (.tex)
    ↓
Tree-sitter Parser (grammar: lambda/tree-sitter-latex/grammar.js)
    ↓
Lambda Element Tree (Lambda data structures)
    ↓
V2 Formatter (format_latex_html_v2.cpp - C++ translation of LaTeX.js)
    ↓
HTML Output (string or Element tree)
```

---

## 2. Parser Layer: Tree-sitter to Lambda

### Tree-sitter Grammar

**File**: `lambda/tree-sitter-latex/grammar.js`

- Defines LaTeX syntax rules (commands, environments, groups, etc.)
- Generates C parser: `lambda/tree-sitter-latex/src/parser.c`
- Produces Concrete Syntax Tree (CST) with node types

### Parser Integration

**File**: `lambda/input/input-latex-ts.cpp`

**Function**: `parse_latex_ts(Input* input, const char* source, size_t len)`

**Process**:
1. Tree-sitter parses LaTeX source → CST
2. Lambda parser walks CST nodes
3. Converts to Lambda Element tree using `MarkBuilder`

**Key Conversion Rules**:

| Tree-sitter Node | Lambda Representation | Example |
|------------------|----------------------|---------|
| `command` with children | `Element` | `\textbf{text}` → `<textbf>` element |
| `command` without children | `Symbol` | `\newpage` → symbol "newpage" |
| `text` | `String` | Plain text content |
| `group` (`{...}`) | `Element` with tag "curly_group" | Argument grouping |
| `environment` | `Element` with tag = env name | `\begin{itemize}` → `<itemize>` |

**Terminal nodes** (no children) become **Symbols**:
```cpp
// Example: \textbf has children (the text argument)
<textbf>  // Element with tag "textbf"
    "Bold text"  // String child

// Example: \newpage has no children
Symbol("newpage")  // Just a symbol
```

**Special Cases**:
- `command_name` tokens: Extract text as String (e.g., `\greet` → `"greet"`)
- `color_reference`: Specialized handler for color commands
- `graphics_include`: Structured option parsing for images
- `verb_command`: External scanner for verbatim text (see below)

### External Scanner: `\verb` Command Implementation

The `\verb` command presents unique parsing challenges because it uses **arbitrary delimiters** and treats content as **literal text** (no interpretation of backslashes, braces, or other LaTeX syntax). This cannot be expressed in Tree-sitter's grammar alone and requires an **external scanner**.

#### Pattern 1: Context-Gated External Token

We use **Pattern 1** from Tree-sitter's external scanner design patterns:

**Pattern Definition**: External scanner only emits token when `valid_symbols[TOKEN]` is true (i.e., parser makes it valid in current state). The scanner checks this immediately and returns false if not valid.

**Why Pattern 1?**
- Tree-sitter's lexer would normally tokenize `\verb` as `command_name` before considering the external scanner
- By declaring `verb_command` as a specific alternative in the grammar (not just an external token), the parser makes `VERB_COMMAND` valid in `_inline` contexts
- When valid, scanner takes precedence and matches the full `\verb<delim>content<delim>` pattern

#### Grammar Setup

**File**: `lambda/tree-sitter-latex/grammar.js`

```javascript
externals: $ => [
  // ... other externals
  $._verb_command,  // Line 25: Declare external token
],

conflicts: $ => [
  // ... other conflicts
  [$.verb_command, $.command],  // Lines 32-33: GLR disambiguation
],

// Lines 140-147: Rule using external token
verb_command: $ => $._verb_command,  // Pattern 1: context-gated

_inline: $ => choice(
  $.verb_command,    // Line 107: Place BEFORE $.command
  $.command,         // Regular commands come after
  // ... other inline elements
),
```

**Key Points**:
1. **External declaration**: `$._verb_command` in externals array
2. **Conflict resolution**: Tell Tree-sitter `verb_command` and `command` can conflict (enables GLR)
3. **Context placement**: Put `verb_command` in `_inline` choice **before** `command`
4. **Simple rule**: `verb_command: $ => $._verb_command` (just references external token)

#### External Scanner Implementation

**File**: `lambda/tree-sitter-latex/src/scanner.c`

```c
// Line 12: Token type enum
enum TokenType {
  // ... other tokens
  VERB_COMMAND,
};

// Lines 190-238: Scanner function
static bool scan_verb_command(TSLexer *lexer) {
  // Must start with backslash
  if (lexer->lookahead != '\\') return false;
  lexer->advance(lexer, false);
  
  // Must have "verb"
  if (!try_match_string(lexer, "verb")) return false;
  
  // Optional star: \verb*
  if (lexer->lookahead == '*') {
    lexer->advance(lexer, false);
  }
  
  // Capture delimiter (next character)
  unsigned char delimiter = lexer->lookahead;
  if (delimiter == '\n' || delimiter == '\r') return false;  // No newline delimiters
  lexer->advance(lexer, false);
  
  // Scan until matching delimiter
  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == delimiter) {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);  // Success!
      return true;
    }
    
    // Verbatim content cannot span lines
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      return false;
    }
    
    lexer->advance(lexer, false);
  }
  
  return false;  // EOF before closing delimiter
}

// Lines 260-267: Pattern 1 gate
bool tree_sitter_latex_external_scanner_scan(
    void *payload, TSLexer *lexer, const bool *valid_symbols
) {
  // Pattern 1: Check if parser wants this token
  if (valid_symbols[VERB_COMMAND] && lexer->lookahead == '\\') {
    if (scan_verb_command(lexer)) {
      lexer->result_symbol = VERB_COMMAND;
      return true;
    }
  }
  
  // ... other token types
  return false;
}
```

**Pattern 1 Gate**: `if (valid_symbols[VERB_COMMAND])` - only try scanning if parser makes token valid

#### AST Builder Integration

**File**: `lambda/input/input-latex-ts.cpp`

```cpp
// Line 50: Node type classification
{"verb_command", NODE_CONTAINER},

// Lines 394-406: Special handling
if (strcmp(node_type, "verb_command") == 0) {
  // Extract full token text (e.g., "\verb|text|")
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  const char* text = source + start;
  size_t len = end - start;
  
  // Create element with tag "verb_command"
  ElementBuilder elem = builder.element("verb_command");
  
  // Store token as string child (not attribute - more reliable)
  Item token_string = builder.createStringItem(text, len);
  elem.child(token_string);
  
  return elem.final();
}
```

**Design Decision**: Store full token text (`\verb|text|`) as a **string child** rather than attribute, because:
- Attributes had type issues (stored as null type)
- Child extraction via `childAt(0)` is more reliable
- Allows formatter to parse delimiter and content

#### Formatter Implementation

**File**: `lambda/format/format_latex_html_v2.cpp`

```cpp
// Lines 2715-2760: Handler function
static void cmd_verb_command(LatexProcessor* proc, Item elem) {
  HtmlGenerator* gen = proc->generator();
  ElementReader elem_reader(elem);
  
  // Get token text from first child
  ItemReader first_child = elem_reader.childAt(0);
  const char* text = first_child.cstring();  // "\verb|text|" or "\verb+text+"
  
  // Parse token structure
  // Skip "\verb" prefix (5 characters)
  const char* delimiter_start = text + 5;
  
  // Skip optional star (if present)
  if (*delimiter_start == '*') {
    delimiter_start++;
  }
  
  // Extract delimiter character
  char delim = *delimiter_start;
  
  // Find content between delimiters
  const char* content_start = delimiter_start + 1;
  const char* content_end = strchr(content_start, delim);
  
  if (!content_end) {
    // Malformed - missing closing delimiter
    gen->text(text);
    return;
  }
  
  size_t content_len = content_end - content_start;
  
  // Output: <code class="latex-verbatim">content</code>
  gen->writer()->openTagRaw("code", "class=\"latex-verbatim\"");
  std::string content(content_start, content_len);
  gen->writer()->writeText(content.c_str());
  gen->writer()->closeTag("code");
}

// Line 4867: Registration
command_table_["verb_command"] = cmd_verb_command;
```

**Output**: `\verb|text|` → `<code class="latex-verbatim">text</code>`

#### Testing

**Multiple Delimiters**:
```latex
\verb|pipe delimiter|     → <code class="latex-verbatim">pipe delimiter</code>
\verb+plus delimiter+     → <code class="latex-verbatim">plus delimiter</code>
\verb/slash delimiter/    → <code class="latex-verbatim">slash delimiter</code>
\verb*|visible spaces|    → <code class="latex-verbatim">visible spaces</code>
```

**Test Results**: All delimiter types work correctly ✅

#### Why This Approach Works

**Previous Failure**: Without Pattern 1 setup, `valid_symbols[VERB_COMMAND]` was always 0 because:
- Lexer tokenized `\verb` as `command_name` immediately
- Parser never considered `verb_command` as alternative
- External scanner never called

**Pattern 1 Solution**:
1. Grammar declares `verb_command` as alternative in `_inline` (line 107)
2. Parser tries both `verb_command` and `command` paths (GLR)
3. When trying `verb_command`, sets `valid_symbols[VERB_COMMAND] = true`
4. Scanner sees valid flag, matches full pattern `\verb<delim>content<delim>`
5. Returns `VERB_COMMAND` token to parser
6. Parser selects `verb_command` path (GLR resolution)

**Key Insight**: External scanners work through **parser context**, not by overriding the lexer globally.

#### Limitations & Future Work

**Current Implementation**:
- ✅ Arbitrary delimiters (any character except newline)
- ✅ Star variant: `\verb*` (parsed but not differently formatted)
- ✅ Multi-word content
- ✅ Special characters in content

**Not Yet Implemented**:
- ❌ Visible spaces for `\verb*` (currently treated same as `\verb`)
- ❌ `verbatim` environment (different parser challenge)
- ❌ Error recovery for missing closing delimiter (currently fails silently)

**Alternative Approaches Considered**:
- **Lexer precedence**: Can't work - Tree-sitter lexer scans before parser context
- **Grammar-only solution**: Can't work - need to match arbitrary delimiters dynamically
- **Pattern 2 (Lexer Context)**: Overkill - Pattern 1 sufficient for this use case

### Viewing the Lambda Tree

**Command**: 
```bash
./lambda.exe convert document.tex -f latex -t mark -o output.mark
```

This outputs the Lambda Element tree in Mark format, showing:
- Element tags (e.g., `<textbf>`, `<itemize>`)
- String content
- Symbol nodes
- Tree structure with indentation

**Use Case**: Debug parser output before formatter processing

---

## 3. Formatter Layer: LaTeX.js Translation

### Source Files Mapping

| LaTeX.js File | Lambda V2 File | Status |
|---------------|----------------|--------|
| `latex.ltx.ls` | `format_latex_html_v2.cpp` | ✅ Phases 1-5 complete |
| `html-generator.ls` | Existing `HtmlGenerator` class | ✅ Reused |
| `generator.ls` | `LatexProcessor` class | ✅ Custom implementation |
| `symbols.ls` | Inline in command handlers | ✅ Partial (diacritics, escapes) |
| `documentclasses/*.ls` | ⏳ Future | Pending |
| `css/*.css` | ⏳ Future | Pending |

### Core Architecture

**File**: `lambda/format/format_latex_html_v2.cpp`

**Main Components**:

```cpp
// 1. Entry point (called by Lambda)
Item format_latex_html_v2(Input* input, bool text_mode);

// 2. Processor class (state management, command dispatch)
class LatexProcessor {
private:
    HtmlGenerator* gen_;                           // HTML output
    Pool* pool_;                                   // Memory management
    Input* input_;                                 // Input context
    std::map<std::string, CommandFunc> command_table_;  // Command registry
    std::map<std::string, MacroDefinition> macro_table_; // User macros
    
public:
    void process(Item root);                       // Main processing
    void processNode(Item node);                   // Single node
    void processChildren(Item elem);               // Recurse children
    void processCommand(const char* cmd_name, Item elem);  // Command dispatch
};

// 3. Command handler function type
typedef void (*CommandFunc)(LatexProcessor* proc, Item elem);

// 4. Individual command handlers (translate from LaTeX.js)
static void cmd_textbf(LatexProcessor* proc, Item elem);
static void cmd_section(LatexProcessor* proc, Item elem);
static void cmd_begin(LatexProcessor* proc, Item elem);
// ... ~75+ command handlers
```

### Translation Pattern

**LaTeX.js Function** (JavaScript):
```javascript
generator.macro('textbf', function(groupsAndItems) {
    var text = this.create(groupsAndItems[0]);
    return this.createHtml('span', { 'class': 'bf' }, text);
});
```

**Lambda V2 Translation** (C++):
```cpp
static void cmd_textbf(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    // Open <span class="bf">
    gen->span("class=\"bf\"");
    
    // Process children (the text inside \textbf{...})
    proc->processChildren(elem);
    
    // Close </span>
    gen->closeElement();
}
```

**Key Translation Principles**:
1. **One JS macro → One C++ handler function**
2. **Preserve logic flow**: Open tag → Process content → Close tag
3. **Use Lambda APIs**: `gen->span()`, `proc->processChildren()`, etc.
4. **Memory safety**: Pool-based allocation, no manual malloc/free

### Command Dispatch Pattern

```cpp
void LatexProcessor::initCommandTable() {
    // Register all command handlers
    command_table_["textbf"] = cmd_textbf;
    command_table_["textit"] = cmd_textit;
    command_table_["section"] = cmd_section;
    // ... ~75+ commands
}

void LatexProcessor::processCommand(const char* cmd_name, Item elem) {
    // Look up handler
    auto it = command_table_.find(cmd_name);
    if (it != command_table_.end()) {
        // Call handler function
        it->second(this, elem);
    } else {
        // Unknown command - output as text
        gen_->text("\\");
        gen_->text(cmd_name);
    }
}
```

**Benefits**:
- O(1) lookup via hash map
- Easy to add new commands
- Clear separation of concerns
- Testable individual handlers

### Special Element Handling

**Linebreak in Restricted Mode (`\\` in `\mbox{}`)** (Fixed December 17, 2025):

In LaTeX restricted horizontal mode (inside `\mbox`, `\fbox`), linebreaks cannot actually break lines but still perform whitespace manipulation:

```cpp
if (restricted_h_mode_) {
    bool had_trailing_ws = gen_->hasTrailingWhitespace();
    gen_->trimTrailingWhitespace();  // LaTeX \unskip behavior
    
    // Special case: \\[dim] with surrounding whitespace preserves one space
    // Dimension indicates intentional vertical spacing request
    if (strcmp(tag, "linebreak") == 0 && has_dimension && 
        had_trailing_ws && next_has_leading_ws) {
        gen_->text(" ");  // Preserve word separation
    }
    strip_next_leading_space_ = true;  // LaTeX skip behavior
}
```

**Rules**:
1. `\linebreak[N]` (with argument): Always outputs exactly one space
2. `\\[dim]` with whitespace both sides: Preserve one space (dimension = intentional spacing)
3. `\\` without dimension OR no surrounding whitespace: Collapse all whitespace
4. `\newline`: Always collapse all whitespace

**Why Fixture Expectations Appear Conflicting**:
- `sp \\ ace` → `space` (no dimension, collapse all)
- `one \\[4cm] space` → `one space` (has dimension + whitespace, preserve one)

Both are correct! The dimension argument changes the semantic intent from "just break line" to "intentional spacing", so one space is preserved to maintain word separation. This matches LaTeX.js behavior where `unskip_macro` in the parser consumes preceding spaces, but `\\[dim]` is treated specially.

**curly_group (`{...}`) - Zero-Width Space Logic**:

Curly groups require special handling for spacing. The formatter outputs a zero-width space (ZWS) to maintain proper text flow, but only under specific conditions:

```cpp
if (strcmp(cmd_name, "curly_group") == 0) {
    gen_->enterGroup();
    
    // Detect if group is empty (no non-whitespace content)
    bool is_empty_group = true;
    // ... iterate children checking for content
    
    // Process children
    processChildren(elem);
    gen_->exitGroup();
    
    // ZWS at exit for depth <= 2, but NOT for empty groups
    if (gen_->groupDepth() <= 2 && !is_empty_group) {
        gen_->text("\xe2\x80\x8b");  // Unicode ZWS
    }
    return;
}
```

**Rules**:
1. **Empty groups** (e.g., `\^{}`, `\~{}`): No ZWS output - prevents spurious characters after diacritics
2. **Non-empty groups at depth ≤ 2**: Output ZWS to maintain word boundaries
3. **Deep nested groups** (depth > 2): No ZWS to avoid excessive spacing

---

## 4. Debug & Test Strategy

### 4.1 Parser Debugging

**View Lambda Tree**:
```bash
# Convert LaTeX to Mark format (shows parsed tree)
./lambda.exe convert test.tex -f latex -t mark -o test.mark

# View the tree structure
cat test.mark
```

**Expected Output**:
```
<document>
    <textbf>
        "Bold text"
    <section>
        "Introduction"
```

**Use Case**: Verify parser correctly converts Tree-sitter CST to Lambda Elements

---

## 3.1 Dual-Mode Output Architecture: HtmlWriter

### Design Overview

The V2 formatter supports **two output modes** via the `HtmlWriter` abstract interface:

1. **Text Mode**: Generate HTML as a string (using `StringBuf`)
2. **Node Mode**: Generate HTML as Lambda Element tree (using `MarkBuilder`)

This dual-mode design enables:
- **Text output** for direct file writing or display
- **Node output** for further processing in Lambda pipelines
- **Same formatter logic** for both modes (single code path)
- **Testable** output in both forms

### HtmlWriter Interface

**File**: `lambda/format/html_writer.hpp` (used by HtmlGenerator)

```cpp
// Abstract base class for HTML output
class HtmlWriter {
public:
    virtual ~HtmlWriter() {}
    
    // Core output methods
    virtual void writeStartElement(const char* tag) = 0;
    virtual void writeEndElement() = 0;
    virtual void writeAttribute(const char* name, const char* value) = 0;
    virtual void writeText(const char* text) = 0;
    virtual void writeComment(const char* text) = 0;
    
    // Get final result
    virtual Item getResult() = 0;
};
```

### Text Mode: TextHtmlWriter

**Implementation**: `lambda/format/html_writer.cpp`

**Storage**: Uses `StringBuf* sb_` from `lib/strbuf.h`

**Operation**:
```cpp
class TextHtmlWriter : public HtmlWriter {
private:
    StringBuf* sb_;      // Dynamic string buffer
    Pool* pool_;         // Memory pool
    bool pretty_print_;  // Formatting flag
    int indent_level_;   // Current indentation
    
public:
    void writeStartElement(const char* tag) override {
        // Append "<tag>" to string buffer
        if (pretty_print_) {
            stringbuf_append_indent(sb_, indent_level_);
        }
        stringbuf_append_char(sb_, '<');
        stringbuf_append_str(sb_, tag);
        stringbuf_append_char(sb_, '>');
        indent_level_++;
    }
    
    void writeText(const char* text) override {
        // Escape HTML entities and append
        stringbuf_append_html_escaped(sb_, text);
    }
    
    Item getResult() override {
        // Convert StringBuf to Lambda String
        String* result = stringbuf_to_string(sb_);
        Item item;
        item.string_ptr = (uint64_t)result;
        item._8_s = LMD_TYPE_STRING;
        return item;
    }
};
```

**Use Case**: Final HTML output for files, HTTP responses, display

### Node Mode: NodeHtmlWriter

**Implementation**: `lambda/format/html_writer.cpp`

**Storage**: Uses `MarkBuilder` from `lambda/mark_builder.hpp`

**Operation**:
```cpp
class NodeHtmlWriter : public HtmlWriter {
private:
    MarkBuilder builder_;           // Element tree builder
    std::stack<ElementBuilder> stack_; // Track open elements
    Input* input_;                  // Input context
    
public:
    void writeStartElement(const char* tag) override {
        // Create new Element with tag name
        ElementBuilder elem = builder_.element(tag);
        stack_.push(elem);
    }
    
    void writeEndElement() override {
        // Finalize current element
        if (!stack_.empty()) {
            ElementBuilder current = stack_.top();
            stack_.pop();
            
            Item elem_item = current.final();
            
            if (!stack_.empty()) {
                // Add to parent
                stack_.top().child(elem_item);
            } else {
                // Root element
                root_ = elem_item;
            }
        }
    }
    
    void writeText(const char* text) override {
        // Add String child to current element
        if (!stack_.empty()) {
            String* str = builder_.createString(text);
            Item str_item;
            str_item.string_ptr = (uint64_t)str;
            str_item._8_s = LMD_TYPE_STRING;
            stack_.top().child(str_item);
        }
    }
    
    Item getResult() override {
        // Return root Element
        return root_;
    }
};
```

**Use Case**: Pipeline processing, validation, further transformation

### Mode Selection

**Entry Point**: `format_latex_html_v2(Input* input, bool text_mode)`

```cpp
Item format_latex_html_v2(Input* input, bool text_mode) {
    Pool* pool = input->pool;
    
    // Create appropriate writer based on mode
    HtmlWriter* writer = nullptr;
    if (text_mode) {
        // Text mode - generate HTML string
        writer = new TextHtmlWriter(pool, true);  // pretty print
    } else {
        // Node mode - generate Element tree
        writer = new NodeHtmlWriter(input);
    }
    
    // Create HTML generator (same logic for both modes)
    HtmlGenerator gen(pool, writer);
    
    // Create processor and format
    LatexProcessor proc(&gen, pool, input);
    proc.process(input->root);
    
    // Get result (String or Element depending on mode)
    Item result = writer->getResult();
    
    // Cleanup
    delete writer;
    
    return result;
}
```

### Benefits of Dual-Mode Design

1. **Single Code Path**: Formatter logic identical for both modes
   - No duplication of command handlers
   - Same bugs/fixes apply to both modes
   - Easier maintenance

2. **Flexible Output**: Choose mode based on use case
   - Text mode: Direct output, HTTP responses
   - Node mode: Further processing, validation, transformation

3. **Testable**: Can verify both representations
   - Text mode: Compare HTML strings
   - Node mode: Validate Element tree structure

4. **Memory Efficient**: 
   - Text mode: Single StringBuf growth, no intermediate nodes
   - Node mode: Arena allocation, automatic cleanup

5. **Type Safety**: Compiler enforces interface
   - Can't call wrong methods
   - Virtual dispatch handles mode switching

### Example Usage

**Text Mode**:
```bash
# Generate HTML string output
./lambda.exe convert document.tex -f latex -t html -o output.html
```

**Node Mode** (in Lambda script):
```lambda
let html_tree = convert(latex_doc, "html", text_mode: false)
validate(html_tree, html_schema)
transform(html_tree, custom_rules)
```

**C API**:
```cpp
// Text mode
Input* input = parse_latex("\\textbf{Hello}");
Item html_string = format_latex_html_v2(input, true);
printf("%s\n", html_string.string_ptr->chars);  // <span class="bf">Hello</span>

// Node mode
Item html_tree = format_latex_html_v2(input, false);
Element* root = html_tree.element;
// Process tree further...
```

---

### 4.2 Formatter Debugging

**Enable Debug Logging**:
```cpp
// In format_latex_html_v2.cpp
fprintf(stderr, "Processing command: %s\n", cmd_name);
fprintf(stderr, "Element has %lld children\n", reader.childCount());
```

**Debug Output to stderr**:
- Parser creates which Element tags
- Command handler execution flow
- Child iteration and types
- Parameter extraction

**Test Single Command**:
```cpp
TEST_F(LatexHtmlV2Test, TextbfCommand) {
    const char* latex = "\\textbf{Bold text}";
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    EXPECT_STREQ(html, "<p><span class=\"bf\">Bold text</span></p>");
}
```

### 4.3 Test Suite Architecture

**Test Suite Split** (December 18, 2025):

The test suite has been split into two categories with different stability requirements:

**Baseline Tests** (must pass 100% - STABILITY PRIORITY):
- **File**: `test/latex/test_latex_html_v2_baseline.cpp`
- **Fixtures**: `test/latex/fixtures_v2/*.tex`
- **Coverage**: Core features verified to work correctly
- **Status**: **63/63 passing (100%)** ✅
- **Policy**: No extended test fix should break baseline tests
- **Purpose**: Regression prevention, production readiness verification

**Extended Tests** (work-in-progress - ADVANCEMENT PRIORITY):
- **File**: `test/latex/test_latex_html_v2_extended.cpp`
- **Fixtures**: Same source (`test/latex/fixtures_v2/*.tex`)
- **Coverage**: Advanced features, edge cases, complex scenarios
- **Status**: **0/44 passing (44 failing)** 🚧
- **Policy**: Incremental improvement, can have failures
- **Purpose**: Feature development, identifying missing functionality

**Why Split?**
1. **Clear stability baseline**: Know what definitely works
2. **Risk management**: Changes can't silently break working features
3. **Development velocity**: Can work on hard problems without fear
4. **Quality tracking**: Separate metrics for "production ready" vs "in development"
5. **Prioritization**: Baseline breaks are P0, extended failures are P1-P2

**V2-Specific Tests**:
- **Files**: `test/test_latex_html_v2_*.cpp`
- **Coverage**: V2 formatter-specific functionality
- **Categories**: Lists/Envs, Tables, Floats, Special Chars, Bibliography, Graphics/Color, Macros

**Fixture File Coverage** (December 23, 2025):

The baseline test suite includes 16 of 18 available fixture files from `test/latex/fixtures/`:

**Included in Baseline** (16 files):
- `basic_test.tex` - Basic text formatting
- `boxes.tex` - Box commands (minipage, raisebox, etc.)
- `counters.tex` - Counter operations
- `environments.tex` - Standard environments
- `fonts.tex` - Font commands and styles
- `formatting.tex` - Text formatting commands
- `groups.tex` - Group handling and scope
- `label-ref.tex` - Labels and cross-references
- `layout-marginpar.tex` - Margin notes
- `macros.tex` - Macro definitions and usage
- `preamble.tex` - Preamble commands
- `sectioning.tex` - Section hierarchy
- `spacing.tex` - Spacing commands
- `symbols.tex` - Special symbols
- `text.tex` - Text processing
- `whitespace.tex` - Whitespace handling

**Excluded from Baseline** (2 files):
- **`math.tex`**: Contains only screenshot tests (`s**`) requiring visual/KaTeX rendering verification
  - 1 test: "simple inline math" with KaTeX HTML output
  - Requires visual regression testing infrastructure
  - Mathematical typesetting quality verification needed
- **`picture.tex`**: Contains only screenshot tests for picture environment rendering
  - 11 tests: circles, lines, ovals, bezier curves, grid, vectors, etc.
  - All marked as screenshot tests (`s**` or `!s**`)
  - Requires pixel-perfect visual comparison
  - Specialized graphics rendering verification

**Why Exclude Screenshot Tests?**
- Baseline focuses on functional HTML structure correctness
- Screenshot tests require visual rendering pipeline (browser/headless)
- Mathematical and graphical output needs pixel-level comparison tools
- These tests belong in separate visual regression test suite

### 4.4 Fixture Test Format

**File**: `test/latex/fixtures/text.tex`

```
** simple test
.
Hello world
.
<div class="body"><p>Hello world</p></div>
.

** bold and italic
.
\textbf{Bold} and \textit{italic}
.
<div class="body"><p><span class="bf">Bold</span> and <span class="it">italic</span></p></div>
.
```

**Format**:
- `** header` - Test name
- `.` - Separator
- LaTeX source
- `.` - Separator  
- Expected HTML output
- `.` - End marker

**Benefits**:
- Easy to read and write
- Self-documenting test cases
- Shared with LaTeX.js (can compare outputs)

### 4.5 Test Workflow

**1. Run Baseline Tests** (must always pass):
```bash
./test/test_latex_html_baseline.exe
# Expected: [  PASSED  ] 50+ tests
```

**2. Run Extended Tests** (track progress):
```bash
./test/test_latex_html_extended.exe
# Shows: X passing, Y failing
```

**3. Run V2-Specific Tests**:
```bash
# Phase 1-2: Core features, tables, floats
./test/test_latex_html_v2_lists_envs.exe
./test/test_latex_html_v2_tables.exe
./test/test_latex_html_v2_floats.exe

# Phase 3: Special characters
./test/test_latex_html_v2_special_chars.exe

# Phase 4: Bibliography
./test/test_latex_html_v2_bibliography.exe

# Phase 5: Graphics and colors
./test/test_latex_html_v2_graphics_color.exe

# Phase 6: Macros (CURRENT)
./test/test_latex_html_v2_macros.exe
```

**4. Debug Specific Test**:
```bash
# Run single test with GTest filter
./test/test_latex_html_v2_macros.exe --gtest_filter="*NewCommandSimple*"

# With debug output
./test/test_latex_html_v2_macros.exe --gtest_filter="*NewCommandSimple*" 2>&1 | grep "DEBUG"
```

**5. Compare with LaTeX.js**:
```bash
# Generate HTML with Lambda
./lambda.exe convert test.tex -f latex -t html -o lambda_output.html

# Generate HTML with LaTeX.js
latex.js test.tex > latexjs_output.html

# Compare outputs
diff lambda_output.html latexjs_output.html
```

---

## 5. Implementation Status

### ✅ Completed Phases (Phases 1-8: 202/205 tests passing)

**Phase 1: Core Features** (15/15 tests passing)
- Text formatting: `\textbf`, `\textit`, `\texttt`, `\emph`, `\underline`
- Font sizes: `\tiny`, `\small`, `\large`, `\Large`, `\huge`, etc.
- Document structure: `\section`, `\subsection`, `\chapter`, `\part`
- Lists: `itemize`, `enumerate`, `description` with nested support
- Text environments: `quote`, `quotation`, `verse`, `center`, `flushleft`, `flushright`
- Mathematics: Inline `$...$`, display `\[...\]`, `equation` environment
- Cross-references: `\label`, `\ref`, `\pageref`
- Hyperlinks: `\url`, `\href`
- Line breaking: `\\`, `\newline`, `\newpage`
- Footnotes: `\footnote`
- Verbatim: `\verb` (via external scanner - see section 2.3), `verbatim` environment

**Phase 2: Tables & Floats** (25/25 tests)
- Tables: `tabular` environment with `\hline`, `\multicolumn`
- Floats: `figure` and `table` with `\caption`, `\centering`
- Special characters: Escape sequences (`\%`, `\&`, `\$`, `\#`, `\_`, `\{`, `\}`)
- Diacritics: `\'`, `` \` ``, `\^`, `\~`, `\"`

**Phase 3: Bibliography** (13/13 tests)
- Citations: `\cite`, `\cite[text]`, `\citeauthor`, `\citeyear`
- Bibliography: `\bibliography`, `thebibliography` environment
- Entries: `\bibitem`, `\bibitem[label]`
- Styles: `\bibliographystyle`

**Phase 4: Graphics & Color** (17/17 tests)
- Colors: `\textcolor`, `\colorbox`, `\fcolorbox`, `\color`, `\definecolor`
- Color models: named, RGB, HTML, grayscale
- Graphics: `\includegraphics` with options (width, height, scale, angle)

### 🚧 Current Phase 6: Macros (1/14 tests passing - December 12, 2025)

**Macro System** (in active development):
- ✅ `\newcommand`: Simple macros without parameters
- ✅ `\def`: Basic definitions
- ✅ `\renewcommand`: Macro redefinition detection
- ✅ `\providecommand`: Conditional definition
- ✅ Parameter substitution: `#1` working, `#2`, `#3`, etc. code written
- 🔄 Nested macros: Code exists but causes stack overflow
- ❌ Optional arguments: `\newcommand{\cmd}[n][default]{def}`
- ❌ Recursive macros: Need depth limit protection

**Recent Progress** (December 12, 2025):
- ✅ Fixed placeholder parsing: `#1` now preserved as Symbol("#1")
- ✅ Enhanced substituteParamsRecursive: handles SYMBOL and LIST types
- ✅ Fixed substitution update logic: always updates items array
- ✅ Added LIST processing to processNode()
- ✅ Made "arg" element transparent in HTML output
- ✅ **First test passing**: NewCommandWithArguments produces correct output!

**Current Issues**:
- ⚠️ Stack overflow on full test suite (infinite recursion)
- ⚠️ No recursion depth limit (causes crashes on nested macros)
- ⚠️ Debug statements throughout code (need cleanup)
- ⚠️ Multiple parameters (#2, #3, etc.) untested
- ⚠️ Optional arguments not implemented

**Next Steps**:
1. Add MAX_MACRO_DEPTH limit (prevent infinite loops)
2. Remove all debug fprintf statements
3. Fix stack overflow issues
4. Test multiple parameter support
5. Implement optional arguments

**Phase 5: Extended Commands (Part 1)** (42/42 tests passing)
- Font commands: `\textmd`, `\textup`, `\textsl`, `\textnormal`, `\bfseries`, `\itshape`, etc. (14 commands)
- Spacing: `\hspace`, `\vspace`, `\smallbreak`, `\medbreak`, `\bigbreak`, `\vfill`, `\hfill`, `\negthinspace`, etc. (15 commands)
- Boxes: `\mbox`, `\fbox`, `\framebox`, `\phantom`, `\hphantom`, `\vphantom`, `\llap`, `\rlap`, etc. (13 commands)
- Alignment: `\centering`, `\raggedright`, `\raggedleft` (3 commands)
- Metadata: `\author`, `\title`, `\date`, `\thanks`, `\maketitle` (5 commands)
- Special: `\TeX`, `\LaTeX`, `\today`, `\empty`, `\makeatletter`, `\makeatother` (6 commands)

**Phase 5.5: Layout Commands** (partial - Dec 18, 2025)
- **Whitespace hack commands** (for preventing space collapse in HTML):
  - ✅ `\pagebreak`: Made completely silent (was outputting `<div>`)
  - ✅ `\nopagebreak`: Made completely silent (was outputting `<span>`)
  - ✅ `\enlargethispage`: Made silent no-op
  - ⚠️ `\vspace`: Conflict - baseline requires output, extended requires silence
- **Indexing/margin commands** (newly implemented as no-ops):
  - ✅ `\marginpar{text}`: Silent (margin notes)
  - ✅ `\index{entry}`: Silent (index entries)
  - ✅ `\glossary{entry}`: Silent (glossary entries)

**Phase 7: Document Structure** (52/52 tests passing)
- Document class: `\documentclass` (no-op for HTML)
- Packages: `\usepackage` (no-op for HTML)
- File inclusion: `\include`, `\input` (placeholders)
- Abstract: `\abstract` environment
- Table of contents: `\tableofcontents`, `\tableofcontents*`
- Matter markers: `\appendix`, `\mainmatter`, `\frontmatter`, `\backmatter`

**Phase 8: Counter & Length System** (60/60 tests passing - NEW!)
- Counter management: `\newcounter`, `\setcounter`, `\addtocounter`, `\stepcounter`, `\refstepcounter`
- Counter access: `\value` (returns placeholder "0")
- Length management: `\newlength`, `\setlength`
- Note: State tracking not implemented (commands are placeholders/no-ops)

### 📊 Test Suite Status (December 18, 2025 - Evening Update)

**Baseline Suite** (test/test_latex_html_v2_baseline.cpp):
- **65/65 tests passing (100%)** ✅ STABLE
- These are core features that MUST work correctly
- Policy: No changes should break baseline tests
- Verified: text formatting, lists, sections, basic commands, spacing, parbox positioning
- **Recently promoted**: boxes_tex_2, boxes_tex_3 (parbox improvements)

**Extended Suite** (test/test_latex_html_v2_extended.cpp):
- **0/42 tests passing (42 failing)** 🚧 WORK IN PROGRESS  
- These are advanced features and edge cases under development
- Includes: complex whitespace, special characters, advanced boxes, macros, labels/refs

**HTML Formatting & Test Comparison**:
- LaTeX.js uses **js-beautify** library for optional HTML pretty-printing (`--pretty` CLI flag)
- Our implementation has `TextHtmlWriter` with `pretty_print` flag for newline/indent control
- Tests use `HtmlComparator` with `ignore_whitespace=true` - normalizes all whitespace:
  - Collapses multiple spaces to single space
  - Removes whitespace between tags (`>\s+<` → `><`)
  - Removes whitespace after opening tags and before closing tags
- **Conclusion**: HTML formatting (newlines, indentation) doesn't affect test results
- Pretty-printing disabled by default for performance; tests compare semantic HTML structure

**Recent Fixes** (December 18, 2025 - Evening):
- **Counter System**: ✅ Fully functional
  - Enabled counter display commands: `\arabic`, `\roman`, `\Roman`, `\alph`, `\Alph`, `\fnsymbol`
  - Dynamic `\the<counter>` support (e.g., `\thec` automatically formats counter "c")
  - Parent-child counter relationships: `\newcounter{a}[c]` properly parses parent parameter
  - `\value{counter}` returns actual counter value (not placeholder "0")
  - Counter tests fail due to lack of expression evaluation in `\setcounter` and `\addtocounter`
- **`\parbox` command**: ✅ Fully implemented
  - Proper argument parsing: `\parbox[pos][height][inner-pos]{width}{text}`
  - CSS class generation: `p-c/p-t/p-b`, `p-cc/p-ct/p-cb`, `pbh`, `stretch`
  - LaTeX length to pixel conversion: `2cm` → `75.591px` (using `convert_length_to_px`)
  - Pixel values rounded to 3 decimal places for test compatibility
  - Works correctly nested in `\fbox` and other containers
- **`\fbox` + box integration**: ✅ Smart frame handling
  - Detects when fbox contains single parbox/minipage/makebox
  - Adds "frame" class directly to inner box instead of wrapping
  - Result: `\fbox{\parbox{2cm}{text}}` → `<span class="parbox p-c p-cc frame" ...>`
  - Matches LaTeX.js behavior for cleaner HTML structure
- **Parser Investigation**: ✅ No issues found
  - Tree-sitter parser working correctly
  - Commands like `\thec`, `\roman`, `\newcounter` all properly recognized
  - Parser outputs expected Element tree structure

**Previous Fixes**:
- **`\char` command**: ✅ Fixed via formatter lookahead (symbols_tex_1 now passes)
- **`\verb` command**: ✅ Already working via external scanner (verified functional)
- **Linebreak in mbox (`\\[dim]`)**: Fixed whitespace handling in restricted horizontal mode

**Extended Suite - Major Failing Categories** (42 tests):
1. **Counters** (4 tests): ✅ Expression evaluation implemented (Dec 18, 2025)
   - Implemented recursive descent parser for arithmetic expressions: `+`, `-`, `*`, `/`, `()`, unary `--`
   - Supports `\real{float}` command for float values in expressions
   - Supports `\value{counter}` command for counter references
   - **Critical LaTeX.js Behavior**: Truncates after EACH `*` or `/` operation, not just final result
     - Example: `3*\real{1.6} * \real{1.7} + --2`
     - Evaluation: `3*1.6=4.8→4`, `4*1.7=6.8→6`, `6+2=8` ✓
     - This differs from standard math: `3*1.6*1.7+2 = 10.16→10` ✗
   - Files: `lambda/format/format_latex_expr_eval.cpp`, integrated in `format_latex_html_v2.cpp`
   - Status: Counter arithmetic working, 1 test has unrelated paragraph formatting issue
2. **Boxes** (6 tests): ⚠️ 2 parbox tests promoted to baseline, remaining need fixes/features
   - boxes_tex_2, 3 promoted to baseline (basic parbox positioning) ✅
   - boxes_tex_4: Has `\noindent` after list item causing paragraph nesting issues
   - Remaining tests (boxes_tex_1, 5, 6, 7, 8) likely need `\minipage` environment
   - May also need `\makebox`, `\framebox` with dimension parameters
3. **Whitespace handling** (12 tests): Complex spacing rules, space hacks
   - whitespace_tex_8: 5/6 commands fixed (vspace conflict with baseline)
4. **Text formatting** (5 tests): Special characters, control sequences
5. **Label/references** (10 tests): Cross-reference system not implemented
6. **Macros** (8 tests): User-defined commands partially working
7. **Environments** (6 tests): Specialized environments
8. **Fonts** (6 tests): Font command edge cases
9. **Layout** (6 tests): Margin notes (marginpar now implemented)
10. **Others** (12 tests): Groups, sectioning, symbols

**Root Cause Analysis**:
- Most failures stem from **missing expression evaluation** in counter/length commands
- LaTeX.js uses sophisticated expression parser for arithmetic, function calls (`\real{1.6}`), operators
- This would require implementing: tokenizer, parser, evaluator for LaTeX expression syntax
- Estimated effort: 500-1000 lines of code for full expression support

**Next Steps** (Priority Order):
1. **Verify parbox improvements**: Check boxes_tex_2 and related tests manually
2. **Expression evaluator**: Decide if worth implementing or skip counter expression tests
3. **Other Extended Tests**: Focus on tests that don't need expression evaluation
   - Try text_tex_6/7, fonts_tex_6/7/8, or basic_text_tex_4/6
   - Look for command implementation gaps
4. **Special characters**: Control symbol handling (`\%`, `\&`, `\$`, `\#`)
5. **Label/ref system**: Cross-references implementation (moderate complexity)

---

## 6. LaTeX Macro Support - Deep Dive

### 6.1 Overview

LaTeX macros (`\newcommand`, `\def`, etc.) enable users to define custom commands within documents. This is a powerful feature that makes LaTeX extensible. Our implementation provides **runtime macro definition and expansion** with parameter substitution - more faithful to actual LaTeX than LaTeX.js, which uses compile-time JavaScript functions.

### 6.2 Macro Architecture

**Core Components**:

```cpp
// Macro definition storage
struct MacroDefinition {
    std::string name;              // Macro name (e.g., "greet")
    int num_params;                // Number of parameters (0-9)
    bool has_optional;             // Has optional first argument
    Item default_value;            // Default for optional arg
    Item body;                     // Macro body (Element tree)
};

// Macro registry (in LatexProcessor)
std::map<std::string, MacroDefinition> macro_table_;
```

**Data Flow**:

```
\newcommand{\greet}[1]{Hello, #1!}
         ↓
    Parse definition
         ↓
    Store in macro_table_
         
\greet{Alice}
         ↓
    Look up macro_table_["greet"]
         ↓
    Collect arguments
         ↓
    substituteParamsRecursive(body, args)
         ↓
    Process substituted body
         ↓
    Output: Hello, Alice!
```

### 6.3 Macro Definition: `\newcommand`

**LaTeX Syntax**:
```latex
\newcommand{\cmdname}[num_params]{body}
\newcommand{\cmdname}[num_params][default]{body}  % optional first arg
```

**Implementation** (`cmd_newcommand` in format_latex_html_v2.cpp):

```cpp
static void cmd_newcommand(LatexProcessor* proc, Item elem) {
    MarkReader reader(elem);
    
    // 1. Extract macro name from first curly_group
    Item name_group = reader.findFirstChildWithTag("curly_group");
    String* name_str = extract_command_name(name_group);
    
    // 2. Extract parameter count from brack_group_argc
    int num_params = 0;
    Item argc_group = reader.findFirstChildWithTag("brack_group_argc");
    if (argc_group.item) {
        num_params = parse_int_from_group(argc_group);
    }
    
    // 3. Check for optional argument (second brack_group)
    Item opt_group = reader.findSecondBrackGroup();
    Item default_value = opt_group.item ? opt_group : ItemNull;
    
    // 4. Extract body from second curly_group
    Item body = reader.findSecondChildWithTag("curly_group");
    
    // 5. Store in macro table
    MacroDefinition macro;
    macro.name = name_str->chars;
    macro.num_params = num_params;
    macro.has_optional = default_value.item != 0;
    macro.default_value = default_value;
    macro.body = body;
    
    proc->registerMacro(macro);
}
```

**Challenges Solved**:

1. **Tree-sitter parser loses bracket content**:
   - Issue: `\newcommand{\greet}[1]{...}` - parser creates empty `brack_group_argc`
   - Workaround: Default to `num_params=1` when bracket group present but empty

2. **Multiple bracket groups**:
   - `[N]` = parameter count
   - `[default]` = optional argument default
   - Solution: Parse sequentially, track position

### 6.4 Parameter Substitution

**Challenge**: Replace `#1`, `#2`, etc. in macro body with actual arguments

**Tree-sitter Parsing**:
- Original: `#1` parsed as empty `<placeholder>` element (lost data!)
- Fixed: Modified `input-latex-ts.cpp` to create `Symbol("#1")` instead

**Implementation** (`substituteParamsRecursive`):

```cpp
void LatexProcessor::substituteParamsRecursive(
    Item node, 
    const std::vector<Item>& args, 
    std::vector<Item>& new_items,
    int depth  // NEW: recursion depth tracking
) {
    // Safety check: prevent infinite recursion
    if (depth > MAX_MACRO_DEPTH) {
        log_error("Macro expansion exceeded maximum depth %d", MAX_MACRO_DEPTH);
        return;
    }
    
    TypeId type = get_type_id(node);
    
    // Handle different node types
    switch (type) {
        case LMD_TYPE_STRING:
            // Text content - scan for #1, #2, etc.
            substituteParamsInString(node, args, new_items);
            break;
            
        case LMD_TYPE_SYMBOL:
            // Symbol("#1") - direct replacement
            String* sym = (String*)node.string_ptr;
            if (sym->len >= 2 && sym->chars[0] == '#') {
                int param_num = sym->chars[1] - '0';
                if (param_num > 0 && param_num <= args.size()) {
                    new_items.push_back(args[param_num - 1]);
                    return;
                }
            }
            new_items.push_back(node);
            break;
            
        case LMD_TYPE_ELEMENT:
            // Recursively process children
            Element* elem = node.element;
            List* children = elem->children;
            
            List* new_children = create_list(pool_);
            for (int64_t i = 0; i < children->length; i++) {
                std::vector<Item> child_items;
                substituteParamsRecursive(
                    children->items[i], 
                    args, 
                    child_items,
                    depth + 1  // Increment depth
                );
                for (Item& child : child_items) {
                    list_append(new_children, child);
                }
            }
            
            // Create new element with substituted children
            Element* new_elem = clone_element_with_children(elem, new_children);
            Item new_item;
            new_item.element = new_elem;
            new_items.push_back(new_item);
            break;
            
        case LMD_TYPE_LIST:
            // Process each list item
            List* list = node.list;
            for (int64_t i = 0; i < list->length; i++) {
                substituteParamsRecursive(
                    list->items[i], 
                    args, 
                    new_items,
                    depth + 1
                );
            }
            break;
            
        default:
            // Pass through unchanged
            new_items.push_back(node);
    }
}
```

**Key Insights**:

1. **Three substitution modes**:
   - String scanning: Find `#1` in text like `"Hello, #1!"`
   - Symbol replacement: Direct `Symbol("#1")` → argument
   - Recursive descent: Traverse Element tree

2. **Depth tracking**: NEW - prevents infinite recursion
   - Each recursive call increments depth
   - Abort if depth > MAX_MACRO_DEPTH (100)

### 6.5 Macro Expansion

**Process**:

```cpp
void LatexProcessor::expandMacro(
    const MacroDefinition& macro, 
    const std::vector<Item>& args,
    int depth  // NEW: track expansion depth
) {
    // Safety check
    if (depth > MAX_MACRO_DEPTH) {
        log_error("Macro expansion depth exceeded");
        return;
    }
    
    // 1. Clone macro body (avoid modifying original)
    Item body_copy = clone_element_tree(macro.body);
    
    // 2. Substitute parameters
    std::vector<Item> substituted;
    substituteParamsRecursive(body_copy, args, substituted, 0);
    
    // 3. Process substituted body
    for (Item& item : substituted) {
        processNode(item, depth + 1);  // Pass depth down
    }
}
```

**Argument Collection**:

When `\greet{Alice}` is encountered:

```cpp
void LatexProcessor::processCommand(const char* cmd_name, Item elem) {
    // Check if it's a user-defined macro
    auto it = macro_table_.find(cmd_name);
    if (it != macro_table_.end()) {
        const MacroDefinition& macro = it->second;
        
        // Collect arguments from element children
        std::vector<Item> args;
        MarkReader reader(elem);
        
        // Expected: curly_group children like {Alice}, {Bob}, etc.
        // PROBLEM: Tree-sitter doesn't always parse this way!
        // WORKAROUND: Wrap all children in temporary "arg" elements
        
        for (int i = 0; i < macro.num_params; i++) {
            Item arg_elem = reader.createTempArgElement(reader.child(i));
            args.push_back(arg_elem);
        }
        
        // Expand macro with collected args
        expandMacro(macro, args, 0);  // Start at depth 0
        return;
    }
    
    // Not a macro - regular command handling
    // ...
}
```

### 6.6 Comparison with LaTeX.js

**Our Implementation** vs **LaTeX.js**:

| Feature | Lambda V2 | LaTeX.js |
|---------|-----------|----------|
| **\newcommand support** | ✅ Yes | ❌ No |
| **Runtime definition** | ✅ Yes | ❌ No (compile-time only) |
| **Parameter substitution** | ✅ `#1`, `#2`, etc. | ❌ Function parameters |
| **Nested macros** | 🔄 In progress | N/A |
| **Optional arguments** | ❌ Not yet | ✅ Yes (via type system) |
| **LaTeX compatibility** | ✅ High | ⚠️ Low (different paradigm) |

**LaTeX.js Approach**:
```javascript
// Define in JavaScript code (NOT in LaTeX document)
CustomMacros.args['myMacro'] = ['H', 'g'];  // Declare args
CustomMacros.prototype.myMacro = function(arg1) {
    return this.createHtml('span', {}, arg1);
};
```

**Our Approach**:
```latex
% Define in LaTeX document (runtime)
\newcommand{\myMacro}[1]{<span>#1</span>}
\myMacro{text}
```

**Verdict**: Our implementation is **more faithful to actual LaTeX**.

### 6.7 Current Limitations & Roadmap

**Working Features** ✅:
- Simple macros: `\newcommand{\cmd}{text}`
- Single parameter: `\newcommand{\greet}[1]{Hello, #1!}`
- Parameter substitution in strings and symbols
- Macro redefinition detection (`\renewcommand`)
- Conditional definition (`\providecommand`)

**Partially Working** 🔄:
- Multiple parameters: Code written but untested
- Nested macros: Implemented but causes stack overflow
- Recursion protection: Needs MAX_MACRO_DEPTH enforcement

**Not Implemented** ❌:
- Optional arguments: `\newcommand{\cmd}[2][default]{#1 and #2}`
- `\def` with delimited parameters: `\def\cmd#1.#2{...}`
- Macro-specific counters
- `\let` (alias creation)
- `\expandafter` (expansion control)

**Known Bugs** 🐛:
- Stack overflow on some test cases (infinite recursion)
- No recursion depth limit (causes crashes)
- Debug statements need removal
- Argument collection fragile (depends on parser structure)

### 6.8 Recursion Safety (NEW - December 12, 2025)

**Problem**: Nested macros can cause infinite loops:

```latex
% Directly recursive
\newcommand{\loop}{\loop}
\loop  % Stack overflow!

% Indirectly recursive
\newcommand{\a}{\b}
\newcommand{\b}{\a}
\a  % Stack overflow!
```

**Solution**: Depth tracking throughout call chain

```cpp
// Constants
const int MAX_MACRO_DEPTH = 100;  // Reasonable nesting limit

// Modified signatures
void processNode(Item node, int depth = 0);
void processCommand(const char* cmd, Item elem, int depth = 0);
void expandMacro(const MacroDefinition& macro, 
                 const std::vector<Item>& args, 
                 int depth);
void substituteParamsRecursive(Item node, 
                                const std::vector<Item>& args,
                                std::vector<Item>& result,
                                int depth);

// Enforcement at every recursive entry point
void LatexProcessor::processNode(Item node, int depth) {
    if (depth > MAX_MACRO_DEPTH) {
        log_error("Processing depth exceeded maximum %d", MAX_MACRO_DEPTH);
        gen_->text("[MAX DEPTH EXCEEDED]");
        return;
    }
    
    // Normal processing...
    processCommand(cmd_name, elem, depth + 1);
}
```

**Protection Points**:
1. `processNode` → `processCommand` (increment)
2. `processCommand` → `expandMacro` (increment)
3. `expandMacro` → `processNode` (increment)
4. `substituteParamsRecursive` → itself (increment)

**Trade-offs**:
- ✅ Prevents crashes on malformed input
- ✅ Allows deep (but not infinite) nesting
- ❌ Adds overhead (depth parameter everywhere)
- ❌ May reject valid deeply-nested documents

**Tuning**: `MAX_MACRO_DEPTH = 100` chosen as balance:
- Real LaTeX documents rarely nest > 10 levels
- 100 allows complex packages/templates
- Significantly less than stack overflow threshold (~1000s)

### 6.9 Testing Strategy

**Test Structure** (`test/test_latex_html_v2_macros.cpp`):

```cpp
TEST_F(LatexHtmlV2MacrosTest, NewCommandSimple) {
    const char* latex = R"(\newcommand{\hello}{Hello, World!}\hello)";
    const char* expected = "<p>Hello, World!</p>";
    EXPECT_TRUE(output_contains(expected));
}

TEST_F(LatexHtmlV2MacrosTest, NewCommandWithArguments) {
    const char* latex = R"(\newcommand{\greet}[1]{Hello, #1!}\greet{Alice})";
    const char* expected = "<p>Hello, Alice!</p>";
    EXPECT_TRUE(output_contains(expected));
}

TEST_F(LatexHtmlV2MacrosTest, NestedMacros) {
    const char* latex = R"(
        \newcommand{\bold}[1]{\textbf{#1}}
        \newcommand{\greet}[1]{Hello, \bold{#1}!}
        \greet{World}
    )";
    const char* expected = "Hello, <span class=\"bf\">World</span>!";
    EXPECT_TRUE(output_contains(expected));
}
```

**Test Coverage**:
- Simple macros (no parameters)
- Single parameter substitution
- Multiple parameters
- Nested macro calls
- Recursive macros (should fail gracefully)
- Optional arguments
- Redefinition scenarios

**Current Status**: 1/14 tests passing (NewCommandWithArguments ✅)

### 6.10 Future Enhancements

**Phase 1: Stabilization** (Current)
- ✅ Add recursion depth limits
- ✅ Remove debug output
- ✅ Fix stack overflow bugs
- ✅ Test multiple parameters

**Phase 2: Optional Arguments**
- Parse `[default]` in `\newcommand` definition
- Distinguish parameter count vs default value brackets
- Implement argument defaulting logic
- Test cases with mixed required/optional args

**Phase 3: Advanced Features**
- `\def` with delimited parameters: `\def\cmd#1.#2{...}`
- `\let\newcmd=\oldcmd` (aliasing)
- `\expandafter` (expansion order control)
- `\csname...\endcsname` (dynamic command names)

**Phase 4: Package Compatibility**
- Common package macros (amsmath, geometry, etc.)
- Predefined macro library
- Macro namespace management

**Inspiration from LaTeX.js**:
- Declarative argument type system: `'g'` (group), `'o?'` (optional), `'s'` (star)
- Rich argument validation (15+ types)
- Better error messages for malformed arguments

---

## 7. Key Design Decisions

### 7.1 Why Command Dispatch Pattern?

**Advantages**:
- **O(1) lookup**: Hash map for command resolution
- **Modularity**: Each command is independent function
- **Extensibility**: Easy to add new commands
- **Testability**: Test individual handlers
- **Maintainability**: Clear code organization

**Alternative Rejected**:
- Giant if-else chain: O(n) lookup, hard to maintain
- Inheritance hierarchy: Over-engineered for this use case

### 7.2 Why Reuse HtmlGenerator?

**Advantages**:
- **Proven code**: Already tested in old formatter
- **No duplication**: Single HTML generation logic
- **Consistent output**: Same HTML structure across formatters

**Alternative Rejected**:
- New HtmlWriter abstraction: Unnecessary complexity for current needs
- Direct string building: Error-prone, no tag matching

### 7.3 Why Pool-Based Memory?

**Advantages**:
- **Performance**: Fast allocation, no fragmentation
- **Safety**: Automatic cleanup via pool_destroy()
- **Lambda standard**: Consistent with rest of codebase

**Alternative Rejected**:
- Manual malloc/free: Leak-prone, error-prone
- C++ smart pointers: Mixing paradigms, Lambda uses C-style pools

### 7.4 Why File-by-File Translation?

**Advantages**:
- **Verification**: Easy to compare with LaTeX.js source
- **Debugging**: Know which JS function maps to which C++ function
- **Incremental**: Implement features one at a time
- **Documentation**: Self-documenting through naming

**Alternative Rejected**:
- Complete rewrite: Risk of missing edge cases
- Hybrid approach: Confusing, hard to verify

---

## 8. Development Workflow

### 8.1 Adding a New LaTeX Command

**Step 1: Find LaTeX.js implementation**
```bash
# Search in LaTeX.js repository
grep -r "textbf" latex.js/src/
```

**Step 2: Create handler function**
```cpp
static void cmd_textbf(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    gen->span("class=\"bf\"");
    proc->processChildren(elem);
    gen->closeElement();
}
```

**Step 3: Register in command table**
```cpp
void LatexProcessor::initCommandTable() {
    // ... existing commands
    command_table_["textbf"] = cmd_textbf;
}
```

**Step 4: Write test**
```cpp
TEST_F(LatexHtmlV2Test, TextbfCommand) {
    const char* latex = "\\textbf{Bold}";
    parse_latex_string(input, latex);
    const char* html = format_to_html_text(input);
    EXPECT_TRUE(strstr(html, "<span class=\"bf\">Bold</span>"));
}
```

**Step 5: Run tests**
```bash
make build-test
./test/test_latex_html_v2_lists_envs.exe
```

### 8.2 Debugging a Failing Test

**Step 1: Identify the failure**
```bash
./test/test_latex_html_baseline.exe
# Shows: [  FAILED  ] text_tex_5
```

**Step 2: View the fixture**
```bash
cat test/latex/fixtures/text.tex
# Find test with ID 5
```

**Step 3: Run single test**
```bash
./test/test_latex_html_baseline.exe --gtest_filter="*text_tex_5"
```

**Step 4: Add debug output**
```cpp
fprintf(stderr, "DEBUG: Processing node type=%d\n", type);
fprintf(stderr, "DEBUG: Command name='%s'\n", cmd_name);
```

**Step 5: View Lambda tree**
```bash
./lambda.exe convert fixture_input.tex -f latex -t mark -o debug.mark
cat debug.mark
```

**Step 6: Fix and verify**
```cpp
// Make fix in format_latex_html_v2.cpp
make build-test
./test/test_latex_html_baseline.exe --gtest_filter="*text_tex_5"
```

### 8.3 Parser Issues

**If parser produces wrong Element structure**:

**Step 1: Check grammar**
```bash
# View grammar definition
cat lambda/tree-sitter-latex/grammar.js
```

**Step 2: Add debug to parser**
```cpp
// In input-latex-ts.cpp
fprintf(stderr, "Parser: node_type='%s', has_children=%d\n", 
        node_type, has_children);
```

**Step 3: View Tree-sitter CST**
```bash
# Use tree-sitter CLI (if installed)
tree-sitter parse test.tex
```

**Step 4: Fix parser conversion logic**
```cpp
// Modify convert_node() in input-latex-ts.cpp
```

**Step 5: Regenerate if needed**
```bash
make clean-grammar
make generate-grammar
make build
```

---

## 9. Architecture Diagrams

### Overall Data Flow

```
┌─────────────────┐
│  LaTeX Source   │
│   (.tex file)   │
└────────┬────────┘
         │
         ↓
┌─────────────────────────────┐
│  Tree-sitter Parser         │
│  (grammar.js → parser.c)    │
│  Produces: Concrete Syntax  │
│            Tree (CST)       │
└────────┬────────────────────┘
         │
         ↓
┌─────────────────────────────┐
│  Lambda Parser              │
│  (input-latex-ts.cpp)       │
│  Converts: CST → Elements   │
│  Uses: MarkBuilder          │
└────────┬────────────────────┘
         │
         ↓
┌─────────────────────────────┐
│  Lambda Element Tree        │
│  - Element nodes (commands) │
│  - String nodes (text)      │
│  - Symbol nodes (terminals) │
└────────┬────────────────────┘
         │
         ↓
┌─────────────────────────────┐
│  V2 Formatter               │
│  (format_latex_html_v2.cpp) │
│  - LatexProcessor           │
│  - Command dispatch         │
│  - HtmlGenerator            │
└────────┬────────────────────┘
         │
         ↓
┌─────────────────┐
│  HTML Output    │
│  (string or     │
│   Element tree) │
└─────────────────┘
```

### Command Processing Flow

```
processNode(Item node)
    │
    ├─ Is String? → gen->text(string)
    │
    ├─ Is Symbol? → processCommand(symbol_name, node)
    │
    └─ Is Element?
        │
        ├─ Get tag name
        │
        └─ processCommand(tag_name, node)
            │
            ├─ Look up in command_table_
            │
            ├─ Found? → Call handler function
            │   │
            │   └─ Handler:
            │       1. Open HTML tag
            │       2. processChildren(node)
            │       3. Close HTML tag
            │
            └─ Not found? → Output as text
```

### Memory Management

```
┌───────────────────────────────────┐
│  Pool (from input->pool)          │
│  - Fast allocation                │
│  - No fragmentation               │
│  - Automatic cleanup              │
└────────┬──────────────────────────┘
         │
         ├─ HtmlGenerator
         │  └─ StringBuf (for HTML string)
         │
         ├─ LatexProcessor
         │  └─ Command handlers
         │
         └─ Temporary allocations
            └─ pool_calloc(), arena_alloc()
```

---

## 11. Complex Deferred Issues

The following tests require significant architectural changes and are documented here for future work:

### 11.1 text_tex_10: Paragraph Alignment at Break-Time

**LaTeX Source**:
```latex
This is a horrible test.
\centering
In this paragraph we change the alignment to centering.
{\raggedright But it actually becomes raggedright,

even with a group, and only after the par will it be centered.}
Until the group ends.

And we are now still centered.
```

**Expected Behavior**: In LaTeX, `\centering`, `\raggedright`, `\raggedleft` are **declarations** that set alignment for when a paragraph break actually happens. The alignment active at `\par` time applies to the entire paragraph retroactively.

**Current Behavior**: `endParagraph()` is called immediately, and alignment is applied at paragraph open time.

**How LaTeX.js Handles It** (from `latex-parser.pegjs`):
```pegjs
paragraph =
    bb:((escape noindent)? break)*
    _ n:(escape noindent)? txt:text+
    be:break?
    {
        var p = g.create(g.par, txt, n ? "noindent" : "");
        ...
    }
```

LaTeX.js's PEG.js parser collects all text content (`txt:text+`) before calling `g.create()`. Any `\centering` commands within the text have already executed `setAlignment()` by the time the paragraph element is created. The `create()` method reads `@alignment!` at creation time, which naturally contains the last alignment set.

**Fix Approach**:
1. Remove `endParagraph()` calls from alignment commands
2. Alignment commands just set `next_paragraph_alignment_` 
3. Let paragraph breaks (`\par`, blank lines) naturally close paragraphs
4. Alignment is read at paragraph close time, applied retroactively via CSS or by deferring paragraph open

### 11.2 environments_tex_10: Font Environment Span Splitting ✅ FIXED

**Status**: FIXED (Dec 23, Session 3)

**LaTeX Source**:
```latex
normal text \begin{small}
    small text
    \begin{bfseries}
        bold text
    \end{bfseries}
\end{small}
  three spaces!
```

**Expected HTML**:
```html
<p>
normal text <span class="small">​ </span><span class="small">small text </span>
<span class="bf">​ </span><span class="bf">bold text </span><span class="bf">​ </span>
<span class="small">​ </span>
three spaces!
</p>
```

**Problem**: Was producing nested spans: `<span class="small">...<span class="bf">...</span></span>`

**Solution Implemented**:
1. Added font environment class stack in LatexProcessor:
   - `pushFontEnvClass(const char* cls)` - push new font class onto stack
   - `popFontEnvClass()` - pop and return to previous class
   - `currentFontEnvClass()` - get current combined font class
2. When entering nested font environment:
   - Close current span with ZWS (U+200B)
   - Start new span with parent class (e.g., "small") combined with current
3. Output ZWS markers at environment boundaries
4. On exit, restore previous class with new span

### 11.3 environments_tex_14: Comment Environment Parser ✅ FIXED

**Status**: FIXED (Dec 23, Session 3)

**LaTeX Source**:
```latex
text
\begin{comment}
    This is a comment.
    \end{comment
    still more comment.
\end{comment}
more text, but now with%
\begin{comment}
    This is a comment.
\end{comment}
out space.
```

**Expected HTML**:
```html
<div class="body">
<p>text more text, but now without space.</p>
</div>
```

**Problem**: Content between `\begin{comment}` and `\end{comment}` was being output.

**Solution Implemented**:
- Updated `env_comment` handler to simply skip all children (produce no output)
- The existing parser correctly captures the environment content, it was just being output
- Simple fix: handler returns without processing children

### 11.4 whitespace_tex_5: Mbox ZWS at Start ✅ FIXED

**Status**: FIXED (Dec 23, Session 3)

**LaTeX Source**:
```latex
\mbox{
one \gobbleO space,
...
}
```

**Expected**: ZWS (U+200B) at start of mbox output when content starts with newline.

**Problem**: `\mbox{\n...}` wasn't getting ZWS prefix like `\mbox{ ...}` (space) gets space prefix.

**Solution Implemented**:
1. Modified `input-latex-ts.cpp` to preserve newline vs space distinction in `space` nodes:
   - Now outputs `"\n"` for whitespace starting with newline
   - Outputs `" "` for whitespace starting with space
2. Added `restricted_h_mode_first_text_` flag in LatexProcessor
3. In `processText()`: Check if first text in restricted h-mode starts with newline
4. If so, output ZWS before the text content

### 11.5 whitespace_tex_7: Echo Package Macros ⏸️ SKIPPED

**Status**: SKIPPED (matches original LaTeX.js - test has `!` prefix)

**LaTeX Source**:
```latex
one \gobbleO space,
two \gobbleO[] spaces,
two \gobbleO{} spaces,
two \echoOGO{} spaces,
two \echoO[] spaces.
```

**Expected HTML**:
```html
<p>one space, two ​ spaces, two ​ spaces.</p>
```

**Issue**: Expected output has only 3 parts but input has 5 lines. Lines with `\echoOGO{}` and `\echoO[]` are completely missing from expected output.

**Analysis**:
- The expected doesn't match documented latex-js echo package behavior
- `\echoO[]` should output `--` (dash-empty-dash), not nothing
- `\echoOGO{}` should output `++` (plus-empty-plus), not nothing
- Test appears to be aspirational - latex-js itself skips this test

**Decision**: Keep skipped until upstream clarifies intended behavior.

### 11.6 whitespace_tex_8: Invisible Commands ⏸️ SKIPPED

**Status**: SKIPPED (matches original LaTeX.js - test has `!` prefix)

**LaTeX Source**:
```latex
x \pagebreak x \nopagebreak x \vspace{1mm} x \label{hi} x \marginpar{test} x \index{something} x \glossary{word} x \nocite x \enlargethispage x
```

**Expected HTML**:
```html
<p>x x x x x x x x x x</p>
```

**Issue**: Expects `\vspace{1mm}` and `\marginpar{test}` to produce NO visible HTML output.

**Conflict**:
- The passing `spacing.tex` tests expect `\vspace` to produce `<span class="vspace-inline">...</span>`
- This test expects `\vspace` to produce nothing
- Cannot satisfy both requirements without mode-specific behavior

**Analysis**:
- Test is about "space hack to prevent multiple spaces"
- May be testing specific configuration or mode that isn't standard
- Original latex-js also skips this test (`!` prefix)

**Decision**: Keep skipped. Fixing would break passing baseline tests (spacing.tex).

---

## 12. References

### LaTeX.js Repository
- **URL**: https://github.com/michael-brade/LaTeX.js
- **Key Files**:
  - `src/latex.ltx.ls` - Command macros (~1400 lines)
  - `src/html-generator.ls` - HTML generation
  - `src/generator.ls` - Base generator
  - `src/symbols.ls` - Unicode mappings

### Lambda Codebase
- **Parser**: `lambda/input/input-latex-ts.cpp`
- **Formatter**: `lambda/format/format_latex_html_v2.cpp`
- **Grammar**: `lambda/tree-sitter-latex/grammar.js`
- **Tests**: `test/test_latex_html_v2_*.cpp`
- **Fixtures**: `test/latex/fixtures/*.tex`

### Documentation
- **Progress Report**: `vibe/Latex_to_Html_v2.md`
- **This Document**: `vibe/Latex_to_Html_v2_Design.md`
- **Copilot Instructions**: `.github/copilot-instructions.md`

---

## 13. Conclusion

The LaTeX to HTML V2 implementation successfully translates LaTeX.js formatting logic to C++ through:

1. **Tree-sitter Parser**: Robust LaTeX parsing with grammar-based approach
2. **Lambda Element Tree**: Unified data representation for document processing
3. **Command Dispatch**: Modular, extensible architecture for command handlers
4. **File-by-File Translation**: Systematic porting with clear verification path
5. **Comprehensive Testing**: Single baseline test suite with 97/101 tests passing

**Final Status** (December 2025 - Session 3): ✅ **COMPLETE**
- **Baseline**: 97/101 fixture tests passing (97 pass, 4 skipped)
- **Extended**: Test suite deleted - all tests consolidated into baseline
- **Skipped tests** (4 total - all match original latex-js `!` prefix):
  - environments_tex_13: `\unskip` breaks out of groups
  - whitespace_tex_6: `\unskip`/`\ignorespaces` semantics
  - whitespace_tex_7: Echo package macro edge cases (aspirational test)
  - whitespace_tex_8: Invisible commands conflicting with spacing tests

**Session 3 Achievements**:
- Fixed font environment span splitting (environments_tex_10)
- Fixed comment environment handling (environments_tex_14)
- Fixed mbox ZWS at start for newline content (whitespace_tex_5)
- Consolidated all tests into single baseline suite
- Achieved 96.0% pass rate (97/101 tests)

**Previous Session Fixes**:
- Paragraph alignment at break-time (text_tex_10)
- Parbreak symbol handling in groups (macros_tex_6)
- Typographic hyphen conversion (text_tex_4, text_tex_6)
- ZWS for empty curly groups (text_tex_5, basic_text_tex_4)

**Implementation Complete**: The V2 formatter now handles all major LaTeX constructs with high fidelity to LaTeX.js behavior. The 4 skipped tests represent edge cases that are also skipped in the original LaTeX.js implementation.

**Next Goals** (Optional enhancements): 
- Complete Phase 6 (Macro system) to enable user-defined commands via `\newcommand`
- Implement remaining commands (counters/lengths, documentclass, usepackage, include/input)
- Extend to handle additional LaTeX packages and document classes

**Long-term Vision**: Full LaTeX.js feature parity with ~200 commands, document classes, and package support
