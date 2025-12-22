# LaTeX to HTML V2 - Improvement Proposal (Phase 5)

**Date**: December 22, 2025  
**Status**: Baseline 86/86 (100%), Extended 9/31 (29%)  
**Previous Phase**: Phase 4 - ZWS Implementation Complete  
**Objective**: Systematic analysis of extended test failures with prioritized implementation roadmap

---

## Executive Summary

Analysis of extended test failures reveals **8 major functional categories** requiring implementation. This document provides structured analysis of each failure, root cause identification, and prioritized implementation recommendations.

### Test Status Overview

| Category | Tests | Status |
|----------|-------|--------|
| **Baseline** | 86/86 | ✅ 100% |
| **Extended** | 9/31 | 🟡 29% |
| **Total** | 95/117 | 81% |

### Recent Progress (Dec 22, 2025)

| Change | Tests Fixed | Notes |
|--------|-------------|-------|
| `\textendash` / `\textemdash` commands | environments_tex_6 | Unicode en-dash (–) and em-dash (—) |
| `\echoOGO` / `\echoGOG` handlers | macros_tex_2 | Echo package mandatory/optional arg output |
| Custom enumerate labels `\item[\itshape label]` | environments_tex_7 | Capture mode for rendering label HTML |
| **Two-pass forward reference resolution** | label_ref_tex_2,3,6 | First pass collects labels, second pass resolves |
| **Item label context for \ref** | label_ref_tex_7 (partial) | `setCurrentLabel` called in `createItem` for enumerate |
| **Hyphen U+2010 conversion** | text_tex_4, text_tex_6 | Single hyphens converted to Unicode hyphen U+2010 |

**Moved to Baseline**: `macros_tex_2`, `environments_tex_6`, `environments_tex_7`, `environments_tex_12`, `label_ref_tex_2`, `label_ref_tex_3`, `label_ref_tex_6`, `text_tex_4`, `text_tex_6`

---

## Failure Category Analysis

### Category 1: Custom Macro System (4 tests remaining)

**Failing Tests**: `macros_tex_4`, `macros_tex_5`, `macros_tex_6`, `whitespace_tex_7`  
**Fixed**: ✅ `macros_tex_2` (echoOGO simple case)

**Priority**: P0 (Critical)  
**Complexity**: High  
**Estimated Effort**: 6 days (reduced from 9)

#### Problem Description

Lambda has **no runtime macro expansion system**. LaTeX documents using `\newcommand`, `\renewcommand`, `\def`, or custom packages (`\usepackage{echo}`) fail completely.

#### Failure Details

| Test | LaTeX | Expected | Actual | Issue |
|------|-------|----------|--------|-------|
| ✅ `macros_tex_2` | `\echoOGO{with a macro}` | `+with a macro+` | ✅ FIXED | Handler now outputs +arg+ |
| `macros_tex_4` | `\echoGOG{...}[...]{...}` | Complex output | Raw text | Parser doesn't capture multi-arg patterns |
| `macros_tex_5` | `\echoOGO[{t]}t]{ext}` | `-t]t-+ext+` | `more` | Protected brackets in args |
| `macros_tex_6` | `\echoOGO{again}[%...]{more}` | Multiline output | `[ ]` | Comment handling in args |
| `whitespace_tex_7` | `\gobbleO space` | `space` | `space,` | Missing gobble macros |

#### Root Cause

1. No `\usepackage` command implementation
2. No macro definition system (`\newcommand`, `\def`)
3. No argument parsing for custom macros
4. No macro expansion engine

#### Implementation Strategy

**Phase 1**: Argument Parser Infrastructure
```cpp
struct MacroArgSpec {
    enum Type { OPTIONAL, MANDATORY, STAR };
    Type type;
    std::string default_value;
};

struct MacroDefinition {
    std::string name;
    std::vector<MacroArgSpec> arg_specs;
    Item body;  // AST for expansion
};
```

**Phase 2**: `\usepackage{echo}` Implementation
- Define `echo` package with `\echoOGO`, `\echoGOG`, `\gobbleO` etc.
- Register built-in packages in a map

**Phase 3**: Macro Expansion Engine
- Parse arguments according to spec
- Substitute `#1`, `#2`, etc. in body
- Recursively process expanded AST

---

### Category 2: Label/Reference Forward Resolution (1 test remaining)

**Failing Tests**: `label_ref_tex_7`  
**Fixed**: ✅ `label_ref_tex_2`, `label_ref_tex_3`, `label_ref_tex_6` (two-pass architecture implemented)

**Priority**: P1 (High)  
**Complexity**: Medium  
**Estimated Effort**: 1 day (reduced from 6 - core infrastructure done)

#### Problem Description

Forward references (`\ref{label}` before `\label{label}`) render as `??` instead of the correct value.

#### Failure Details

| Test | Issue | Status |
|------|-------|--------|
| ✅ `label_ref_tex_2` | Forward ref shows `??`, back ref shows `1` | ✅ FIXED |
| ✅ `label_ref_tex_3` | Forward ref to section shows `??` | ✅ FIXED |
| ✅ `label_ref_tex_6` | Forward ref in book class with chapters | ✅ FIXED |
| `label_ref_tex_7` | Forward ref to `\item` label shows `??` | 🔴 Item labels need special handling |

#### Root Cause

~~Single-pass architecture cannot resolve forward references.~~ **SOLVED** with two-pass architecture.

Remaining issue: `\label` inside list items needs to associate with the item's anchor (`item-N`) rather than a counter anchor.

#### Implementation Strategy

**✅ Two-Pass Architecture (IMPLEMENTED)**:
1. **Pass 1**: Use NullHtmlWriter to collect all labels (output discarded)
2. **Pass 2**: Copy labels to new generator, generate HTML with resolved references

**Remaining**: Item label association requires updating `cmd_label` to detect list item context and use the item's anchor ID.

---

### Category 3: Verbatim Commands (1 test)

**Failing Tests**: `text_tex_8`

**Priority**: P0 (Quick Win)  
**Complexity**: Low  
**Estimated Effort**: 1 day

#### Problem Description

`\verb` command exists in grammar but V2 formatter doesn't handle it correctly. The `\verb*` variant (visible spaces) is broken.

#### Failure Details

| Test | LaTeX | Expected | Actual | Issue |
|------|-------|----------|--------|-------|
| `text_tex_8` | `\verb*-is possible, too-` | `<code class="tt">is␣possible,␣too</code>` | Empty output | `\verb*` delimiter mismatch with `-` |

**Log Warning**: `verb_command: missing closing delimiter '*' in: \verb*-is possible, too-`

#### Root Cause

The parser/formatter is treating `*` as the delimiter instead of recognizing `\verb*` as the starred variant with `-` as the actual delimiter.

#### Implementation Strategy

1. Fix delimiter detection: After `\verb`, check for `*` as starred marker, THEN take next char as delimiter
2. For `\verb*`, replace spaces with visible space character (`␣` U+2423)
3. Use CSS class `tt` instead of `latex-verbatim` to match LaTeX.js

---

### Category 4: Font Environment Scoping (3 tests)

**Failing Tests**: `environments_tex_10`, `fonts_tex_7`, `fonts_tex_8`

**Priority**: P2 (Medium)  
**Complexity**: Medium  
**Estimated Effort**: 5 days

#### Problem Description

Font environments (`\begin{small}`, `\tiny`, etc.) don't properly wrap content in styled spans. Text runs should be individually wrapped, not the entire environment.

#### Failure Details

| Test | Issue |
|------|-------|
| `environments_tex_10` | `\begin{small}...\begin{bfseries}...\end{bfseries}\end{small}` outputs single span, not nested |
| `fonts_tex_7` | `\small a line\\[1em]` should output `<span class="small">a line</span><span class="small"><span class="breakspace">...` |
| `fonts_tex_8` | `\tiny ... \Large ...` - each text run needs individual font span |

#### Root Cause

Font commands set internal state but don't wrap each text segment. LaTeX.js wraps **each text run** in its current font state.

#### Implementation Strategy

**Font State Stack**:
```cpp
struct FontState {
    std::string size_class;   // "tiny", "small", "Large", etc.
    std::string style_class;  // "bf", "it", "tt", etc.
};

std::vector<FontState> font_stack_;

void text(const char* str) {
    if (hasFontAttributes()) {
        openFontSpan();
        writer_->writeText(str);
        closeFontSpan();
    } else {
        writer_->writeText(str);
    }
}
```

---

### Category 5: Unicode Character Mapping (3 tests)

**Failing Tests**: `text_tex_4`, `text_tex_6`, `fonts_tex_6`

**Priority**: P2 (Low Complexity)  
**Complexity**: Low  
**Estimated Effort**: 1.5 days

#### Problem Description

Single hyphen `-` should become Unicode hyphen (U+2010 `‐`) in running text. Monospace fonts (`\texttt`) should NOT apply ligature/dash conversions.

#### Failure Details

| Test | LaTeX | Expected | Actual | Issue |
|------|-------|----------|--------|-------|
| `text_tex_4` | `[]-/*@` | `[]‐/*@` | `[]-/*@` | Single hyphen not converted |
| `text_tex_6` | `daughter-in-law` | `daughter‐in‐law` | `daughter-in-law` | Hyphen not Unicode |
| `fonts_tex_6` | `\texttt{-- and ---}` | `-- and ---` (literal) | `– and —` | Monospace shouldn't ligature |
| `boxes_tex_4` | `inner-pos` | `inner‐pos` | `inner-pos` | Hyphen in text |

#### Root Cause

1. Single hyphen `-` not converted to Unicode hyphen `‐` (U+2010)
2. No "monospace mode" flag to disable ligature/dash conversion

#### Implementation Strategy

```cpp
// Track if we're in monospace mode
bool in_monospace_ = false;

std::string convertHyphens(const std::string& text) {
    if (in_monospace_) return text;  // No conversion in monospace
    
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '-') {
            if (i+2 < text.size() && text[i+1] == '-' && text[i+2] == '-') {
                result += "—";  // em-dash
                i += 2;
            } else if (i+1 < text.size() && text[i+1] == '-') {
                result += "–";  // en-dash
                i += 1;
            } else {
                result += "‐";  // Unicode hyphen (U+2010)
            }
        } else {
            result += text[i];
        }
    }
    return result;
}
```

---

### Category 6: Whitespace & Comment Handling (6 tests)

**Failing Tests**: `whitespace_tex_5`, `whitespace_tex_6`, `whitespace_tex_8`, `whitespace_tex_12`, `whitespace_tex_21`, `groups_tex_2`

**Priority**: P1 (Medium)  
**Complexity**: Medium  
**Estimated Effort**: 4 days

#### Problem Description

Various whitespace handling issues including `\mbox`, `\unskip`, `\ignorespaces`, comment line-joining, and space-hack commands.

#### Failure Details

| Test | LaTeX | Expected | Actual | Issue |
|------|-------|----------|--------|-------|
| `whitespace_tex_5` | `\mbox{one \gobbleO space}` | Content in hbox, linebreaks stripped | Linebreaks not stripped in mbox |
| `whitespace_tex_6` | `\unskip}` | Removes preceding space | Trailing comma not removed |
| `whitespace_tex_8` | `x \vspace{1mm} x` | `x x` (vspace silent in text) | `<span class="vspace-inline">` output |
| `whitespace_tex_12` | `Supercal%\nifragilist%` | `Supercalifragilist` | `Supercal ifragilist` (space from newline) |
| `whitespace_tex_21` | `\begin{empty}text\end{empty}` | ZWS around empty env | Extra spaces |
| `groups_tex_2` | `{t]]ext}` | `t]]ext​` with ZWS | Missing `]]ext` |

#### Root Cause

1. `\mbox` doesn't properly restrict line breaking
2. `\unskip` and `\ignorespaces` not implemented
3. `\vspace` should be silent inline, visible block-level
4. Comment `%` doesn't consume the following newline
5. `\begin{empty}` environment not properly handled

#### Implementation Strategy

1. **`\mbox`**: Strip paragraph breaks inside, output as `<span class="hbox">`
2. **`\unskip`**: Track and remove last whitespace token
3. **`\vspace` context**: Check if inline (silent) or block-level (output)
4. **Comment handling**: In parser, consume `%...newline` as single token, eating the newline

---

### Category 7: Paragraph & Group Boundaries (3 tests)

**Failing Tests**: `groups_tex_3`, `text_tex_10`, `sectioning_tex_3`

**Priority**: P2 (Medium)  
**Complexity**: Medium  
**Estimated Effort**: 4 days

#### Problem Description

Complex interactions between groups, paragraphs, and inline elements.

#### Failure Details

| Test | Issue |
|------|-------|
| `groups_tex_3` | Group `{...}` spanning paragraph break should split into two `<p>` |
| `text_tex_10` | `\centering` inside paragraph affects rest of para, `\raggedright` in group affects para until group end |
| `sectioning_tex_3` | `\emph{some \section{Title}}` - section inside inline creates malformed HTML |

#### Root Cause

1. Groups can span paragraph breaks but each paragraph needs its own `<p>`
2. Alignment commands are declarative, affect current paragraph state
3. Block elements inside inline require auto-closing the inline wrapper

#### Implementation Strategy

1. **Group-paragraph interaction**: When paragraph break inside group, close current `<p>`, open new one, maintain group state across
2. **Alignment tracking**: Stack-based alignment state, applied at paragraph level
3. **Block-in-inline**: Detect block elements (`\section`, `\chapter`), auto-close parent inline spans, reopen after

---

### Category 8: Special Features (5 tests remaining)

**Failing Tests**: `boxes_tex_4`, `environments_tex_14`, `layout_marginpar_tex_1`, `layout_marginpar_tex_2`, `layout_marginpar_tex_3`  
**Fixed**: ✅ `environments_tex_7` (custom item labels)

**Priority**: P3 (Lower)  
**Complexity**: Varies  
**Estimated Effort**: 5 days (reduced from 6)

#### Problem Description

Various specialized features not yet implemented.

#### Failure Details

| Test | Feature | Issue |
|------|---------|-------|
| `boxes_tex_4` | `\noindent` placement | Creates nested `<p>` inside list item |
| ✅ `environments_tex_7` | `\item[\itshape label]` | ✅ FIXED - Capture mode renders label HTML |
| `environments_tex_14` | `\begin{comment}...\end{comment}` | Comment environment not stripped |
| `layout_marginpar_tex_*` | `\marginpar{text}` | Margin notes not implemented |

#### Root Cause

1. `\noindent` after list item start creates incorrect nesting
2. ~~`\item` optional argument not used for custom label~~ FIXED
3. `comment` environment treated as regular environment
4. `\marginpar` command not implemented

#### Implementation Strategy

1. **`\noindent`**: Don't create new `<p>` if already in paragraph context
2. ~~**Custom item labels**~~ ✅ IMPLEMENTED: Use capture mode to render label content to HTML string
3. **Comment environment**: Skip all content until `\end{comment}`
4. **Margin notes**: Collect marginpar content, output in separate `<div class="margin-right">` after main content

---

## Prioritized Implementation Roadmap

### ✅ Completed (Dec 22, 2025)

| Task | Tests Fixed | Status |
|------|-------------|--------|
| Custom item labels `\item[\itshape label]` | environments_tex_7 | ✅ DONE |
| Echo package `\echoOGO` simple case | macros_tex_2 | ✅ DONE |
| `\textendash` / `\textemdash` commands | environments_tex_6 | ✅ DONE |
| Custom enumerate labels | environments_tex_12 | ✅ DONE (prior) |
| **Two-pass forward reference resolution** | label_ref_tex_2,3,6 | ✅ DONE |

**Current Result**: 91/115 tests (79%) - 7 extended tests fixed, baseline now 84/84

### Phase 1: Quick Wins (Remaining) - Target: +3 tests

| Task | Tests | Effort | Priority |
|------|-------|--------|----------|
| Unicode hyphen mapping | 3 | 1.5 days | P2 |
| Comment environment | 1 | 0.5 days | P3 |
| Monospace ligature suppression | 1 | 0.5 days | P2 |

**Expected Result**: 94/115 tests (82%)

### Phase 2: Core Systems (Weeks 2-3) - Target: +5 tests

| Task | Tests | Effort | Priority |
|------|-------|--------|----------|
| ~~Label/Reference two-pass~~ | ~~4~~ | ~~6 days~~ | ✅ DONE (3 of 4) |
| Item label association | 1 | 1 day | P1 |
| Macro system (complex cases) | 4 | 6 days | P0 |

**Expected Result**: 99/115 tests (86%)

### Phase 3: Whitespace & Fonts (Week 4) - Target: +9 tests

| Task | Tests | Effort | Priority |
|------|-------|--------|----------|
| Font environment scoping | 3 | 5 days | P2 |
| Whitespace commands | 6 | 4 days | P1 |

**Expected Result**: 102/108 tests (94%)

### Phase 4: Edge Cases (Week 5) - Target: +6 tests

| Task | Tests | Effort | Priority |
|------|-------|--------|----------|
| Paragraph/group boundaries | 3 | 4 days | P2 |
| Margin notes | 3 | 2 days | P3 |

**Expected Result**: 108/108 tests (100%)

---

## Detailed Test-by-Test Analysis

### 1. `boxes_tex_4` - Parbox with \noindent

**File**: boxes.tex, Test ID: 4

**LaTeX**:
```latex
\noindent
The following examples demonstrate all explicit \emph{pos}/\emph{inner-pos} combinations:
\begin{itemize}
\item center alignment:
\noindent
Some text \fbox{\parbox[c][3cm][t]{2cm}{...}}
```

**Issues**:
1. `inner-pos` should be `inner‐pos` (Unicode hyphen)
2. `\noindent` after `\item` creates nested `<p>` structure

**Expected**: Single `<p class="noindent">` containing item content  
**Actual**: `<p>` followed by nested `<p class="noindent">`

---

### 2. ✅ `macros_tex_2` - Custom Macro Package (FIXED)

**File**: macros.tex, Test ID: 2  
**Status**: ✅ FIXED (Dec 22, 2025)

**LaTeX**:
```latex
\documentclass{article}
\usepackage{echo}
\begin{document}
some text \echoOGO{with a macro}
\end{document}
```

**Fix Applied**: Added `cmd_echoOGO` handler that detects text children vs group elements. Uses `child.cstring()` to read mandatory argument content.

**Expected**: `some text +with a macro+`  
**Actual**: ✅ `some text +with a macro+`

---

### 3. `macros_tex_4` - Complex Macro Arguments

**File**: macros.tex, Test ID: 4

**LaTeX**:
```latex
some text, \echoGOG{mandatory arg} [%
,  % another comment
 ]
 {more mandatory} inside.
```

**Issue**: Parse error at position 119 - comment handling in macro arguments

---

### 4. `macros_tex_5` - Protected Brackets

**File**: macros.tex, Test ID: 5

**LaTeX**:
```latex
\echoOGO[{t]}t]{ext} more
```

**Issue**: Parse error - brackets inside `{...}` should be protected

---

### 5. `macros_tex_6` - Macro with Newlines

**File**: macros.tex, Test ID: 6

**LaTeX**:
```latex
some text \echoOGO{again} [ % test

 ]% another comment
```

**Issue**: Parse error - newlines and comments in optional arguments

---

### 6. `sectioning_tex_3` - Section Inside Inline

**File**: sectioning.tex, Test ID: 3

**LaTeX**:
```latex
Start with \emph{some \section{A Section}} text.
```

**Expected**: `<span class="it">some<h2 id="sec-1">1 A Section</h2></span> text.`  
**Actual**: `<span class="it">some</span><h2 id="sec-1">1 A Section</h2></p><p>text.`

**Issue**: Section auto-closes `<span>` but doesn't reopen it for remaining content

---

### 7. `text_tex_4` - Unicode Hyphen

**File**: text.tex, Test ID: 4

**LaTeX**: `.:;,?!'´'()[]-/*@+=`

**Expected**: `()[]‐/*@` (Unicode hyphen)  
**Actual**: `()[]-/*@` (ASCII hyphen)

---

### 8. `text_tex_6` - Dashes

**File**: text.tex, Test ID: 6

**LaTeX**: `daughter-in-law, pages 13--67, yes---or no?`

**Expected**: `daughter‐in‐law, pages 13–67, yes—or no?`  
**Actual**: `daughter-in-law, pages 13–67, yes—or no?`

**Issue**: Single hyphen not converted to Unicode hyphen

---

### 9. `text_tex_8` - Verbatim

**File**: text.tex, Test ID: 8

**LaTeX**:
```latex
This is some \verb|verbatim| text.
\verb/Any{thing/ is possible in there.
And starred \verb*-is possible, too-. Nice, eh?
```

**Expected**: All three produce `<code class="tt">content</code>`  
**Actual**: Third line empty (delimiter parsing issue)

---

### 10. `text_tex_10` - Alignment

**File**: text.tex, Test ID: 10

**LaTeX**:
```latex
This is a horrible test.
\centering
In this paragraph...
{\raggedright But it actually becomes raggedright...}
```

**Issue**: Alignment changes not applied correctly to paragraph classes

---

### 11-13. `environments_tex_10`, `environments_tex_14` (environments_tex_7 FIXED)

| Test | Feature | Issue |
|------|---------|-------|
| ✅ 7 | `\item[\itshape label]` | ✅ FIXED - Capture mode renders label HTML |
| 10 | `\begin{small}...\end{small}` | Font env not wrapping text runs |
| 14 | `\begin{comment}...\end{comment}` | Content not stripped |

---

### 14-16. `layout_marginpar_tex_1/2/3`

All three tests fail because `\marginpar{...}` is not implemented. Requires:
1. Collect margin content during processing
2. Output `<span class="mpbaseline" id="marginref-N">` at insertion point
3. Output `<div class="margin-right">` with collected content after main body

---

### 17-22. `whitespace_tex_5/6/7/8/12/21`

See Category 6 above for detailed breakdown.

---

### 23-24. `groups_tex_2/3`

| Test | Issue |
|------|-------|
| 2 | Parse error on unbalanced brackets `{t]]ext}` |
| 3 | Group spanning paragraph break not handled |

---

### 25-28. `label_ref_tex_2/3/6/7` (3 of 4 FIXED)

| Test | Issue | Status |
|------|-------|--------|
| ✅ `label_ref_tex_2` | Forward ref | ✅ FIXED - Two-pass architecture |
| ✅ `label_ref_tex_3` | Forward ref to section | ✅ FIXED - Two-pass architecture |
| ✅ `label_ref_tex_6` | Forward ref in book class | ✅ FIXED - Two-pass architecture |
| `label_ref_tex_7` | Forward ref to `\item` label | 🔴 Needs item label association |

**Implementation**: Added `NullHtmlWriter` for pass 1 (label collection), then `copyLabelsFrom()` to share labels with pass 2 generator.

---

### 29-31. `fonts_tex_6/7/8`

| Test | Issue |
|------|-------|
| 6 | `\texttt` should suppress dash ligatures |
| 7 | Font size commands need to wrap each text run |
| 8 | Same as 7, multiple font sizes in paragraph |

---

## LaTeX.js Reference Implementation Notes

The LaTeX.js implementation (LiveScript) provides reference behavior for:

### Macro System (`src/packages/*.ls`)
- Macros defined as functions with argument specs
- `@args` object defines argument types: `H` (horizontal), `o?` (optional), `g` (mandatory group)
- Package loading via require/import system

### Label/Reference (`src/generator.ls`)
```livescript
setLabel: (label) !->
    @_labels.set label, {id: @currentLabel.id, text: @currentLabel.text}

getRef: (label) ->
    if @_labels.has label
        @_labels.get label
    else
        @_unresolvedRefs.push {label, node}
        "??"
```

### Font Scoping (`src/html-generator.ls`)
- Each text output checks current font state
- Opens/closes `<span>` tags for font attributes
- Attribute-based (size + style tracked separately)

### Verbatim (`src/latex.ltx.ls`)
```livescript
\verb:  (s) !->
    match = s.match /^\\verb(\*?)(.)(.*?)\2/
    [_, star, delim, content] = match
    if star then content = content.replace /\ /g, "␣"
    @g.inline \code, {class: \tt}, content
```

---

## Recommended Next Steps

### Immediate (This Week)

1. **Fix `\verb*` delimiter detection** (1 day)
   - Parse `\verb*X...X` where X is delimiter after `*`
   - Add visible space replacement for starred variant

2. **Add Unicode hyphen conversion** (1 day)
   - Convert single `-` to `‐` (U+2010) in text
   - Track `in_monospace_` flag to skip in `\texttt`

3. **Implement comment environment** (0.5 days)
   - Skip all content between `\begin{comment}` and `\end{comment}`

### Short-term (Next 2 Weeks)

4. **Two-pass label/reference system** (6 days)
   - First pass: collect all `\label` definitions
   - Second pass: resolve `\ref` with collected labels

5. **Basic macro system** (6 days)
   - Implement `\usepackage{echo}` with hardcoded macros
   - Add `\gobbleO`, `\echoOGO`, `\echoGOG` implementations

### Medium-term (Month)

6. **Font environment scoping** (5 days)
7. **Whitespace command fixes** (4 days)
8. **Group/paragraph boundary handling** (4 days)

---

## Appendix: Test Failure Summary Table

| Test | Category | Issue | Priority | Status |
|------|----------|-------|----------|--------|
| `boxes_tex_4` | Unicode/Noindent | Hyphen + nested p | P2 | 🔴 |
| ✅ `macros_tex_2` | Macros | Package loading | P0 | ✅ FIXED |
| `macros_tex_4` | Macros | Comment in args | P0 | 🔴 |
| `macros_tex_5` | Macros | Protected brackets | P0 | 🔴 |
| `macros_tex_6` | Macros | Newlines in args | P0 | 🔴 |
| `sectioning_tex_3` | Block-in-inline | Section in emph | P2 | 🔴 |
| `text_tex_4` | Unicode | Single hyphen | P2 | 🔴 |
| `text_tex_6` | Unicode | Single hyphen | P2 | 🔴 |
| `text_tex_8` | Verbatim | verb* delimiter | P0 | 🔴 |
| `text_tex_10` | Alignment | Paragraph alignment | P2 | 🔴 |
| ✅ `environments_tex_6` | Commands | textendash | P2 | ✅ FIXED |
| ✅ `environments_tex_7` | Lists | Custom item label | P3 | ✅ FIXED |
| `environments_tex_10` | Fonts | Font env scoping | P2 | 🔴 |
| `environments_tex_14` | Environments | Comment env | P3 | 🔴 |
| `layout_marginpar_tex_1` | Layout | Margin notes | P3 | 🔴 |
| `layout_marginpar_tex_2` | Layout | Margin notes | P3 | 🔴 |
| `layout_marginpar_tex_3` | Layout | Margin notes | P3 | 🔴 |
| `whitespace_tex_5` | Whitespace | mbox content | P1 | 🔴 |
| `whitespace_tex_6` | Whitespace | unskip | P1 | 🔴 |
| `whitespace_tex_7` | Macros | gobbleO | P0 | 🔴 |
| `whitespace_tex_8` | Whitespace | vspace context | P1 | 🔴 |
| `whitespace_tex_12` | Whitespace | Comment newline | P1 | 🔴 |
| `whitespace_tex_21` | Whitespace | empty env ZWS | P1 | 🔴 |
| `groups_tex_2` | Parser | Unbalanced brackets | P2 | 🔴 |
| `groups_tex_3` | Groups | Paragraph spanning | P2 | 🔴 |
| ✅ `label_ref_tex_2` | Labels | Forward ref | P1 | ✅ FIXED |
| ✅ `label_ref_tex_3` | Labels | Forward ref | P1 | ✅ FIXED |
| ✅ `label_ref_tex_6` | Labels | Forward ref | P1 | ✅ FIXED |
| `label_ref_tex_7` | Labels | Item label ref | P1 | 🔴 |
| `fonts_tex_6` | Fonts | tt ligature | P2 | 🔴 |
| `fonts_tex_7` | Fonts | Font wrapping | P2 | 🔴 |
| `fonts_tex_8` | Fonts | Font wrapping | P2 | 🔴 |

**Total**: 31 tests analyzed, **7 FIXED** (23%), 24 remaining

**Remaining Effort**: ~25-30 days (reduced from 35-40)
