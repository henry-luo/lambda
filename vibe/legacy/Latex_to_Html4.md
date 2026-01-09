# LaTeX to HTML Converter Enhancement Plan

## Summary

**Current Status (Phase 2 In Progress)**: 15 failures out of 33 baseline tests (~55% pass rate)
- Baseline tests: 18/33 passing, 1 skipped
- Extended tests: Not yet started

Based on analysis of the LaTeX.js library and failing test fixtures, this document outlines a prioritized plan to improve the C/C++ LaTeX to HTML converter implementation.

---

## Design Overview

### Architecture

Lambda's LaTeX-to-HTML conversion uses a two-stage pipeline:

```
LaTeX Source → Parser → Lambda AST → Formatter → HTML Output
                ↓                       ↓
        input-latex.cpp         format-latex-html.cpp
```

### Stage 1: Parser (`lambda/input/input-latex.cpp`)

The parser converts LaTeX source into Lambda's internal data representation using `MarkBuilder`. Key responsibilities:

1. **Tokenization & Command Recognition**
   - Identifies commands (`\textbf`, `\section`, etc.)
   - Parses arguments (braced `{}`, bracketed `[]`, delimited)
   - Handles special syntax (`\verb|...|`, `$math$`, `%comments`)

2. **Character-Level Processing**
   - Smart quote conversion (`` ` `` → U+2018 ', `'` → U+2019 ')
   - TeX ligatures in source (`--` → en-dash, `---` → em-dash)
   - Diacritic combinations (`\"a` → ä, `\'e` → é, `\"\i` → ï)
   - Symbol commands (`\ss` → ß, `\ae` → æ, `\o` → ø)

3. **AST Construction**
   - Creates elements with type and attributes
   - Handles nesting (environments, groups, arguments)
   - Preserves whitespace semantics

**Parser Output Structure** (simplified):
```
document
├── section { title: "Introduction" }
│   ├── textbf { }
│   │   └── "bold text"
│   └── " regular text"
├── itemize { }
│   ├── item { }
│   │   └── "first item"
│   └── item { }
│       └── "second item"
```

### Stage 2: Formatter (`lambda/format/format-latex-html.cpp`)

The formatter traverses the Lambda AST and generates HTML. Key responsibilities:

1. **Element-to-Tag Mapping**
   - `textbf` → `<b>`, `textit` → `<i>`, `section` → `<h2>`
   - Environment wrappers (`itemize` → `<ul>`, `enumerate` → `<ol>`)

2. **Font State Management**
   - Tracks `FontState` through nesting (family, series, shape, size)
   - Handles declarations (`\bfseries`) vs commands (`\textbf{}`)
   - Properly scopes changes within groups

3. **Typography Processing**
   - Ligature conversion (ff → ﬀ, fi → ﬁ, etc.)
   - Space commands (`\,` → thin space, `\quad` → em space)
   - Special characters (`\$` → $, `\&` → &)

4. **Counter System**
   - Tracks section/chapter/figure numbers
   - Generates numbered headings and list items

**HTML Output Structure**:
```html
<h2>Introduction</h2>
<p><b>bold text</b> regular text</p>
<ul>
  <li>first item</li>
  <li>second item</li>
</ul>
```

### Key Data Structures

**FontState** (formatter):
```cpp
struct FontState {
    FontSeries series;  // medium, bold
    FontShape shape;    // upright, italic, slanted
    FontFamily family;  // roman, sansserif, typewriter
    FontSize size;      // normalsize, large, small, etc.
};
```

**MarkBuilder** (parser):
- `createMap()` / `MapBuilder` - creates element with attributes
- `createList()` / `ListBuilder` - creates ordered children
- `createString()` - creates text content

### Character Encoding

Both stages handle UTF-8 throughout:
- Parser converts TeX escapes to Unicode (e.g., `\'e` → `é` U+00E9)
- Formatter outputs UTF-8 HTML (e.g., thin space → U+2009)
- Special Unicode markers: ZWSP (U+200B) for word boundaries, visible space (U+2423) for `\verb*`

---

## Test Infrastructure

### Test Suite Organization

The tests were reorganized from the original LaTeX.js 75-test suite to a more focused structure:

| Test Suite | Purpose | Tests |
|------------|---------|-------|
| **Baseline** (`test_latex_html_baseline.cpp`) | Core functionality that must pass | 33 tests |
| **Extended** (`test_latex_html_extended.cpp`) | Advanced features (counters, macros, etc.) | Planned |

### Fixture Files (`test/latex/fixtures/`)

Each `.tex` file contains multiple test cases in LaTeX.js format:

| Fixture File | Description | Baseline Tests |
|--------------|-------------|----------------|
| `text.tex` | Text formatting, typography, verbatim | `text_tex_1` - `text_tex_10` |
| `environments.tex` | Lists, quotes, alignment environments | `environments_tex_1` - `environments_tex_12` |
| `sectioning.tex` | Chapters, sections, headings | `sectioning_tex_1` - `sectioning_tex_3` |
| `fonts.tex` | Font commands and declarations | `fonts_tex_1` - `fonts_tex_4` |
| `spacing.tex` | Horizontal and vertical spacing | `spacing_tex_1` - `spacing_tex_4` |

### Fixture Format

```
** test description
.
\LaTeX{} source code
.
<expected HTML output>
.
```

Special prefixes:
- `!` - Skip test (disabled)
- `s` - Screenshot test (visual validation)

### Test Naming Convention

Test names follow the pattern: `{fixture}_{N}` where:
- `{fixture}` = fixture filename without extension (e.g., `text_tex`)
- `{N}` = test number within that fixture (1-indexed)

Example: `text_tex_4` = 4th test in `text.tex`

### Running Tests

```bash
# Run all baseline tests
./test/test_latex_html_baseline.exe

# Run specific test
./test/test_latex_html_baseline.exe --gtest_filter='*text_tex_4*'

# Show only failures
./test/test_latex_html_baseline.exe 2>&1 | grep 'FAILED.*tex_'
```

### Reorganization from LaTeX.js

The original LaTeX.js test suite had ~75 tests across many categories. We reorganized to:

1. **Consolidated baseline tests** (33 tests) - Core text, environments, sectioning
2. **Deferred advanced tests** - Counters, macros, labels/refs moved to extended suite
3. **Removed duplicates** - Overlapping tests merged
4. **Skipped unsupported** - Math, picture environments (`.tex.skip` files)

This provides faster feedback during development while maintaining comprehensive coverage.

---

## Phase 2 Progress Log

### Completed Fixes

#### text_tex_4 - UTF-8 Text, Punctuation, TeX Symbols
- **Smart quotes**: Added `` ` `` → U+2018 (') and `'` → U+2019 (') conversion in parser
- **Thin space**: Fixed `\,` and `\thinspace` to output U+2009 instead of regular space
- **Inverted punctuation**: Fixed `!´`/`?´` ligature detection (3-byte UTF-8)
- **Diacritics over dotless letters**: Added `\"\i` → ï, `\"\j` handling for unbraced forms
- **Symbol commands**: Made `\ss`, `\o`, `\i`, `\j`, `\ae`, `\oe`, `\AA`, `\AE`, `\OE`, `\O` return early without consuming trailing `{}`
- **Empty groups**: Changed `{}` to output ZWSP (U+200B) for word boundary preservation

#### text_tex_7 - Ligatures and mbox
- **`\mbox`**: Added support with `<span class="hbox"><span>...</span></span>` structure

#### text_tex_8 - Verbatim Text
- **`\verb*`**: Added visible space handling (convert spaces to U+2423 ␣)
- **Class fix**: Changed verbatim span class from `latex-verbatim` to `tt`
- **Whitespace**: Preserved trailing space after `\verb` elements

#### environments_tex_1 - Empty Environments
- **`empty` environment**: Now outputs content followed by ZWSP
- **Whitespace preservation**: Fixed space after `\end{env}` not being consumed

---

## Missing Features Analysis

### Category 1: Counter System (HIGH PRIORITY)
**Status**: Not implemented
**Failing Tests**: `counters_tex_1`, `counters_tex_2`

LaTeX.js implements a full counter system with:
- `\newcounter{name}[parent]` - create counter with optional parent
- `\setcounter{name}{value}` - set counter value
- `\stepcounter{name}` - increment and reset children
- `\addtocounter{name}{value}` - add to counter
- Counter formatters: `\arabic{}`, `\roman{}`, `\Roman{}`, `\alph{}`, `\Alph{}`, `\fnsymbol{}`
- `\the<counter>` - display counter value
- `\value{counter}` - get numeric value

**Required Changes**:
1. Add counter state management in `format-latex-html.cpp`
2. Implement counter commands in AST processor
3. Support arithmetic expressions (e.g., `3*\real{1.6}`)

### Category 2: Macro System (HIGH PRIORITY)
**Status**: Not implemented
**Failing Tests**: `macros_tex_1` through `macros_tex_6`

LaTeX.js supports:
- Custom macro definitions via `\newcommand`, `\renewcommand`
- Package loading (`\usepackage{}`)
- Macro expansion with arguments (mandatory `{}` and optional `[]`)
- Special commands like `\empty`, `\relax`

**Required Changes**:
1. Add macro definition storage and expansion
2. Implement `\documentclass{}` and `\usepackage{}` processing
3. Support `\begin{document}...\end{document}` structure

### Category 3: Text & Typography (MEDIUM PRIORITY)
**Status**: Partially implemented
**Failing Tests**: `text_tex_2` through `text_tex_10`, `fonts_tex_2` through `fonts_tex_8`

Missing features:
- Ligatures: `ff`, `ffi`, `ffl`, `fi`, `fl` → Unicode ligature characters
- TeX/LaTeX logos: `\TeX`, `\LaTeX` with proper kerning
- Hyphenation: `\-` (discretionary hyphen)
- Diacritics: `\^{o}` → ô, `\"a` → ä, etc.
- Special characters: proper handling of `\$`, `\#`, `\&`, `\%`, `\_`, `\{`, `\}`
- Verbatim: `\verb|...|` and `\verb*|...|` (visible spaces)
- Text alignment: `\centering`, `\raggedright`, `\raggedleft` as declarations

**Required Changes**:
1. Add ligature conversion table in text processing
2. Implement TeX/LaTeX logo generation (special span structures)
3. Add diacritic combining characters support
4. Fix verbatim inline command handling

### Category 4: Spacing (MEDIUM PRIORITY)
**Status**: Partially implemented
**Failing Tests**: `spacing_tex_1`, `spacing_tex_3`, `spacing_tex_4`

Missing features:
- Horizontal spaces: `\negthinspace`, `\thinspace`, `\,`, `\enspace`, `\quad`, `\qquad`, `\hspace{}`
- Vertical spaces: `\vspace{}` (inline vs block mode)
- Break commands: `\smallbreak`, `\medbreak`, `\bigbreak`
- Line dimension conversions (cm, mm, pt, em, ex to CSS)

**Required Changes**:
1. Proper CSS margin-right for horizontal spaces
2. Distinguish inline vs block vertical space handling
3. Complete dimension unit conversions

### Category 5: Boxes (MEDIUM PRIORITY)
**Status**: Not implemented
**Failing Tests**: `boxes_tex_2` through `boxes_tex_5`

Missing features:
- `\parbox[pos][height][inner-pos]{width}{content}`
- `\mbox{content}` (horizontal box, prevents line breaks)
- `\fbox{content}` (framed box)
- `minipage` environment
- `\llap{}`, `\rlap{}` (overlapping boxes)
- `\hbox{}` (TeX primitive)

**Required Changes**:
1. Add box command processing
2. Generate appropriate CSS flexbox/inline-block structures
3. Handle alignment parameters

### Category 6: Label/Reference System (MEDIUM PRIORITY)
**Status**: Not implemented
**Failing Tests**: `label_ref_tex_1` through `label_ref_tex_7`

Missing features:
- `\label{name}` - mark location
- `\ref{name}` - reference to label (displays counter)
- `\currentlabel` tracking
- Forward reference resolution

**Required Changes**:
1. Two-pass processing or deferred resolution
2. Store label → counter value mapping
3. Generate anchor links and references

### Category 7: Groups & Scoping (MEDIUM PRIORITY)
**Status**: Partially implemented
**Failing Tests**: `groups_tex_1` through `groups_tex_3`

Missing features:
- Proper `{}` group scoping for font changes
- Font state inheritance across groups
- Zero-width break space after groups (`\u200B`)
- `\par` handling within groups

**Required Changes**:
1. Track font state stack during group processing
2. Add proper spacing markers between groups

### Category 8: Whitespace Handling (MEDIUM PRIORITY)
**Status**: Partially implemented
**Failing Tests**: Multiple `whitespace_tex_*` tests

Missing features:
- Comment stripping (`%` lines)
- Multiple newlines → paragraph break
- Space collapsing within paragraphs
- `\unskip` and `\ignorespaces` handling
- Control space `\ ` and tie `~`

**Required Changes**:
1. Pre-process LaTeX to handle comments
2. Implement proper whitespace normalization
3. Handle `\par` vs empty line equivalence

### Category 9: Symbols & Characters (LOW PRIORITY)
**Status**: Partially implemented
**Failing Tests**: `symbols_tex_1` through `symbols_tex_4`

Missing features:
- `\char98` (TeX character by code)
- `^^A0`, `^^^^2103` (TeX hex notation)
- `\symbol{"00A9}` (LaTeX symbol by code)
- Predefined symbols: `\textellipsis`, `\textbullet`, `\textendash`, etc.

**Required Changes**:
1. Add symbol lookup table (from latex-js symbols.ls)
2. Parse character code specifications

### Category 10: Preamble & Document Structure (LOW PRIORITY)
**Status**: Partially implemented
**Failing Tests**: `preamble_tex_1`

Missing features:
- `\documentclass[options]{class}` handling
- Package options processing
- Preamble vs document body separation
- `\maketitle` with proper document state

**Required Changes**:
1. Track document class and options
2. Process preamble commands before body

### Category 11: Layout/Marginpar (LOW PRIORITY)
**Status**: Not implemented
**Failing Tests**: `layout_marginpar_tex_1` through `layout_marginpar_tex_3`

Missing features:
- `\marginpar{text}` - margin notes
- Layout length definitions
- Two-column support

**Required Changes**:
1. Add margin container structure
2. Implement CSS float/margin positioning

---

## Implementation Priority

### Phase 1: Core Text Processing (Week 1-2)
Target: +20 test passes

1. **Ligatures**: Add conversion table for ff, fi, fl, ffi, ffl → Unicode
2. **Diacritics**: Implement combining characters for `\'`, `\"`, `\^`, etc.
3. **TeX/LaTeX logos**: Generate proper span structure
4. **Special characters**: Proper escaping for `\$`, `\#`, etc.
5. **Whitespace normalization**: Comment stripping, space collapsing

### Phase 2: Font & Group Handling (Week 3)
Target: +15 test passes

1. **Font state stack**: Track font family/weight/shape through groups
2. **Group scoping**: Proper `{}` boundaries with state save/restore
3. **Font declarations vs commands**: `\bfseries` vs `\textbf{}`
4. **`\em` toggling**: Italic in roman, upright in italic

### Phase 3: Spacing & Boxes (Week 4)
Target: +10 test passes

1. **Horizontal spaces**: All space commands with proper CSS
2. **Vertical spaces**: Inline vs block mode handling
3. **Basic boxes**: `\mbox{}`, `\fbox{}`, `\hbox{}`
4. **`\parbox`**: Alignment parameters

### Phase 4: Counter & Reference System (Week 5-6)
Target: +10 test passes

1. **Counter state**: Create, set, step, format
2. **Label storage**: Map labels to current counter state
3. **Reference resolution**: Lookup and link generation
4. **Section numbering**: Automatic numbering with counters

### Phase 5: Advanced Features (Week 7-8)
Target: +10 test passes

1. **Symbol table**: Complete Unicode symbol mappings
2. **Macro expansion**: Basic `\newcommand` support
3. **Preamble processing**: Document class handling
4. **Marginpar**: Basic margin note support

---

## Code Changes Summary

### Files to Modify

1. **`lambda/format/format-latex-html.cpp`**
   - Add counter state management
   - Implement font state stack
   - Add ligature/symbol tables
   - Improve whitespace handling
   - Add box processing

2. **`lambda/format/format-latex-html.h`**
   - Add state structures for counters, labels, fonts

3. **New file: `lambda/format/latex-symbols.cpp`**
   - Unicode symbol mappings from latex-js symbols.ls

4. **New file: `lambda/format/latex-macros.cpp`**
   - Macro expansion engine

### Key Data Structures to Add

```cpp
// Counter management
struct LatexCounter {
    int value;
    std::string name;
    std::vector<std::string> reset_children;
};

// Label/reference system
struct LatexLabel {
    std::string id;
    std::string counter_value;
    std::string anchor_id;
};

// Font state (already partially exists)
struct FontState {
    FontSeries series;
    FontShape shape;
    FontFamily family;
    FontSize size;
    bool em_active;
};

// Group stack
struct GroupState {
    FontState font;
    std::string alignment;
    std::map<std::string, int> counters_snapshot;
};
```

---

## Success Metrics

| Phase | Target Pass Rate | Tests Passing | Status |
|-------|-----------------|---------------|--------|
| Initial | 13% | 10/75 | ✅ Complete |
| Phase 1 | 40% | ~13/33 | ✅ Complete |
| **Phase 2** | **55%** | **18/33** | **In Progress** |
| Phase 3 | 75% | 25/33 | Planned |
| Phase 4 | 88% | 29/33 | Planned |
| Phase 5 | 95%+ | 31+/33 | Planned |

*Note: Test suite was reorganized from 75 tests to 33 baseline + extended tests*

---

## Notes

- LaTeX.js uses LiveScript (compiles to JavaScript)
- Key files to reference: `html-generator.ls`, `generator.ls`, `latex.ltx.ls`, `symbols.ls`
- Test fixtures use LaTeX.js format: `** header\n.\nlatex\n.\nhtml\n.`
- HTML comparison is whitespace-normalized, so exact spacing isn't critical
- Focus on semantic correctness first, then CSS styling refinement
