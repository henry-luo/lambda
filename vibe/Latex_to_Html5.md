# LaTeX to HTML Extended Test Resolution Plan

## Summary

**Current Status**: Baseline tests 100% passing (28/28), Extended tests need work
- Baseline: 28 passed, 0 skipped
- Extended: 81 fixtures loaded, 68 failing, 4 skipped, 9 passing

This document analyzes all 68 failing extended tests and provides a structured plan to resolve them, including necessary parser improvements.

---

## Failing Test Analysis

### Test Breakdown by Category

| Category | Failing Tests | Root Cause |
|----------|---------------|------------|
| **Whitespace** | 17 tests | Parser: escaped newlines, `\empty`, control space handling |
| **Macros** | 6 tests | Parser: custom macro definitions, `\newcommand` expansion |
| **Labels/Refs** | 7 tests | Formatter: label/reference system not implemented |
| **Counters** | 2 tests | Formatter: counter system not implemented |
| **Fonts** | 8 tests | Formatter: font declaration scope, `\em` toggling |
| **Symbols** | 3 tests | Parser: `\char`, `^^`, `^^^^`, `\symbol{}` not implemented |
| **Boxes** | 4 tests | Formatter: `\parbox`, `\fbox` not implemented |
| **Groups** | 3 tests | Formatter: groups spanning paragraph breaks |
| **Spacing** | 3 tests | Formatter: control space `\ `, vertical breaks |
| **Environments** | 4 tests | Parser + Formatter: font environments, alignment, abstract |
| **Preamble** | 1 test | Formatter: preamble commands outputting content |
| **Layout/Marginpar** | 3 tests | Formatter: marginpar, layout commands not implemented |
| **Basic Text** | 4 tests | Formatter: HTML entity escaping (quotes) |
| **Formatting** | 1 test | Formatter: alignment environment class names |
| **Text** | 1 test | Formatter: alignment commands affecting paragraph class |

---

## Category 1: Whitespace Handling (17 tests)

### Failing Tests
- `whitespace_tex_1` through `whitespace_tex_4`
- `whitespace_tex_9` through `whitespace_tex_21`

### Issue Analysis

#### 1.1 Escaped Newline at Paragraph Start (`whitespace_tex_19`)
**Input**: `\<newline>x`
**Expected**: `<p>​ x</p>` (ZWSP + space + x)
**Actual**: `<p>x</p>` (missing ZWSP)

**Root Cause**: Parser doesn't generate ZWSP for escaped newline `\<newline>`.

#### 1.2 Control Space `\ ` Not Preserved (`whitespace_tex_14-16`)
**Input**: `some \empty  text.` or `some \empty{} text.` or `some \empty\ text.`
**Expected**: `<p>some ​ text.</p>` (ZWSP + space)
**Actual**: `<p>some ​text.</p>` (missing space)

**Root Cause**: Control space `\ ` after `\empty` not generating explicit space.

#### 1.3 `\phantom{}` and Spacing Commands (`whitespace_tex_11, 13, 17`)
**Input**: `\phantom{xxx}` or `\vphantom{A}` or `\hphantom{yyy}`
**Expected**: `<span style="visibility:hidden">xxx</span>`
**Actual**: Content rendered visibly

**Root Cause**: Phantom commands not implemented.

#### 1.4 `~` vs `\ ` Distinction (`whitespace_tex_1-4`)
- `~` should be `&nbsp;`
- `\ ` should be ZWSP + space (word boundary preserved)

### Required Changes

**Parser (`input-latex.cpp`)**:
1. Add escaped newline handling: `\<newline>` → ZWSP element
2. Fix control space `\ ` to generate explicit space, not just consume it
3. Add `\phantom{}`, `\vphantom{}`, `\hphantom{}` as elements with visibility attribute

**Formatter (`format-latex-html.cpp`)**:
1. Handle phantom elements with `style="visibility:hidden"`
2. Ensure ZWSP + space for word boundary commands

---

## Category 2: Macros (6 tests)

### Failing Tests
- `macros_tex_1` through `macros_tex_6`

### Issue Analysis

#### 2.1 `\empty` Command (`macros_tex_1`)
**Input**: `some text \empty\ inside.`
**Expected**: `<p>some text ​ inside.</p>` (ZWSP + space)
**Actual**: `<p>some text ​inside.</p>` (missing space)

**Root Cause**: `\empty` followed by control space `\ ` loses the space.

#### 2.2 Custom Macro Definitions (`macros_tex_2-6`)
**Input**:
```latex
\newcommand{\test}{content}
\test
```
**Expected**: Expanded content
**Actual**: Raw command output or missing

**Root Cause**: `\newcommand`, `\renewcommand`, `\providecommand` not implemented.

### Required Changes

**Parser (`input-latex.cpp`)**:
1. Track macro definitions during parsing
2. Implement `\newcommand{\name}[args]{definition}` storage
3. Expand macros when encountered
4. Support optional arguments `\newcommand{\name}[2][default]{...}`

**New File**: `lambda/input/latex-macros.hpp`
```cpp
struct MacroDefinition {
    std::string name;
    int num_args;
    std::string default_arg;  // for optional first arg
    std::string definition;
};

class MacroRegistry {
    std::map<std::string, MacroDefinition> macros;
public:
    void define(const std::string& name, int num_args, const std::string& def);
    std::string expand(const std::string& name, const std::vector<std::string>& args);
};
```

---

## Category 3: Labels and References (7 tests)

### Failing Tests
- `label_ref_tex_1` through `label_ref_tex_7`

### Issue Analysis

**Input**: `This\label{test} label is empty:~\ref{test}.`
**Expected**: `<p>This label is empty:&nbsp;<a href="#"></a>.</p>`
**Actual**: `<p>This<p>test</p> label is empty: <p>test</p>.</p>` (treating label name as content)

**Root Cause**: `\label{}` and `\ref{}` not implemented as reference commands.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Add label storage: `std::map<std::string, LabelInfo> labels`
2. Track `\currentlabel` (counter value when label was set)
3. Implement `\label{name}` - stores current counter state
4. Implement `\ref{name}` - generates `<a href="#name">counter_value</a>`
5. Implement `\pageref{name}` - page references (always empty in HTML)

**Data Structure**:
```cpp
struct LabelInfo {
    std::string id;
    std::string counter_value;  // e.g., "1.2.3" for section
    std::string anchor_id;
};

class LabelRegistry {
    std::map<std::string, LabelInfo> labels;
    std::string current_label;  // updated by sectioning, counters
public:
    void set_label(const std::string& name);
    std::string get_ref(const std::string& name);
};
```

---

## Category 4: Counters (2 tests)

### Failing Tests
- `counters_tex_1`, `counters_tex_2`

### Issue Analysis

**Input**:
```latex
\newcounter{c}
\stepcounter{c}
\thec: \roman{c}
```
**Expected**: `<p>1: i</p>`
**Actual**: `<p><p>c</p><p>c</p>: <p>c</p>` (outputting counter name, not value)

**Root Cause**: Counter system not implemented.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Add counter storage and operations
2. Implement counter commands:
   - `\newcounter{name}[parent]` - create counter
   - `\setcounter{name}{value}` - set value
   - `\stepcounter{name}` - increment, reset children
   - `\addtocounter{name}{value}` - add to counter
   - `\value{name}` - get numeric value
3. Implement counter formatters:
   - `\arabic{name}` → "1", "2", ...
   - `\roman{name}` → "i", "ii", ...
   - `\Roman{name}` → "I", "II", ...
   - `\alph{name}` → "a", "b", ...
   - `\Alph{name}` → "A", "B", ...
   - `\fnsymbol{name}` → *, †, ‡, ...
   - `\the<name>` → formatted value

**Data Structure**:
```cpp
struct Counter {
    int value = 0;
    std::string parent;  // reset when parent increments
    std::vector<std::string> children;
};

class CounterRegistry {
    std::map<std::string, Counter> counters;
public:
    void new_counter(const std::string& name, const std::string& parent = "");
    void set_counter(const std::string& name, int value);
    void step_counter(const std::string& name);
    void add_to_counter(const std::string& name, int delta);
    int get_value(const std::string& name);
    std::string format_arabic(const std::string& name);
    std::string format_roman(const std::string& name, bool upper);
    std::string format_alph(const std::string& name, bool upper);
    std::string format_fnsymbol(const std::string& name);
};
```

---

## Category 5: Font Handling (8 tests)

### Failing Tests
- `fonts_tex_1` through `fonts_tex_8`

### Issue Analysis

#### 5.1 `\em` Toggle Not Working (`fonts_tex_2`)
**Input**: `\em You can also \emph{emphasize} text.`
**Expected**:
```html
<span class="it">You can also </span>
<span class="it"><span class="up">emphasize</span></span>
<span class="it"> text.</span>
```
**Actual**: Missing span boundaries, no toggle

**Root Cause**: `\em` is a declaration that toggles italic/upright, and `\emph{}` should toggle based on current em state.

#### 5.2 Font Declaration Scope (`fonts_tex_3-8`)
Font declarations like `\bfseries`, `\itshape` should affect all following text until group ends.

**Root Cause**: Font declarations not properly scoped.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Add `em_active` flag to FontState
2. `\em` toggles: if roman→italic, if italic→upright
3. `\emph{}` checks `em_active` to determine output style
4. Track font declaration scope through groups
5. Output span boundaries at font state changes

---

## Category 6: Symbols (3 tests)

### Failing Tests
- `symbols_tex_1`, `symbols_tex_2`, `symbols_tex_3`

### Issue Analysis

#### 6.1 `\char` Command (`symbols_tex_1`)
**Input**: `\char98 \char"A0`
**Expected**: `<p>b &nbsp;</p>`
**Actual**: `<p>98 "A0</p>` (literal argument output)

**Root Cause**: `\char` not parsed as character code.

#### 6.2 TeX Hex Notation (`symbols_tex_2`)
**Input**: `^^41 ^^^^0041`
**Expected**: `<p>A A</p>`
**Actual**: Literal `^^` output

**Root Cause**: TeX hex notation not implemented in parser.

#### 6.3 `\symbol{}` Command (`symbols_tex_3`)
**Input**: `\symbol{"00A9}`
**Expected**: `<p>©</p>`
**Actual**: Not parsed

### Required Changes

**Parser (`input-latex.cpp`)**:
1. Implement `\char<decimal>` - convert to Unicode
2. Implement `\char"<hex>` - convert hex to Unicode
3. Implement `\char'<octal>` - convert octal to Unicode
4. Implement `^^<two hex digits>` inline notation
5. Implement `^^^^<four hex digits>` inline notation
6. Implement `\symbol{"code}` command

---

## Category 7: Boxes (4 tests)

### Failing Tests
- `boxes_tex_2` through `boxes_tex_5`

### Issue Analysis

**Input**:
```latex
\fbox{\parbox{2cm}{parbox content}}
```
**Expected**:
```html
<span class="parbox p-c p-cc frame" style="width:75.591px;">
<span>parbox content</span>
</span>
```
**Actual**: Nested `<p>` tags, raw dimension output

**Root Cause**: `\parbox`, `\fbox` not implemented.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Implement `\fbox{content}` → `<span class="frame">content</span>`
2. Implement `\parbox[align][height][inner]{width}{content}`:
   - Parse optional alignment arguments
   - Convert width dimension to CSS
   - Generate appropriate span structure
3. Implement `\mbox{content}` → `<span class="hbox"><span>content</span></span>`
4. Add dimension conversion (cm, mm, pt, em, ex → px)

---

## Category 8: Groups (3 tests)

### Failing Tests
- `groups_tex_1` through `groups_tex_3`

### Issue Analysis

**Input**:
```latex
This paragraph { starts a group.

But then a new paragraph begins... } end.
```
**Expected**: Groups can span paragraph breaks, ZWSP at boundaries
**Actual**: Broken HTML structure

**Root Cause**: Groups spanning `\par` or blank lines not handled.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Groups should not close at paragraph boundaries
2. Track group stack separately from paragraph state
3. Insert ZWSP at group boundaries
4. Handle `\par` inside groups properly

---

## Category 9: Spacing (3 tests)

### Failing Tests
- `spacing_tex_1`, `spacing_tex_3`, `spacing_tex_4`

### Issue Analysis

#### 9.1 Control Space After Commands (`spacing_tex_1`)
**Input**: `Normal space: -\ -`
**Expected**: `-​ -` (ZWSP + space)
**Actual**: `--` (no space)

#### 9.2 Vertical Break Commands (`spacing_tex_3, 4`)
**Input**: `\smallbreak`, `\medbreak`, `\bigbreak`
**Expected**: Paragraph break with vertical space class
**Actual**: Not recognized

### Required Changes

**Parser (`input-latex.cpp`)**:
1. Fix control space `\ ` to generate ZWSP + space

**Formatter (`format-latex-html.cpp`)**:
1. Implement `\smallbreak` → `<p class="vskip small">`
2. Implement `\medbreak` → `<p class="vskip med">`
3. Implement `\bigbreak` → `<p class="vskip big">`
4. Implement `\smallskip`, `\medskip`, `\bigskip`

---

## Category 10: Environments (4 tests)

### Failing Tests
- `environments_tex_2` (itemize - parbreak after comment)
- `environments_tex_10` (font environments)
- `environments_tex_11` (alignment environments)
- `environments_tex_12` (alignment of lists)
- `environments_tex_13` (abstract environment)

### Issue Analysis

#### 10.1 Itemize After Comment + Blank Line (`environments_tex_2`)
**Expected**: `<p>more text</p>` (no class="continue")
**Actual**: `<p class="continue">more text</p>`

**Root Cause**: Parser doesn't preserve parbreak after `% comment<newline><blank line>`.

#### 10.2 Font Environments (`environments_tex_10`)
**Input**:
```latex
\begin{small}
text
\begin{bfseries}
bold
\end{bfseries}
\end{small}
```
**Expected**: Nested spans with proper ZWSP
**Actual**: Missing ZWSP, improper nesting

#### 10.3 Alignment in Environments (`environments_tex_11, 12`)
**Issue**: `\centering` inside list items should affect item class.

#### 10.4 Abstract Environment (`environments_tex_13`)
**Input**: `\begin{abstract}...\end{abstract}`
**Expected**: Abstract title + quotation wrapper
**Actual**: Just paragraph content

### Required Changes

**Parser (`input-latex.cpp`)**:
1. Fix parbreak preservation after comment + blank line
2. Emit parbreak element when comment followed by blank line

**Formatter (`format-latex-html.cpp`)**:
1. Add abstract environment handling:
   - Generate title div with "Abstract"
   - Wrap content in quotation div
   - Apply small font size
2. Fix font environment nesting with ZWSP boundaries
3. Propagate alignment to list items

---

## Category 11: Preamble (1 test)

### Failing Tests
- `preamble_tex_1`

### Issue Analysis

**Input**:
```latex
\documentclass{book}
\usepackage{geometry}
...
\begin{document}
\end{document}
```
**Expected**: `<div class="body"></div>` (empty body)
**Actual**: Package names output as paragraphs

**Root Cause**: Preamble commands (`\documentclass`, `\usepackage`, `\setlength`, etc.) should not output content.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Recognize preamble commands and suppress output:
   - `\documentclass[opts]{class}`
   - `\usepackage[opts]{package}`
   - `\setlength{...}{...}`
   - `\setcounter{...}{...}`
2. Only output content inside `\begin{document}...\end{document}`

---

## Category 12: Layout/Marginpar (3 tests)

### Failing Tests
- `layout_marginpar_tex_1` through `layout_marginpar_tex_3`

### Issue Analysis

**Input**: `\marginpar{note text}`
**Expected**: Margin container with positioned note
**Actual**: Inline paragraph

**Root Cause**: Marginpar system not implemented.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Add margin note collection during processing
2. Generate margin container after body
3. Implement `\marginpar{content}`:
   - Insert baseline marker in main text
   - Collect note content for margin container
4. Add counter for marginpar references

---

## Category 13: Basic Text (4 tests)

### Failing Tests
- `basic_text_tex_3` through `basic_text_tex_6`

### Issue Analysis

**Input**: `"quotes"`
**Expected**: `<p>"quotes"</p>` (literal quotes)
**Actual**: `<p>&quot;quotes&quot;</p>` (HTML entities)

**Root Cause**: Over-aggressive HTML entity escaping.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Only escape `<`, `>`, `&` in text content
2. Preserve `"` as literal character (not `&quot;`)
3. Handle already-converted smart quotes from parser

---

## Category 14: Formatting (1 test)

### Failing Tests
- `formatting_tex_6`

### Issue Analysis

**Input**: `\begin{center}...\end{center}`
**Expected**: `<div class="latex-center">...</div>`
**Actual**: `<div class="list center">...</div>`

**Root Cause**: Using "list center" instead of "latex-center" class.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Change alignment environment class names:
   - `center` → `latex-center`
   - `flushleft` → `latex-flushleft`
   - `flushright` → `latex-flushright`

---

## Category 15: Text Alignment (1 test)

### Failing Tests
- `text_tex_10`

### Issue Analysis

**Input**:
```latex
This is text.
\centering
This becomes centered.
{\raggedright But actually raggedright,

even after par will be centered.}
Until group ends.
```
**Expected**: First paragraph gets `raggedright` class (from inner `\raggedright`), second gets `centering`.
**Actual**: First paragraph has no class.

**Root Cause**: Alignment commands affect paragraph class at END of paragraph, not beginning. Requires buffered paragraph output.

### Required Changes

**Formatter (`format-latex-html.cpp`)**:
1. Buffer paragraph content instead of streaming
2. Apply alignment class at paragraph close, using final alignment state
3. Track alignment through groups with save/restore

---

## Implementation Phases

### Phase 1: Quick Wins (1 week)
**Target**: +15 tests passing

| Priority | Category | Tests | Effort |
|----------|----------|-------|--------|
| HIGH | Basic Text | 4 | 1 day |
| HIGH | Formatting | 1 | 0.5 day |
| HIGH | Preamble | 1 | 0.5 day |
| MED | Spacing | 3 | 1 day |
| MED | Font Scoping | 3 | 2 days |

**Focus**: Simple formatter changes, no parser work.

### Phase 2: Parser Improvements (2 weeks)
**Target**: +12 tests passing

| Priority | Category | Tests | Effort |
|----------|----------|-------|--------|
| HIGH | Whitespace | 10 | 3 days |
| HIGH | Symbols | 3 | 2 days |
| MED | Macros (basic) | 2 | 3 days |

**Focus**: Parser-level changes for whitespace, symbols, basic macros.

### Phase 3: Counter & Reference System (1 week)
**Target**: +9 tests passing

| Priority | Category | Tests | Effort |
|----------|----------|-------|--------|
| HIGH | Counters | 2 | 3 days |
| HIGH | Labels/Refs | 7 | 4 days |

**Focus**: Implement counter registry and label/reference resolution.

### Phase 4: Complex Features (2 weeks)
**Target**: +15 tests passing

| Priority | Category | Tests | Effort |
|----------|----------|-------|--------|
| MED | Macros (full) | 4 | 4 days |
| MED | Boxes | 4 | 3 days |
| MED | Groups | 3 | 2 days |
| LOW | Environments | 4 | 3 days |

**Focus**: Advanced macro expansion, box layouts, group spanning.

### Phase 5: Layout & Polish (1 week)
**Target**: +7 tests passing

| Priority | Category | Tests | Effort |
|----------|----------|-------|--------|
| LOW | Layout/Marginpar | 3 | 3 days |
| LOW | Whitespace edge cases | 7 | 2 days |

**Focus**: Marginpar system, remaining edge cases.

---

## Parser Improvements Summary

### Required Parser Changes (`input-latex.cpp`)

1. **Escaped Newline**: `\<newline>` → ZWSP element
2. **Control Space**: `\ ` → explicit space character preservation
3. **`\char` Commands**: Parse decimal, hex, octal character codes
4. **TeX Hex Notation**: `^^XX` and `^^^^XXXX` inline codes
5. **`\symbol{}` Command**: Parse and convert
6. **Parbreak After Comments**: Preserve parbreak when comment followed by blank line
7. **Phantom Commands**: `\phantom{}`, `\vphantom{}`, `\hphantom{}` elements
8. **Macro Registry**: Store and expand custom macros

### New Parser Data Structures

```cpp
// Macro storage
struct MacroDefinition {
    std::string name;
    int num_args;
    std::string default_arg;
    std::string body;
};

class LaTeXParser {
    std::map<std::string, MacroDefinition> macros_;

    void define_macro(const std::string& cmd, int args, const std::string& body);
    std::string expand_macro(const std::string& name, const std::vector<std::string>& args);
};
```

---

## Formatter Improvements Summary

### Required Formatter Changes (`format-latex-html.cpp`)

1. **Counter Registry**: Full counter system with formatters
2. **Label Registry**: Label storage and reference resolution
3. **Font State Stack**: Proper scoping through groups
4. **`\em` Toggle**: Track em state for emphasis toggle
5. **Buffered Paragraphs**: Buffer content for retroactive alignment
6. **Box Commands**: `\fbox`, `\parbox`, `\mbox` implementations
7. **Phantom Commands**: Hidden visibility spans
8. **Environment Classes**: Fix alignment environment class names
9. **Preamble Suppression**: Don't output preamble command arguments
10. **Marginpar System**: Collect and render margin notes

### New Formatter Data Structures

```cpp
// Counter system
struct Counter {
    int value = 0;
    std::string parent;
    std::vector<std::string> children;
};

class CounterRegistry {
    std::map<std::string, Counter> counters;
    // ... methods for counter operations
};

// Label system
struct LabelInfo {
    std::string id;
    std::string counter_value;
    std::string anchor_id;
};

class LabelRegistry {
    std::map<std::string, LabelInfo> labels;
    std::string current_label;
    // ... methods for label operations
};

// Margin notes
struct MarginNote {
    int id;
    std::string content;
};

class MarginparCollector {
    std::vector<MarginNote> notes;
    int next_id = 1;
    // ... methods for note collection
};
```

---

## Success Metrics

| Phase | Target Pass Rate | Tests Passing | Timeline |
|-------|-----------------|---------------|----------|
| Baseline | 100% | 28/28 | ✅ Complete |
| Phase 1 | +15 | ~24/81 | Week 1 |
| Phase 2 | +12 | ~36/81 | Week 2-3 |
| Phase 3 | +9 | ~45/81 | Week 4 |
| Phase 4 | +15 | ~60/81 | Week 5-6 |
| Phase 5 | +7 | ~67/81 | Week 7 |

**Final Target**: 80%+ extended test pass rate (65+/81)

---

## Files to Modify

### Parser
- `lambda/input/input-latex.cpp` - Core parser changes
- `lambda/input/latex-macros.hpp` (new) - Macro registry

### Formatter
- `lambda/format/format-latex-html.cpp` - Main formatter
- `lambda/format/format-latex-html.h` - Add state structures
- `lambda/format/latex-counters.hpp` (new) - Counter registry
- `lambda/format/latex-labels.hpp` (new) - Label registry

### Tests
- `test/latex/test_latex_html_extended.cpp` - Update as tests pass
- `test/latex/test_latex_html_baseline.cpp` - Potentially move passing extended tests

---

## Notes

- Parser changes should be made incrementally with tests
- Counter system is prerequisite for label/reference system
- Font scoping fixes may help multiple test categories
- Consider adding debug logging for complex features
- HTML comparison is whitespace-normalized, focus on semantic correctness
