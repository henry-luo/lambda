# LaTeX to HTML V2 - Improvement Proposal (Phase 4)

**Date**: December 22, 2025  
**Status**: Baseline 77/77 (100%), Extended 0/31 (0%)  
**Previous Status**: Baseline 76/76 (100%), Extended 0/32 (0%)  
**Recent Progress**: ✅ Completed Phase 1 ZWS implementation - fixed space-absorbing command detection  
**Objective**: Systematic analysis and prioritized roadmap for extended test coverage

---

## Recent Achievements (December 22, 2025)

### 1. Baseline Test Suite: 77/77 (100% Pass Rate) ✅

#### **Latest: Phase 1 ZWS Implementation Complete** (December 22, 2025)

✅ **All baseline tests passing** with proper ZWS handling for space-absorbing commands!

**text_tex_9, whitespace_tex_13, basic_text_tex_4**: ZWS between TeX/LaTeX logos
- **Problem**: Unwanted ZWS appearing between `\TeX` and `\LaTeX` logos
- **Root cause**: TeX/LaTeX logos processed as SYMBOLS in `processNode()` (lines 6248-6301), not through command dispatch table. At sibling level, they appear as Elements with tag="LaTeX"/"TeX", not as "command" elements.
- **Solution**:
  - Symbol handlers set `pending_zws_output_ = true` flag after outputting logo HTML
  - In `processChildren()`, added sibling scanning loop (lines 6818-6948) to check if ZWS should be output
  - Key insight: Check element tag name **directly** with `commandAbsorbsSpace(next_tag)` since processed symbols become Elements
  - Suppress ZWS if next sibling is:
    - Paragraph break symbol
    - Another space-absorbing command (including element tags like "LaTeX")
    - No following substantial content exists
- **Implementation details**:
  - Lines 6248-6267: TeX symbol handler outputs logo HTML, sets pending flag
  - Lines 6268-6287: LaTeX symbol handler (similar pattern)
  - Lines 6818-6824: Check for pending flag after processing each child
  - Lines 6825-6948: Sibling scanning logic with paragraph break detection, space-absorbing command detection
  - Lines 6922-6943: Element check that handles both tag="LaTeX" (from symbols) and tag="command" (from parser)
  - Lines 6944-6950: ZWS output with proper font styling if has_following_content=true
- **Impact**: Achieved **77/77 baseline passing (100%)**
- **Test reorganization**: Moved `basic_text_tex_4` from extended to baseline (was already passing with this fix)

#### **Earlier: Parbreak Symbol Detection** (December 22, 2025)

Fixed final 3 baseline failures by discovering critical parser behavior:

**fonts_tex_3, fonts_tex_4, groups_tex_1**: Extra/missing ZWS around paragraph breaks
- **Problem**: ZWS suppression logic only checked strings for newlines, missing paragraph breaks entirely
- **Root cause**: LaTeX parser converts `\n\n` into a **symbol** (TypeId 9) with value "parbreak", NOT a string
- **Solution**:
  - Added explicit symbol detection before text processing in sibling loop
  - Check for `strcmp(sym, "parbreak") == 0` to immediately suppress ZWS
  - Enhanced text checking to handle both strings AND symbols
  - When parbreak symbol found, set `has_following_content = false` and exit loop
- **Impact**: Achieved 71/71 baseline passing (100%)

**Test Suite Reorganization**:
- Identified 6 passing extended tests: `counters_tex_2`, `text_tex_7`, `whitespace_tex_13`, `whitespace_tex_15`, `label_ref_tex_1`, `basic_text_tex_4`
- Moved tests from extended to baseline by updating test fixture filters
- Final status: **77/77 baseline (100%)**, **31 extended tests remaining** (all failing)
- This properly separates production-ready features from known issues

### 2. Previous Baseline Fixes (Earlier December 22, 2025)

Fixed all 3 previously failing baseline tests plus re-enabled the sectioning test:

#### **spacing_tex_2**: Line break with vertical space (`\\[1cm]`)
- **Problem**: `\\[1cm]` was outputting `<br>[1cm]` instead of `<span class="breakspace" style="margin-bottom:37.795px"></span>`
- **Root cause**: `\\` was parsed as a single-character `control_symbol` token, preventing capture of optional `[<length>]` argument
- **Solution**:
  - Removed `\\` from `control_symbol` character class in grammar.js
  - Added new `linebreak_command` rule: `\\` + optional `*` + optional `[<length>]` with right associativity
  - Updated AST builder to extract length from `brack_group` child and store as attribute
  - Updated formatter to handle new grammar structure where `brack_group` is a child of `linebreak_command`

#### **symbols_tex_4**: Parse error on `\textellipsis`
- **Problem**: Fragment starting with `\textellipsis` caused "Parse error at position 0"
- **Root cause**: `command` wasn't in `_top_level_item` grammar choices
- **Solution**: Added `command`, `curly_group`, and `brack_group` to `_top_level_item` to support LaTeX fragments

#### **label_ref_tex_4**: Label inside section title
- **Problem**: `\section{Section Name\label{sec:test}}` was outputting "sec:test" as text
- **Root cause**: Same as symbols_tex_4 - commands weren't allowed at top level
- **Solution**: Fixed by adding `command` to `_top_level_item` choices

#### **SectioningCommands**: Re-enabled test
- **Problem**: Test was disabled with incorrect expectations
- **Root cause**: Expected `<div class="latex-subsection">` but formatter correctly outputs `<h3>` with proper heading hierarchy
- **Solution**: Updated test expectations to match correct HTML output with EM SPACE (U+2003) between section numbers and titles

### 2. Grammar Improvements

**Enhanced `_top_level_item` to support LaTeX fragments**:
- Previously required `\begin{document}...\end{document}` wrapper
- Now supports bare commands, groups, and text at top level
- Enables processing of document fragments and snippets

**New `linebreak_command` grammar rule**:
- Properly captures `\\` with optional star and length argument
- Uses right associativity to greedily consume optional `[<length>]`
- Resolves ambiguity with `brack_group` as sibling vs. child

---

## Executive Summary

Analysis of 32 failing extended tests reveals **10 major functional gaps** in the current Lambda LaTeX-to-HTML formatter implementation. These tests were derived from the LaTeX.js reference implementation and represent advanced LaTeX features essential for production-quality document conversion.

### Current Test Status: 77 Baseline + 31 Extended

**Baseline**: 77/77 passing (100%) - includes 6 tests promoted from extended  
**Extended**: 0/31 passing (0%) - all remaining tests are known failures  
**Recent Achievement**: ✅ Phase 1 ZWS implementation complete - proper space-absorbing command detection

---

## Recommended Next Major Work Area: Verbatim Commands

### Priority: P0 - Verbatim Commands (Quick Win)

**ZWS Implementation Status**: ✅ **Phase 1 Complete**
- ✅ **Space-absorbing command detection** - properly suppresses ZWS between TeX/LaTeX logos
- ✅ **Parbreak symbol detection** - fixes ZWS suppression before paragraph breaks
- ✅ **4 ZWS tests moved to baseline**: `whitespace_tex_13`, `whitespace_tex_15`, `text_tex_7`, `basic_text_tex_4`
- ⏳ **Remaining ZWS work**: ~5 extended tests still failing (empty groups without following text, complex font boundaries)

**Next Priority**: Verbatim Commands

**Selected Rationale**:
1. **High Impact**: Verbatim is a common LaTeX feature used in documentation
2. **Low Complexity**: Grammar already exists, implementation already designed
3. **Quick Win**: Can achieve 3 tests passing in ~1 day
4. **Foundation Complete**: ZWS Phase 1 done, verbatim doesn't depend on remaining ZWS work

### Implementation Strategy

#### Status: Phase 1 Complete ✅

**Space-Absorbing Command Detection** implemented in [format_latex_html_v2.cpp](lambda/format/format_latex_html_v2.cpp#L6818-6950):

**Key Implementation**:
```cpp
// After TeX/LaTeX symbol sets pending_zws_output_ = true
if (pending_zws_output_) {
    // Scan forward siblings to check if ZWS should be output
    for (int64_t j = i + 1; j < count; j++) {
        ItemReader next_reader = elem_reader.childAt(j);
        
        // Check for paragraph break symbol
        if (next_reader.isSymbol() && strcmp(sym, "parbreak") == 0) {
            has_following_content = false;
            break;
        }
        
        // Check if next element is space-absorbing command
        if (next_reader.isElement()) {
            ElementReader next_elem(next_reader.item());
            const char* next_tag = next_elem.tagName();
            
            // Check element tag directly (e.g., tag="LaTeX" from symbol)
            if (next_tag && commandAbsorbsSpace(next_tag)) {
                has_following_content = false;
                break;
            }
            
            // Also check tag="command" elements
            if (next_tag && strcmp(next_tag, "command") == 0) {
                // Extract command name and check if space-absorbing
                // ...
            }
        }
    }
    
    // Output ZWS only if has_following_content=true
    if (has_following_content) {
        gen_->text("\xe2\x80\x8b");  // U+200B
    }
}
```

**Parbreak Detection** also implemented in [format_latex_html_v2.cpp](lambda/format/format_latex_html_v2.cpp#L6777-6810):
```cpp
// Check for paragraph break symbol BEFORE text processing
if (next_reader.isSymbol()) {
    const char* sym = next_reader.asSymbol()->chars;
    if (strcmp(sym, "parbreak") == 0) {
        has_following_content = false;
        goto done_checking_siblings;
    }
}

// Handle both strings AND symbols for text checking
if (next_reader.isString() || next_reader.isSymbol()) {
    const char* next_text = next_reader.isString() ? 
        next_reader.cstring() : 
        (next_reader.isSymbol() ? next_reader.asSymbol()->chars : nullptr);
```

#### Remaining Phase 1: Additional ZWS Detection Logic
Extend detection in `format_latex_html_v2.cpp::processChildren()`:
```cpp
bool needsZwsAfter(const Element* elem) {
    const char* type = elem->type_name;
    
    // Empty curly groups need ZWS to preserve boundaries
    if (strcmp(type, "curly_group") == 0 && !elem->has_children()) {
        return true;
    }
    
    // Commands that absorb following whitespace
    if (strcmp(type, "command") == 0) {
        const char* cmd_name = elem_reader.get_text(elem, "name");
        return commandAbsorbsSpace(cmd_name);
    }
    
    return false;
}
```

#### Phase 2: ZWS Output Helper
```cpp
void HtmlGenerator::writeZWS() {
    // Matches LaTeX.js reference: <span class="zws"> </span>
    writer_->writeRawHtml("<span class=\"zws\"> </span>");
}
```

#### Phase 3: Space-Absorbing Command Registry
```cpp
static const std::unordered_set<std::string> SPACE_ABSORBING_COMMANDS = {
    "LaTeX", "TeX", "LaTeXe",
    "textbf", "textit", "emph", "texttt",
    "small", "large", "Large", "LARGE",
    "empty",  // From whitespace_tex_15
    // Expand as needed based on test failures
};
```

### Phase 1 Results ✅

**Status**: 77/77 baseline (100%), 0/31 extended (0%)  
**Development Time**: 2 days (completed)  

**Fixed Tests** (moved to baseline):
- ✅ `text_tex_9`: No ZWS between `\TeX` and `\LaTeX` logos
- ✅ `whitespace_tex_13`: Control space `\ ` preservation, empty group ZWS
- ✅ `whitespace_tex_15`: ZWS after `\empty{}`
- ✅ `text_tex_7`: ZWS in command contexts
- ✅ `basic_text_tex_4`: ZWS with diacritics

**Remaining ZWS Tests** (Phase 2, lower priority):
- `whitespace_tex_5`: ZWS after isolated `{}`
- `whitespace_tex_6`: Multiple empty groups
- `whitespace_tex_8`: ZWS after `\textbf{}` with empty arg
- `whitespace_tex_12`: ZWS in deeply nested groups
- `whitespace_tex_21`: ZWS in command sequences without following content
- Related: `groups_tex_1` (group spanning paragraphs)

### Next Recommendation: Verbatim Commands (P0)

**Pros**:
- ✅ Marked P0 (high priority)
- ✅ Grammar already exists (`verbatim` and `verbatim_env` nodes)
- ✅ Implementation already designed in design doc
- ✅ Low complexity, quick win
- ✅ Common LaTeX feature for code/literal text

**Why Now**:
- ZWS Phase 1 complete - foundation established
- Remaining ZWS work is edge cases (lower priority)
- Verbatim is independent, doesn't depend on other features
- Can achieve 3 more tests passing (~10% extended coverage) in 1 day

**Recommendation**: Start verbatim commands immediately as next quick win.

### Long-term Roadmap

**Phase 1** ✅ (Complete): Whitespace ZWS Markers → 77/77 baseline (100%)  
**Phase 2** (Week 1): Verbatim Commands → 80/108 tests  
**Phase 3** (Week 2): Unicode Character Mapping → 83/108 tests  
**Phase 4** (Week 3): Counter Child Reset → 84/108 tests  
**Phase 5** (Week 4): Custom Item Labels → 85/108 tests  
**Phase 6** (Week 5-6): Label/Reference System → 92/108 tests  
**Phase 7** (Week 7-9): Custom Macro System → 98/108 tests (most complex)  
**Phase 8** (Week 10+): Remaining edge cases → 108/108 tests (100%)

---

### Key Findings

1. **Custom Macro System** (6 failing tests): No runtime macro expansion via `\newcommand`, `\renewcommand`, `\def`
2. **Verbatim Commands** (3 failing tests): `\verb` command not implemented despite grammar support
3. **Whitespace Handling** (9 failing tests): Missing zero-width space (ZWS) markers for proper HTML rendering
4. **Font Environment Scoping** (3 failing tests): Font attributes not properly scoped within `\begin{small}...\end{small}`
5. **Counter System** (1 failing test): Child counter reset logic not working
6. **Label/Reference System** (7 failing tests): Forward references not resolved, `\label` after command not captured
7. **Groups & Paragraphs** (3 failing tests): Group boundaries not triggering paragraph breaks correctly
8. **Unicode Character Mapping** (3 failing tests): En-dash (`–`), hyphen (`‐`), ligatures not mapped correctly
9. **Custom Item Labels** (1 failing test): `\item[label]` not preserving custom labels
10. **Special Edge Cases** (2 failing tests): `\noindent` placement, nested structures

### Impact Analysis

| Priority | Feature Category | Test Count | Impact | Complexity |
|----------|-----------------|------------|--------|------------|
| **P0** | Verbatim Commands | 3 | High - Common LaTeX feature | Low - Grammar exists |
| **P0** | Custom Macros | 6 | Critical - Document extensibility | High - Runtime expansion |
| **P1** | Label/Reference System | 7 | High - Cross-referencing essential | Medium - Two-pass needed |
| **P1** | Whitespace ZWS Markers | 9 | Medium - HTML rendering fidelity | Low - Add markers |
| **P2** | Font Environment Scoping | 3 | Medium - Visual correctness | Medium - Scope tracking |
| **P2** | Counter Child Reset | 1 | Medium - Numbering accuracy | Low - Logic fix |
| **P2** | Unicode Character Mapping | 3 | Low - Minor visual issues | Low - Character map |
| **P3** | Custom Item Labels | 1 | Low - Edge case | Low - Attribute handling |
| **P3** | Groups & Paragraphs | 3 | Low - Rare edge case | Medium - State tracking |
| **P3** | Special Edge Cases | 2 | Low - Specific scenarios | Varies |

---

## Detailed Analysis by Category

### 1. Custom Macro System (P0 - Critical)

**Failing Tests**: `macros_tex_2`, `macros_tex_4`, `macros_tex_5`, `macros_tex_6`, `whitespace_tex_7`, `whitespace_tex_15`

#### Problem Description

Lambda currently has **no runtime macro expansion system**. LaTeX documents using `\newcommand`, `\renewcommand`, or `\def` fail to render custom commands, outputting raw macro names instead of expanded content.

**Example Failure** (`macros_tex_2`):
```latex
\documentclass{article}
\usepackage{echo}
\begin{document}
some text \echoOGO{with a macro}
\end{document}
```

**Expected**: `some text +with a macro+`  
**Actual**: `some text` (macro not expanded)

#### LaTeX.js Reference Implementation

Located in `/Users/henryluo/Projects/latex-js/src/packages/echo.ls`:

```livescript
export class Echo
    args = @args = {}
    
    args.echoOGO = <[ H o? g o? ]>
    \echoOGO : (o1, g, o2) ->
        []
            ..push "-", o1, "-" if o1
            ..push "+", g,  "+"
            ..push "-", o2, "-" if o2
```

**Key Patterns**:
1. **Argument Specification**: `<[ H o? g o? ]>` defines argument types:
   - `H` = horizontal mode command
   - `o?` = optional argument in brackets `[]`
   - `g` = mandatory group argument in braces `{}`
2. **Runtime Function**: Macro is a function that processes parsed arguments
3. **Dynamic Registration**: Macros registered in `_macros` map at runtime

#### Implementation Approach

**Phase 1: Argument Parser** (Foundation)
```cpp
// File: lambda/format/format_latex_html_v2.cpp

struct MacroArgSpec {
    enum Type { OPTIONAL, MANDATORY, OPTIONAL_DEFAULT };
    Type type;
    char delimiter_open;   // '[' or '{'
    char delimiter_close;  // ']' or '}'
    std::string default_value;  // For optional with default
};

struct MacroDefinition {
    std::string name;
    std::vector<MacroArgSpec> args;
    Item body;  // Lambda element tree of macro body
    bool is_long;  // \long allows \par in arguments
};
```

**Phase 2: Macro Definition Commands**
```cpp
// \newcommand{\name}[num_args][default]{body}
static void cmd_newcommand(LatexProcessor* proc, Item elem) {
    ElementReader er(elem);
    
    // Parse: \newcommand{\echoOGO}[2]{+#1+}
    Item cmd_name = er.childAt(0);  // \echoOGO
    Item num_args = er.childAt(1);  // 2 (optional)
    Item default_val = er.childAt(2);  // default (optional)
    Item body = er.childAt(3);  // +#1+
    
    MacroDefinition macro;
    macro.name = extract_command_name(cmd_name);
    macro.body = body;
    
    // Build argument spec based on num_args
    int n = num_args.isNull() ? 0 : parse_integer(num_args);
    for (int i = 0; i < n; i++) {
        MacroArgSpec spec;
        spec.type = (i == 0 && !default_val.isNull()) 
                    ? MacroArgSpec::OPTIONAL_DEFAULT 
                    : MacroArgSpec::MANDATORY;
        spec.delimiter_open = '{';
        spec.delimiter_close = '}';
        if (spec.type == MacroArgSpec::OPTIONAL_DEFAULT) {
            spec.default_value = extract_text(default_val);
        }
        macro.args.push_back(spec);
    }
    
    proc->defineMacro(macro.name, macro);
}
```

**Phase 3: Macro Expansion**
```cpp
void LatexProcessor::processCommand(const char* cmd_name, Item elem) {
    // Check for custom macros first
    if (hasUserMacro(cmd_name)) {
        expandMacro(cmd_name, elem);
        return;
    }
    
    // ... existing built-in command handling
}

void LatexProcessor::expandMacro(const char* name, Item elem) {
    MacroDefinition& macro = user_macros_[name];
    ElementReader er(elem);
    
    // Extract arguments from element children
    std::vector<Item> args;
    for (size_t i = 0; i < macro.args.size(); i++) {
        args.push_back(er.childAt(i));
    }
    
    // Clone macro body and substitute #1, #2, etc.
    Item expanded = substituteParameters(macro.body, args);
    
    // Process expanded content
    processNode(expanded);
}

Item LatexProcessor::substituteParameters(Item body, 
                                          const std::vector<Item>& args) {
    // Deep clone body tree
    MarkBuilder builder(input_);
    Item cloned = cloneTree(builder, body);
    
    // Walk tree, replacing "#1" text nodes with args[0], etc.
    substituteInTree(cloned, args);
    
    return cloned;
}
```

**Phase 4: Package Loading (`\usepackage`)**
```cpp
static void cmd_usepackage(LatexProcessor* proc, Item elem) {
    ElementReader er(elem);
    const char* pkg_name = er.childAt(0).cstring();
    
    // Hard-coded package definitions (like echo.ls)
    if (strcmp(pkg_name, "echo") == 0) {
        defineEchoPackage(proc);
    }
    // ... other packages
}

void defineEchoPackage(LatexProcessor* proc) {
    // \gobbleO: optional arg, outputs nothing
    MacroDefinition gobble;
    gobble.name = "gobbleO";
    gobble.args.push_back({MacroArgSpec::OPTIONAL, '[', ']', ""});
    gobble.body = ItemNull;  // Empty expansion
    proc->defineMacro("gobbleO", gobble);
    
    // \echoOGO: o? g o? -> "-o1-+g+-o2-"
    MacroDefinition echo;
    echo.name = "echoOGO";
    echo.args.push_back({MacroArgSpec::OPTIONAL, '[', ']', ""});
    echo.args.push_back({MacroArgSpec::MANDATORY, '{', '}', ""});
    echo.args.push_back({MacroArgSpec::OPTIONAL, '[', ']', ""});
    // Body: pre-built element tree for output pattern
    echo.body = buildEchoTemplate(proc);
    proc->defineMacro("echoOGO", echo);
}
```

#### Testing Strategy

1. **Unit Tests**: Test `substituteParameters()` with various `#1`, `#2` patterns
2. **Integration Tests**: Enable `macros_tex_*` tests one by one
3. **Validation**: Compare outputs with LaTeX.js reference

#### Estimated Effort

- **Phase 1**: 2 days (argument parser infrastructure)
- **Phase 2**: 2 days (`\newcommand`, `\renewcommand`, `\def`)
- **Phase 3**: 3 days (expansion engine with parameter substitution)
- **Phase 4**: 1 day (package loading, echo package)
- **Testing**: 1 day
- **Total**: ~9 days

---

### 2. Verbatim Commands (P0 - High Impact, Low Complexity)

**Failing Tests**: `basic_text_tex_6`, `text_tex_8`, (1 related in whitespace)

#### Problem Description

`\verb` command is **parsed by tree-sitter grammar** (external scanner implemented) but **not formatted by V2**. The formatter outputs raw delimiters instead of `<code>` tags.

**Example Failure** (`basic_text_tex_6`):
```latex
This is some \verb|verbatim| text.
```

**Expected**: `This is some <code class="latex-verbatim">verbatim</code> text.`  
**Actual**: `This is some |verbatim| text.`

#### Design Document Reference

The design document ([Latex_to_Html_v2_Design.md](Latex_to_Html_v2_Design.md) lines 139-314) already contains complete implementation:

```cpp
static void cmd_verb_command(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    ElementReader elem_reader(elem);
    
    // Get token text from first child
    ItemReader first_child = elem_reader.childAt(0);
    const char* text = first_child.cstring();  // "\verb|text|"
    
    // Parse delimiter and content
    const char* delimiter_start = text + 5;  // Skip "\verb"
    if (*delimiter_start == '*') delimiter_start++;  // \verb*
    
    char delim = *delimiter_start;
    const char* content_start = delimiter_start + 1;
    const char* content_end = strchr(content_start, delim);
    
    if (!content_end) {
        gen->text(text);  // Malformed
        return;
    }
    
    size_t content_len = content_end - content_start;
    
    // Output: <code class="latex-verbatim">content</code>
    gen->writer()->openTagRaw("code", "class=\"latex-verbatim\"");
    std::string content(content_start, content_len);
    gen->writer()->writeText(content.c_str());
    gen->writer()->closeTag("code");
}
```

#### Implementation Steps

1. Copy `cmd_verb_command()` from design doc to `format_latex_html_v2.cpp`
2. Register in command table: `command_table_["verb_command"] = cmd_verb_command;`
3. Handle `\verb*` variant (visible spaces as `␣`)
4. Test all three failing cases

#### Estimated Effort

- **Implementation**: 0.5 days (code already designed)
- **Testing**: 0.5 days
- **Total**: 1 day

---

### 3. Label/Reference System (P1 - High Impact)

**Failing Tests**: `label_ref_tex_1`, `label_ref_tex_2`, `label_ref_tex_3`, `label_ref_tex_6`, `label_ref_tex_7`, (2 related in counters)

#### Problem Description

Current implementation **does not resolve forward references** and **fails to capture labels** after sectioning commands. This breaks cross-referencing, a fundamental LaTeX feature.

**Example Failure** (`label_ref_tex_2`):
```latex
Forward ref: \ref{test}   % Shows "??" instead of "1"
\refstepcounter{c}
\label{test}              % Label not captured
Back ref: \ref{test}      % Shows "1" correctly
```

**Expected**: Both refs show `1`  
**Actual**: First ref shows `??`, second shows `1`

#### LaTeX.js Reference Implementation

Located in `/Users/henryluo/Projects/latex-js/src/generator.ls` (lines 538-570):

```livescript
setLabel: (label) !->
    error "label #{label} already defined!" if @_labels.has label
    
    if not @_stack.top.currentlabel.id
        console.warn "warning: no \\@currentlabel available"
    
    @_labels.set label, @_stack.top.currentlabel
    
    # Fill forward references
    if @_refs.has label
        for r in @_refs.get label
            while r.firstChild
                r.removeChild r.firstChild
            
            r.appendChild @_stack.top.currentlabel.label.cloneNode true
            r.setAttribute "href", "#" + @_stack.top.currentlabel.id

getRef: (label) ->
    if @_labels.has label
        # Create link to label
        el = @createHtml "a", href: "#" + @_labels.get(label).id
        el.appendChild @_labels.get(label).label.cloneNode true
        return el
    else
        # Forward reference - store for later resolution
        el = @createHtml "a", href: "#"
        el.textContent = "??"
        
        if not @_refs.has label
            @_refs.set label, []
        @_refs.get(label).push el
        
        return el
```

**Key Patterns**:
1. **Two-Pass Architecture**: First pass collects labels, second pass resolves refs
2. **currentlabel State**: Stack-based tracking of current referenceable position
3. **Forward Reference Storage**: `_refs` map stores unresolved `<a>` elements
4. **Deferred Resolution**: When label defined, update all stored forward refs

#### Implementation Approach

**Phase 1: Label Storage**
```cpp
struct LabelData {
    std::string id;          // Anchor ID (e.g., "sec-1")
    std::string text;        // Display text (e.g., "1.2")
};

class LatexProcessor {
private:
    std::map<std::string, LabelData> labels_;
    std::map<std::string, std::vector<size_t>> forward_refs_;
    std::vector<std::pair<std::string, size_t>> unresolved_refs_;
    
    LabelData current_label_;  // Stack-based in LaTeX.js
};
```

**Phase 2: `\refstepcounter` Integration**
```cpp
static void cmd_refstepcounter(LatexProcessor* proc, Item elem) {
    const char* counter_name = extract_name(elem);
    
    // Increment counter
    proc->stepCounter(counter_name);
    
    // Update currentlabel for next \label
    int value = proc->getCounter(counter_name);
    std::string formatted = proc->formatCounter(counter_name, value);
    std::string id = std::string(counter_name) + "-" + 
                     std::to_string(proc->nextId());
    
    proc->setCurrentLabel(id, formatted);
    
    // Output anchor
    proc->gen()->writer()->writeRaw(
        "<a id=\"" + id + "\"></a>"
    );
}
```

**Phase 3: `\label` Command**
```cpp
static void cmd_label(LatexProcessor* proc, Item elem) {
    const char* label_name = extract_name(elem);
    
    // Store label -> currentlabel mapping
    proc->defineLabel(label_name, proc->getCurrentLabel());
    
    // No HTML output (invisible marker)
}
```

**Phase 4: `\ref` Command with Forward References**
```cpp
static void cmd_ref(LatexProcessor* proc, Item elem) {
    const char* label_name = extract_name(elem);
    
    if (proc->hasLabel(label_name)) {
        // Backward reference - resolved immediately
        LabelData label = proc->getLabel(label_name);
        proc->gen()->writer()->openTag("a");
        proc->gen()->writer()->attr("href", "#" + label.id);
        proc->gen()->text(label.text.c_str());
        proc->gen()->writer()->closeTag("a");
    } else {
        // Forward reference - store for later resolution
        size_t ref_pos = proc->gen()->writer()->getPosition();
        proc->registerForwardRef(label_name, ref_pos);
        
        // Output placeholder
        proc->gen()->writer()->openTag("a");
        proc->gen()->writer()->attr("href", "#");
        proc->gen()->text("??");
        proc->gen()->writer()->closeTag("a");
    }
}
```

**Phase 5: Two-Pass Resolution** (Major Change)
```cpp
Item format_latex_html_v2(Input* input, bool text_mode) {
    LatexProcessor proc(input);
    
    // First pass: collect labels
    proc.setPass(1);
    proc.process(input->root);
    
    // Second pass: resolve forward refs
    proc.setPass(2);
    proc.resetGenerator();
    proc.process(input->root);
    
    return proc.finalizeHtml();
}
```

**Alternative: Single-Pass with Post-Processing**
```cpp
Item format_latex_html_v2(Input* input, bool text_mode) {
    LatexProcessor proc(input);
    proc.process(input->root);
    
    // Post-process: patch forward refs in HTML string
    std::string html = proc.getHtmlString();
    for (auto& [label, positions] : proc.getForwardRefs()) {
        LabelData data = proc.getLabel(label);
        for (size_t pos : positions) {
            // Find and replace "??" with actual text at position
            patchHtmlAtPosition(html, pos, data);
        }
    }
    
    return proc.finalizeHtml(html);
}
```

#### Estimated Effort

- **Phase 1-3**: 2 days (label storage and basic commands)
- **Phase 4**: 1 day (forward reference tracking)
- **Phase 5**: 3 days (two-pass architecture or post-processing)
- **Integration**: 2 days (counter system integration)
- **Testing**: 1 day
- **Total**: ~9 days

---

### 4. Whitespace & Zero-Width Space (P1 - Medium Impact, Low Complexity)

**Failing Tests**: `basic_text_tex_4`, `text_tex_7`, `whitespace_tex_5`, `whitespace_tex_6`, `whitespace_tex_13`, `whitespace_tex_15`, `whitespace_tex_21`, (2 related in groups)

#### Problem Description

LaTeX.js outputs **zero-width space (ZWS, U+200B)** at strategic positions to control HTML whitespace collapsing. Lambda V2 is missing these markers, causing:
- Extra spaces collapsed incorrectly
- Empty groups rendered as no space
- Font environment boundaries losing spacing

**Example Failure** (`basic_text_tex_4`):
```latex
\^{} \& \_
```

**Expected**: `^​ &amp; _` (ZWS after `^`)  
**Actual**: `^ &amp; _` (no ZWS, causes collapse)

#### LaTeX.js Reference Implementation

Located in `/Users/henryluo/Projects/latex-js/src/html-generator.ls` (lines 15-17):

```livescript
sp:     ' '              # Regular space
brsp:   '\u200B '        # U+200B + ' ' breakable non-collapsing
nbsp:   he.decode "&nbsp;"  # U+00A0 non-breaking space
zwnj:   he.decode "&zwnj;"  # U+200C zero-width non-joiner
```

**Usage Patterns**:
1. **After empty groups**: `{}` → output ZWS
2. **Font environment boundaries**: `\begin{small}` → ZWS before/after
3. **Diacritic terminators**: `\^{}` → ZWS after
4. **Macro terminators**: `\empty{}` → ZWS after

#### Implementation Approach

**Quick Fix: Add ZWS Utility**
```cpp
class HtmlGenerator {
public:
    void zws() {
        writer()->writeRaw("\u200B");  // Zero-width space
    }
    
    void brsp() {
        writer()->writeRaw("\u200B ");  // Breakable non-collapsing
    }
};
```

**Pattern 1: Empty Curly Groups**
```cpp
void LatexProcessor::processChildren(Item elem) {
    ElementReader er(elem);
    
    for (size_t i = 0; i < er.childCount(); i++) {
        Item child = er.childAt(i);
        
        if (isEmptyCurlyGroup(child)) {
            gen_->zws();  // Output ZWS for empty {}
        } else {
            processNode(child);
        }
    }
}
```

**Pattern 2: Font Environments**
```cpp
static void cmd_begin_font_env(LatexProcessor* proc, Item elem) {
    gen->zws();  // ZWS before environment
    gen->span("class=\"small\"");
    proc->processChildren(elem);
    gen->zws();  // ZWS after environment
    gen->closeElement();
}
```

**Pattern 3: Diacritics with Empty Terminator**
```cpp
static void cmd_caret(LatexProcessor* proc, Item elem) {
    // Output: ˆ or ê depending on argument
    processAccent(proc, elem, ACCENT_CIRCUMFLEX);
    
    // Check for empty curly group terminator
    if (hasEmptyCurlyGroupChild(elem)) {
        gen->zws();
    }
}
```

#### Estimated Effort

- **Implementation**: 1 day (add ZWS calls to ~15 locations)
- **Testing**: 1 day (verify 9 failing tests)
- **Total**: 2 days

---

### 5. Font Environment Scoping (P2 - Medium Complexity)

**Failing Tests**: `environments_tex_10`, `fonts_tex_7`, `fonts_tex_8`

#### Problem Description

Font environments like `\begin{small}...\end{small}` should **wrap all content in `<span>` tags**, not just set a transient state. Current implementation outputs empty `<span>` markers instead of wrapping.

**Example Failure** (`environments_tex_10`):
```latex
normal text \begin{small}
    small text
    \begin{bfseries}
        bold text
    \end{bfseries}
\end{small}
three spaces!
```

**Expected**:
```html
<p>
normal text <span class="small">​ </span><span class="small">small text </span>
<span class="bf">​ </span><span class="bf">bold text </span>
<span class="bf">​ </span><span class="small">​ </span>
three spaces!
</p>
```

**Actual**:
```html
<p>normal text <span class="small">  small text   bold text  </span> three spaces!</p>
```

#### LaTeX.js Reference Implementation

From `src/latex.ltx.ls` (lines 820-825):

```livescript
[ args[..]  = [ \HV ] for <[ tiny scriptsize footnotesize small normalsize 
                             large Large LARGE huge Huge ]> ]

\tiny        :!-> @g.setFontSize "tiny"
\small       :!-> @g.setFontSize "small"
# ... etc
```

And from `src/html-generator.ls` (behavioral observation):
- Font commands are **declarative** (set state)
- HTML generator **automatically wraps state changes** in `<span>` boundaries
- **Each text run** gets its own `<span>` with current attributes

#### Implementation Approach

**Option A: Attribute-Based Span Insertion** (LaTeX.js approach)
```cpp
class HtmlGenerator {
private:
    struct FontState {
        std::string family;   // rm, sf, tt
        std::string weight;   // md, bf
        std::string shape;    // up, it, sl, sc
        std::string size;     // tiny, small, normalsize, large, ...
    };
    
    FontState current_font_;
    FontState last_output_font_;
    
    void ensureFontSpan() {
        if (current_font_ != last_output_font_) {
            if (last_output_font_.isActive()) {
                closeSpan();  // Close previous <span>
            }
            
            if (current_font_.isActive()) {
                std::string classes = buildFontClasses(current_font_);
                openSpan(classes);  // <span class="small bf">
            }
            
            last_output_font_ = current_font_;
        }
    }
    
public:
    void text(const char* str) {
        ensureFontSpan();  // Insert span before text
        writeText(str);
    }
};
```

**Option B: Environment-Based Wrapping** (Explicit scoping)
```cpp
static void cmd_begin_small(LatexProcessor* proc, Item elem) {
    // Enter font scope
    proc->pushFontState();
    proc->setFontSize("small");
    
    // Start span
    proc->gen()->span("class=\"small\"");
    proc->gen()->zws();  // ZWS marker
    
    // Process content
    proc->processEnvironmentContent(elem);
    
    // End span
    proc->gen()->zws();  // ZWS marker
    proc->gen()->closeElement();
    
    // Exit font scope
    proc->popFontState();
}
```

**Recommended: Hybrid Approach**
- Use **Option A** for inline commands (`\small`, `\textbf{...}`)
- Use **Option B** for environments (`\begin{small}...\end{small}`)
- Provides both automatic and explicit control

#### Estimated Effort

- **Font State Tracking**: 2 days (implement stack-based font state)
- **Span Insertion Logic**: 2 days (ensureFontSpan() mechanism)
- **Environment Handlers**: 1 day (update all font environments)
- **Testing**: 1 day
- **Total**: 6 days

---

### 6. Counter Child Reset (P2 - Low Complexity)

**Failing Test**: `counters_tex_2`

#### Problem Description

When a parent counter is stepped (e.g., `\stepcounter{c}`), all **child counters** should reset to 0. Current implementation does not track parent-child relationships.

**Example Failure**:
```latex
\newcounter{c}
\stepcounter{c}

\newcounter{a}[c]  % a is child of c
\newcounter{b}[c]  % b is child of c
\newcounter{d}[b]  % d is child of b

\setcounter{a}{5}
\setcounter{b}{6}

\stepcounter{c}  % Should reset a, b, d to 0
\arabic{a}       % Expected: 0, Actual: 5
\arabic{b}       % Expected: 0, Actual: 6
```

#### LaTeX.js Reference Implementation

From `src/generator.ls` (lines 430-465):

```livescript
newCounter: (c, parent) !->
    error "counter #{c} already defined!" if @hasCounter c
    @_counters.set c, 0
    @_resets.set c, []  # Children to reset
    
    if parent
        @addToReset c, parent

addToReset: (c, parent) !->
    error "no such counter: #{parent}" if not @hasCounter parent
    error "no such counter: #{c}" if not @hasCounter c
    @_resets.get parent .push c

stepCounter: (c) !->
    @setCounter c, @counter(c) + 1
    @clearCounter c  # Reset children

clearCounter: (c) !->
    for r in @_resets.get c
        @clearCounter r  # Recursive
        @setCounter r, 0
```

**Key Pattern**: Recursive child reset via `_resets` map (parent → [children])

#### Implementation Approach

```cpp
class LatexProcessor {
private:
    std::map<std::string, int> counters_;
    std::map<std::string, std::vector<std::string>> counter_children_;
    
public:
    void defineCounter(const char* name, const char* parent = nullptr) {
        if (counters_.find(name) != counters_.end()) {
            log_warn("Counter %s already exists, redefining", name);
        }
        
        counters_[name] = 0;
        counter_children_[name] = {};  // Initialize child list
        
        if (parent) {
            counter_children_[parent].push_back(name);
        }
    }
    
    void stepCounter(const char* name) {
        counters_[name]++;
        clearCounterChildren(name);
    }
    
    void clearCounterChildren(const char* name) {
        // Recursive reset of all descendants
        for (const std::string& child : counter_children_[name]) {
            clearCounterChildren(child.c_str());
            counters_[child] = 0;
        }
    }
};
```

#### Estimated Effort

- **Implementation**: 0.5 days (add parent tracking to counter system)
- **Testing**: 0.5 days
- **Total**: 1 day

---

### 7. Unicode Character Mapping (P2 - Low Complexity)

**Failing Tests**: `text_tex_4`, `text_tex_6`, `boxes_tex_4`

#### Problem Description

Special Unicode characters are not correctly mapped:
- Regular hyphen `-` should become en-dash (`‐`, U+2010) in certain contexts
- Double hyphen `--` should become en-dash (`–`, U+2013)
- Triple hyphen `---` should become em-dash (`—`, U+2014)
- Ligatures (`ff` → `ﬀ`) handled but not in `\texttt` (monospace should disable ligatures)

**Example Failure** (`text_tex_6`):
```latex
daughter-in-law, pages 13--67, yes---or no?
```

**Expected**: `daughter‐in‐law, pages 13–67, yes—or no?`  
**Actual**: `daughter-in-law, pages 13–67, yes—or no?` (hyphen not converted)

#### Implementation Approach

```cpp
const char* convertDashes(const char* text) {
    static std::string buffer;
    buffer.clear();
    
    const char* p = text;
    while (*p) {
        if (*p == '-') {
            if (*(p+1) == '-' && *(p+2) == '-') {
                buffer += "—";  // Em-dash (U+2014)
                p += 3;
            } else if (*(p+1) == '-') {
                buffer += "–";  // En-dash (U+2013)
                p += 2;
            } else {
                buffer += "‐";  // Hyphen (U+2010)
                p++;
            }
        } else {
            buffer += *p++;
        }
    }
    
    return buffer.c_str();
}

void HtmlGenerator::text(const char* str) {
    const char* converted = convertDashes(str);
    writer()->writeText(converted);
}
```

**Ligature Suppression in Monospace**:
```cpp
static void cmd_texttt(LatexProcessor* proc, Item elem) {
    proc->gen()->span("class=\"tt\"");
    proc->setSuppressLigatures(true);  // Disable ff → ﬀ
    proc->processChildren(elem);
    proc->setSuppressLigatures(false);
    proc->gen()->closeElement();
}
```

#### Estimated Effort

- **Implementation**: 1 day (character mapping + ligature control)
- **Testing**: 0.5 days
- **Total**: 1.5 days

---

### 8. Custom Item Labels (P3)

**Failing Test**: `environments_tex_7`

#### Problem Description

`\item[\itshape label]` should use custom label instead of auto-number. Current implementation ignores optional label argument.

**Example Failure**:
```latex
\begin{enumerate}
    \item[\itshape label] first item
    \item second item
\end{enumerate}
```

**Expected**:
```html
<li>
<span class="itemlabel"><span class="hbox llap"><span>
  <span class="it">label</span>
</span></span></span>
<p>first item</p>
</li>
```

**Actual**: Uses auto-number `1` instead of custom label

#### Implementation Approach

```cpp
static void cmd_item(LatexProcessor* proc, Item elem) {
    ElementReader er(elem);
    
    // Check for optional label argument
    Item label_arg = er.findChild("optional_arg");
    
    proc->gen()->openTag("li");
    proc->gen()->span("class=\"itemlabel\"");
    proc->gen()->span("class=\"hbox llap\"");
    proc->gen()->span();
    
    if (!label_arg.isNull()) {
        // Custom label
        proc->processNode(label_arg);
    } else {
        // Auto-generated label
        std::string label = proc->getCurrentItemLabel();
        proc->gen()->text(label.c_str());
    }
    
    proc->gen()->closeElement();  // </span>
    proc->gen()->closeElement();  // </span>
    proc->gen()->closeElement();  // </span>
    
    // Item content
    proc->processChildren(elem);
    
    proc->gen()->closeTag("li");
}
```

#### Estimated Effort

- **Implementation**: 0.5 days
- **Testing**: 0.5 days
- **Total**: 1 day

---

### 9. Groups & Paragraph Boundaries (P3)

**Failing Tests**: `groups_tex_3`, `text_tex_10`, `boxes_tex_4`

#### Problem Description

Curly group boundaries (`{...}`) can span paragraph breaks but shouldn't automatically close paragraphs. Complex interaction with alignment changes.

**Example**: `groups_tex_3`
```latex
This paragraph { starts a group.

But then a new paragraph begins... } and ends group.
```

**Expected**: Two separate `<p>` tags  
**Actual**: Single `<p>` tag

#### Implementation Approach

Requires careful **paragraph state tracking** across group boundaries. This is a complex edge case with low real-world impact.

**Deferred** to Phase 5 (post-baseline stabilization).

#### Estimated Effort

- **Total**: 3 days (low priority)

---

### 10. Special Edge Cases (P3)

**Failing Tests**: `sectioning_tex_3`, `layout_marginpar_tex_*`

#### `sectioning_tex_3`: Section Inside Macro

**Problem**: `\emph{some \section{Title}}` should close `<span>` before opening `<h2>`, then reopen `<span>`.

**Solution**: Detect block-level elements in inline context, auto-close/reopen parent spans.

**Effort**: 2 days

#### `layout_marginpar_tex_*`: Margin Notes

**Problem**: `\marginpar{note}` not outputting special margin structure.

**Solution**: Implement `<div class="marginpar">` output, separate from main content flow.

**Effort**: 2 days

---

## Implementation Roadmap

### Phase 4A: Quick Wins (1 week)

**Goal**: Maximize test coverage with low-hanging fruit

1. **✅ ZWS Phase 1 Complete** (2 days) **DONE**
   - ✅ Parbreak symbol detection
   - ✅ Space-absorbing command detection (TeX/LaTeX logos)
   - ✅ Proper element tag checking
   - +4 tests moved to baseline

2. **Verbatim Commands** (1 day)
   - Copy designed implementation
   - +3 tests passing

3. **Unicode Character Mapping** (1.5 days)
   - Dash conversion
   - Ligature suppression in `\texttt`
   - +3 tests passing

4. **Counter Child Reset** (1 day)
   - Add parent tracking
   - +1 test passing

5. **Custom Item Labels** (1 day)
   - Handle `\item[...]` optional argument
   - +1 test passing

**Remaining**: 5.5 days → **+10 tests passing** (10/31 = 32% of extended)

---

### Phase 4B: Core Systems (3 weeks)

**Goal**: Implement foundational architecture for extensibility

1. **Label/Reference System** (9 days)
   - Two-pass architecture or post-processing
   - Forward reference resolution
   - Integration with counter system
   - +7 tests passing

2. **Custom Macro System** (9 days)
   - Argument parser
   - Macro definition commands
   - Expansion engine
   - Package loading (echo)
   - +6 tests passing

**Total**: 18 days → **+13 tests passing** (30/38 = 79%)

---

### Phase 4C: Refinement (1 week)

**Goal**: Polish visual fidelity and edge cases

1. **Font Environment Scoping** (6 days)
   - Font state stack
   - Automatic span insertion
   - Environment handlers
   - +3 tests passing

2. **Special Edge Cases** (2 days)
   - Section in inline context
   - Margin notes (basic)
   - +2 tests passing

**Total**: 8 days → **+5 tests passing** (35/38 = 92%)

---

### Phase 4D: Edge Cases (optional, post-stabilization)

1. **Groups & Paragraph Boundaries** (3 days)
   - Complex state tracking
   - +3 tests passing

**Total**: 3 days → **+3 tests passing** (38/38 = 100%)

---

## Testing & Validation Strategy

### Continuous Integration

1. **After Each Implementation**: Run full extended test suite
2. **Regression Prevention**: Ensure baseline 67/67 still passes
3. **HTML Comparison**: Use existing fixture comparison infrastructure

### Test Fixture Organization

Current structure:
```
test/latex/
├── basic_text.tex       (6 tests: 5 baseline + 1 extended)
├── text.tex             (10 tests: 5 baseline + 5 extended)
├── macros.tex           (6 tests: 0 baseline + 6 extended)
├── whitespace.tex       (21 tests: 13 baseline + 8 extended)
... etc
```

**Recommendation**: Move tests from extended to baseline as they pass

### Comparison Tools

```bash
# Run specific test category
./test/test_latex_html_v2_extended.exe --gtest_filter=*macros_tex_2

# Visual diff
diff -u expected.html actual.html | less

# HTML pretty-print for debugging
xmllint --html --format actual.html
```

---

## Risk Assessment

### High-Risk Items

1. **Two-Pass Architecture** (Label/Ref System)
   - **Risk**: Performance impact, memory duplication
   - **Mitigation**: Benchmark on large documents (>100 pages)
   - **Alternative**: Single-pass with HTML post-processing

2. **Macro Expansion Complexity**
   - **Risk**: Infinite recursion, parameter edge cases
   - **Mitigation**: Max expansion depth limit (100), extensive unit tests
   - **Alternative**: Hard-code common packages instead of full system

3. **Font State Span Insertion**
   - **Risk**: Over-nesting of `<span>` tags, bloated HTML
   - **Mitigation**: Coalesce adjacent identical spans, limit nesting depth
   - **Alternative**: Use CSS classes on parent elements

### Low-Risk Items

- Verbatim commands: Implementation already designed
- ZWS markers: Simple text insertion
- Counter child reset: Straightforward recursion
- Unicode mapping: Character substitution

---

## Success Criteria

### Phase 4A Success (Week 1)
- ✅ ZWS Phase 1 complete: 77/77 baseline passing (100%)
- ⏳ Target: +10 extended tests passing (10/31 = 32%)
- ✅ No new compiler warnings
- ⏳ Memory leak tests pass (valgrind)

### Phase 4B Success (Week 4)
- ✅ 30/38 extended tests passing (79%)
- ✅ Macro expansion works for echo package
- ✅ Forward references resolve correctly
- ✅ Performance: <2x slowdown vs current

### Phase 4C Success (Week 5)
- ✅ 35/38 extended tests passing (92%)
- ✅ Font environments render correctly
- ✅ Section numbering matches LaTeX.js

### Final Success (100% Extended Coverage)
- ✅ 38/38 extended tests passing (100%)
- ✅ 67/67 baseline tests passing (100%)
- ✅ Total: 105/105 tests (100%)

---

## Conclusion

This proposal provides a **systematic, phased approach** to achieving 100% extended test coverage. By prioritizing **high-impact, low-complexity** features in Phase 4A, we can quickly demonstrate progress (45% coverage in 1 week). The core systems in Phase 4B (macros, label/ref) are essential for production use and justify the 3-week investment.

The roadmap is **flexible**: if Phase 4B proves too complex, we can defer it and still achieve 45% coverage with quick wins. Similarly, Phase 4C and 4D can be deprioritized if resources are constrained.

**Recommended Start**: Begin with Phase 4A (verbatim commands + quick wins) to build momentum and validate the approach.
