# LaTeX Typesetting Library - Design Proposal

## Executive Summary

This document describes the LaTeX typesetting library for Lambda. The goal is to produce publication-quality typeset output comparable to TeX/LaTeX, validated against DVI reference files.

**Status**: All core phases complete (1-5). **173 tests passing.** Legacy code removed. Hyphenation implemented.

## Implementation Status

### Completed Components

| File | Status | Purpose |
|------|--------|---------|
| `tex_node.hpp/cpp` | ✅ Complete | Unified node system (30+ node types) |
| `tex_glue.hpp` | ✅ Complete | Glue/spacing primitives (TeXBook Ch 12) |
| `tex_tfm.hpp/cpp` | ✅ Complete | TFM font parser with built-in fallbacks |
| `tex_hlist.hpp/cpp` | ✅ Complete | HList builder with ligatures/kerning |
| `tex_linebreak.hpp/cpp` | ✅ Complete | Knuth-Plass optimal line breaking |
| `tex_vlist.hpp/cpp` | ✅ Complete | VList builder with baseline skip |
| `tex_pagebreak.hpp/cpp` | ✅ Complete | Optimal page breaking |
| `tex_font_metrics.hpp` | ✅ Complete | Font metric structures (TFM-compatible) |
| `tex_math_bridge.hpp/cpp` | ✅ Complete | Math bridge (inline/display math, fractions, radicals, scripts) |
| `tex_hyphen.hpp/cpp` | ✅ Complete | Liang's hyphenation algorithm (TeXBook Appendix H) |
| `dvi_parser.hpp/cpp` | ✅ Complete | DVI parsing for validation |

### Legacy Components (REMOVED)

The following legacy files were removed after Phase 4 completion:

| File | Status | Notes |
|------|--------|-------|
| `tex_box.hpp/cpp` | 🗑️ Removed | Superseded by tex_node.hpp |
| `tex_ast.hpp` | 🗑️ Removed | Superseded by tex_node.hpp |
| `tex_ast_builder.hpp/cpp` | 🗑️ Removed | Was broken, used undefined types |
| `tex_typeset.hpp/cpp` | 🗑️ Removed | Was broken |
| `tex_output.hpp/cpp` | 🗑️ Removed | Was broken, used wrong types |
| `tex_paragraph.hpp/cpp` | 🗑️ Removed | Superseded by tex_linebreak |
| `tex_math_layout.hpp/cpp` | 🗑️ Removed | Legacy math implementation |
| `tex_radiant_bridge.hpp/cpp` | 🗑️ Removed | Legacy bridge |
| `tex_radiant_font.hpp/cpp` | 🗑️ Removed | Legacy font bridge |
| `tex_simple_math.hpp/cpp` | 🗑️ Removed | Superseded by tex_math_bridge |

### Pending Components

| File | Status | Purpose |
|------|--------|---------|
| `tex_dvi_out.hpp/cpp` | ✅ Complete | DVI output generation (Phase 5) |
| `tex_pdf_out.hpp/cpp` | ✅ Complete | PDF output generation (Phase 5) |

---

## Architecture

### Implemented Layer Structure

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 1: Core Primitives (✅ COMPLETE)                       │
├─────────────────────────────────────────────────────────────┤
│ tex_glue.hpp    - Glue/spacing with infinite orders         │
│ tex_node.hpp    - Unified node system (30+ types)           │
│ tex_node.cpp    - Node factory functions, tree traversal    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Layer 2: Font System (✅ COMPLETE)                           │
├─────────────────────────────────────────────────────────────┤
│ tex_tfm.hpp/cpp - TFM parser + built-in fallbacks           │
│                   (cmr10, cmmi10, cmsy10, cmex10)           │
│ tex_font_metrics.hpp - Font metric structures               │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Layer 3: Horizontal Lists (✅ COMPLETE)                      │
├─────────────────────────────────────────────────────────────┤
│ tex_hlist.hpp/cpp - Text to HList conversion                │
│                     Ligatures (fi, fl, ff, ffi, ffl)        │
│                     Kerning, inter-word glue                │
│ tex_linebreak.hpp/cpp - Knuth-Plass optimal line breaking   │
│                         Badness, demerits, fitness classes  │
│                         Two-pass (pretolerance/tolerance)   │
│                         Parfillskip for short last lines    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Layer 4: Vertical Lists (✅ COMPLETE - Phase 3)             │
├─────────────────────────────────────────────────────────────┤
│ tex_vlist.hpp/cpp - VList builder with:                     │
│                     • Baseline skip calculation             │
│                     • Inter-paragraph spacing (parskip)     │
│                     • Section headings                      │
│                     • Display math insertion                │
│                     • Centered/right-aligned lines          │
│ tex_pagebreak.hpp/cpp - Optimal page breaking with:         │
│                     • Page height constraints               │
│                     • Widow/orphan penalties                │
│                     • Forced page breaks                    │
│                     • Page content extraction               │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ Layer 5: Output (✅ COMPLETE - Phase 5)                     │
├─────────────────────────────────────────────────────────────┤
│ tex_dvi_out.hpp/cpp - DVI generation (TeXBook Appendix A)   │
│ tex_pdf_out.hpp/cpp - PDF generation (via pdf_writer)       │
└─────────────────────────────────────────────────────────────┘
```

---

## Font System with Built-in Fallbacks

### TFM Loading Strategy

The font system (`tex_tfm.cpp`) uses a three-tier approach:

```
┌─────────────────────────────────────────────────────────────┐
│                    Font Loading Flow                        │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. Try local file: ./cmr10.tfm                            │
│           │                                                 │
│           ▼ (if not found)                                  │
│  2. Search TeX Live paths:                                  │
│     • /usr/share/texmf/fonts/tfm/public/cm/                │
│     • /usr/share/texlive/texmf-dist/fonts/tfm/public/cm/   │
│     • /opt/homebrew/share/texmf-dist/fonts/tfm/public/cm/  │
│     • /usr/local/texlive/texmf-dist/fonts/tfm/public/cm/   │
│     • ~/.texlive/texmf-dist/fonts/tfm/public/cm/           │
│           │                                                 │
│           ▼ (if not found - logs ERR but continues)         │
│  3. Use built-in fallback metrics                           │
│     • get_builtin_cmr10()  - Roman text                    │
│     • get_builtin_cmmi10() - Math italic                   │
│     • get_builtin_cmsy10() - Math symbols                  │
│     • get_builtin_cmex10() - Math extensions               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Built-in Font Metrics

The built-in fallbacks provide accurate metrics without requiring TeX Live installation:

```cpp
// Example: CMR10 built-in metrics (tex_tfm.cpp)
TFMFont* get_builtin_cmr10(Arena* arena) {
    // Character widths (in points at 10pt)
    static const float CMR10_WIDTHS[128] = {
        [' '] = 3.33,   // space
        ['a'] = 5.00,   ['b'] = 5.56,   ['c'] = 4.44,
        ['A'] = 7.50,   ['B'] = 7.08,   ['C'] = 6.81,
        // ... full table for all 128 characters
    };

    // Font parameters
    font->params[TFM_PARAM_SPACE] = 3.33;         // inter-word space
    font->params[TFM_PARAM_SPACE_STRETCH] = 1.67; // space stretch
    font->params[TFM_PARAM_SPACE_SHRINK] = 1.11;  // space shrink
    font->params[TFM_PARAM_X_HEIGHT] = 4.31;      // x-height
    font->params[TFM_PARAM_QUAD] = 10.0;          // 1em width
    // ...
}
```

### Error Messages Explained

When you see these messages, they are **informational, not errors**:

```
[ERR!] tex_tfm: cannot open cmr10.tfm
[ERR!] tex_tfm: cannot open /usr/share/texmf/fonts/tfm/public/cm/cmr10.tfm
...
```

These indicate that external TFM files weren't found, but the code **successfully falls back** to built-in metrics. Tests pass because the built-in data is accurate.

To suppress these messages:
1. Install TeX Live: `brew install texlive` (macOS) or `apt install texlive` (Linux)
2. Or: the log level could be changed from ERR to DEBUG (optional)

---

## Implemented Node System

### NodeClass Enum (tex_node.hpp)

```cpp
enum class NodeClass : uint8_t {
    // Characters
    Char,           // Single character glyph
    Ligature,       // fi, fl, ff, ffi, ffl

    // Lists/Boxes
    HList,          // Horizontal list (paragraph content)
    VList,          // Vertical list (page content)
    HBox,           // \hbox{...} with set width
    VBox,           // \vbox{...} with set height
    VTop,           // \vtop{...} aligned at top

    // Spacing
    Glue,           // Stretchable/shrinkable space
    Kern,           // Fixed space
    Penalty,        // Break control

    // Rules
    Rule,           // Filled rectangle

    // Math
    MathList, MathChar, MathOp, Fraction,
    Radical, Delimiter, Accent, Scripts,

    // Structure
    Paragraph, Page,

    // Special
    Mark, Insert, Adjust, Whatsit, Disc, Error
};
```

### TexNode Structure

```cpp
struct TexNode {
    NodeClass node_class;
    uint8_t flags;

    // Dimensions (set during layout)
    float width, height, depth;
    float shift;                // Vertical shift for subscripts/superscripts

    // Position (set during output)
    float x, y;

    // Tree structure
    TexNode *parent;
    TexNode *first_child, *last_child;
    TexNode *prev_sibling, *next_sibling;

    // Content (discriminated union by node_class)
    union {
        CharContent char_content;
        GlueContent glue;
        KernContent kern;
        PenaltyContent penalty;
        // ... etc
    } content;

    // Tree manipulation
    void append_child(TexNode* child);
    void prepend_child(TexNode* child);
    void insert_after(TexNode* sibling);
    void remove_from_parent();
};
```

---

## Line Breaking Algorithm

### Knuth-Plass Implementation (tex_linebreak.cpp)

The line breaking uses the optimal algorithm from TeXBook Chapter 14:

```
┌─────────────────────────────────────────────────────────────┐
│                 Knuth-Plass Algorithm                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Input: HList of characters, glue, penalties                │
│                                                             │
│  1. Find all legal breakpoints (after glue, at penalties)   │
│                                                             │
│  2. First pass (pretolerance = 100):                        │
│     For each breakpoint:                                    │
│       For each active node:                                 │
│         Compute line width, stretch, shrink                 │
│         Compute badness = 100 × (excess/glue)³              │
│         If badness ≤ threshold or forced break:             │
│           Compute demerits, create new active node          │
│         If overfull: deactivate node                        │
│                                                             │
│  3. Second pass (tolerance = 200) if no solution            │
│                                                             │
│  4. Emergency pass (threshold = 10000) if still no solution │
│                                                             │
│  5. Extract optimal path via passive node backpointers      │
│                                                             │
│  Output: Array of breakpoints, total demerits               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Key Features

- **Fitness Classes**: Tight, Normal, Loose, VeryLoose
- **Demerits**: Penalize bad breaks, fitness mismatches, consecutive hyphens
- **Parfillskip**: Infinite stretch on last line allows short endings
- **Parshape**: Support for non-rectangular paragraphs
- **Hanging Indent**: Support for hang_indent/hang_after

### API

```cpp
// Parameters with TeX defaults
LineBreakParams params = LineBreakParams::defaults();
params.hsize = 300.0f;       // Line width
params.tolerance = 200.0f;    // Badness threshold

// Break paragraph into lines
LineBreakResult result = break_paragraph(hlist, params, arena);
// result.line_count, result.breaks[], result.total_demerits

// Full typesetting: break + build lines + VList
TexNode* vlist = typeset_paragraph(hlist, params, baseline_skip, arena);
```

---

## Implementation Plan

### Phase 1: Foundation ✅ COMPLETE

**Goal**: Establish working primitives and font system

#### 1.1 Unify Node System ✅
- [x] Create `tex_node.hpp` with clean `NodeClass` enum (30+ node types)
- [x] Define `TexNode` struct with proper union
- [x] Factory functions for each node type
- [x] Tree manipulation (append_child, prepend_child, insert_after, remove)

#### 1.2 TFM Font System ✅
- [x] Implement TFM binary parser in `tex_tfm.cpp`
- [x] Built-in fallback metrics for CMR10, CMMI10, CMSY10, CMEX10
- [x] Character width/height/depth queries
- [x] Font parameter access (space, stretch, shrink, x-height, quad)
- [x] TFMFontManager for font caching

#### 1.3 Tests ✅
- [x] 25 tests for node creation, tree operations, font metrics

**Deliverable**: Working font metrics, character measurement ✅

### Phase 2: Horizontal Lists ✅ COMPLETE

**Goal**: Build and process horizontal lists (paragraph content)

#### 2.1 HList Builder ✅
- [x] Convert parsed text to HList nodes (`text_to_hlist`)
- [x] Handle text characters with font selection
- [x] Apply ligatures (fi, fl, ff, ffi, ffl)
- [x] Apply kerning between characters
- [x] Insert inter-word glue
- [x] Measure HList dimensions
- [x] Set glue to target width

#### 2.2 Knuth-Plass Line Breaking ✅
- [x] Active/passive node lists
- [x] Compute badness (100 × ratio³)
- [x] Compute demerits with penalties
- [x] Fitness classes (Tight, Normal, Loose, VeryLoose)
- [x] Two-pass algorithm (pretolerance/tolerance)
- [x] Emergency pass for difficult cases
- [x] Parfillskip for last line
- [x] Parshape support (non-rectangular paragraphs)
- [x] Hanging indent support

#### 2.3 Tests ✅
- [x] 12 tests for line breaking, badness, demerits, fitness

**Deliverable**: Paragraphs broken into lines ✅ (37 tests passing)

### Phase 3: Vertical Lists ✅ COMPLETE

**Goal**: Build pages from lines

#### 3.1 VList Builder ✅
- [x] Stack lines with baseline skip
- [x] Inter-paragraph spacing (parskip)
- [x] Section headings (levels 1-3)
- [x] Display math insertion
- [x] Centering and alignment utilities
- [x] VList measurement and glue setting

#### 3.2 Page Breaking ✅
- [x] Optimal page breaking algorithm
- [x] Page height constraints
- [x] Penalty handling (widows, orphans)
- [x] Forced page breaks (\newpage)
- [x] Page content extraction
- [x] Mark handling for headers/footers

#### 3.3 Tests ✅
- [x] 25 tests for VList builder and page breaking

**Deliverable**: Multi-page documents ✅ (62 tests total)

### Phase 4: Math Integration ✅ COMPLETE

**Goal**: Integrate math typesetting with text flow

#### 4.1 Math Bridge ✅
- [x] Create `tex_math_bridge.hpp/cpp`
- [x] MathStyle enum (Display, Text, Script, Scriptscript + cramped variants)
- [x] Style transitions (script_style, cramped_style)
- [x] Size factors (Display/Text=1.0, Script=0.7, Scriptscript=0.5)
- [x] MathContext for typesetting state

#### 4.2 Atom Classification & Spacing ✅
- [x] AtomType classification (Ord, Op, Bin, Rel, Open, Close, Punct, Inner)
- [x] Unicode codepoint classifier
- [x] Inter-atom spacing tables (TeXBook Chapter 18)
- [x] Tight spacing for script styles

#### 4.3 Simple Math Typesetter ✅
- [x] `typeset_math_string()` - parse and typeset math expressions
- [x] Font selection (roman for digits, italic for variables, symbol for operators)
- [x] Character metrics via TFM

#### 4.4 Fractions ✅
- [x] `typeset_fraction()` - numerator/denominator layout
- [x] Axis alignment
- [x] Horizontal centering
- [x] Rule thickness

#### 4.5 Radicals ✅
- [x] `typeset_sqrt()` - square root layout
- [x] `typeset_root()` - nth root with degree
- [x] Clearance above content
- [x] Rule thickness

#### 4.6 Scripts ✅
- [x] `typeset_scripts()` - subscript/superscript layout
- [x] Vertical positioning
- [x] Italic correction
- [x] Concurrent sub/superscripts

#### 4.7 Delimiters ✅
- [x] `typeset_delimited()` - parentheses, brackets, braces
- [x] Match delimiter height to content

#### 4.8 Inline Math ✅
- [x] `find_math_regions()` - detect `$...$` in text
- [x] `process_text_with_math()` - combine text and math nodes
- [x] Baseline alignment

#### 4.9 Display Math ✅
- [x] Detect `$$...$$` and `\[...\]`
- [x] `typeset_display_math()` - centered display equations
- [x] Proper spacing above/below

#### 4.10 Lambda Item Conversion (Conditional) ✅
- [x] `convert_lambda_math()` - convert Lambda math nodes to TeX nodes
- [x] Support for Symbol, Number, Row, Fraction, Radical, Subsup node types
- [x] Guarded by TEX_WITH_LAMBDA preprocessor flag

#### 4.11 Tests ✅
- [x] 41 tests for math bridge functionality

**Deliverable**: Documents with inline and display math ✅ (140 tests total)

### Phase 5: Output Backends ✅ COMPLETE

**Goal**: Generate actual output files

#### 5.1 DVI Output ✅
- [x] DVIWriter context with position tracking (h, v, w, x, y, z)
- [x] DVI preamble/postamble generation (TeXBook Appendix A)
- [x] Font definition and selection
- [x] Character output (set_char, set1-4 opcodes)
- [x] Rule output for horizontal/vertical rules
- [x] Movement commands (right, down, w, x, y, z)
- [x] Stack operations (push/pop)
- [x] Page handling (bop/eop)
- [x] HList and VList traversal
- [x] Round-trip validation via DVIParser

#### 5.2 PDF Output ✅
- [x] PDFWriter using existing pdf_writer.h API
- [x] Font mapping (CM → PDF Base14: cmr→Times-Roman, cmss→Helvetica, cmtt→Courier)
- [x] Coordinate system conversion (TeX top-left → PDF bottom-left)
- [x] Character and text output
- [x] Rule drawing
- [x] Multi-page document support
- [x] HList and VList traversal

#### 5.3 Tests ✅
- [x] 37 tests for DVI/PDF output

**Deliverable**: DVI and PDF documents ✅ (99 tests total)

---

## Current File Structure

```
lambda/tex/
├── tex_node.hpp/cpp          # ✅ Unified node system (30+ node types)
├── tex_glue.hpp              # ✅ Glue/spacing primitives
├── tex_tfm.hpp/cpp           # ✅ TFM parser + built-in fallbacks
├── tex_hlist.hpp/cpp         # ✅ HList builder (ligatures, kerning)
├── tex_linebreak.hpp/cpp     # ✅ Knuth-Plass line breaking
├── tex_vlist.hpp/cpp         # ✅ VList builder
├── tex_pagebreak.hpp/cpp     # ✅ Optimal page breaking
├── tex_math_bridge.hpp/cpp   # ✅ Math integration (Phase 4)
├── tex_dvi_out.hpp/cpp       # ✅ DVI output generation
├── tex_pdf_out.hpp/cpp       # ✅ PDF output generation
├── tex_font_metrics.hpp      # ✅ Font metric structures
└── dvi_parser.hpp/cpp        # ✅ DVI validation tool
```

**22 files total** (11 .hpp + 11 .cpp) - clean, focused implementation.

### Test Files

```
test/
├── test_tex_node_gtest.cpp   # ✅ 37 tests (Phase 1 & 2)
├── test_tex_vlist_gtest.cpp  # ✅ 25 tests (Phase 3)
├── test_tex_phase4_gtest.cpp # ✅ 41 tests (Phase 4 - Math integration)
└── test_tex_phase5_gtest.cpp # ✅ 37 tests (Phase 5 - DVI/PDF output)
```

---

## Testing Strategy

### Unit Tests
- Font metrics loading
- Glue calculations
- Line breaking on known inputs
- Math spacing

### Integration Tests
- Full document typesetting
- Compare against DVI references

### Reference Validation
1. Create `.tex` files with known content
2. Run through TeX to get `.dvi`
3. Parse DVI for reference positions
4. Compare Lambda output against reference

### Test Files Needed
```
test/tex/
├── simple_paragraph.tex      # Basic text
├── linebreak_edge.tex        # Edge cases for breaking
├── hyphenation.tex           # Hyphenation patterns
├── inline_math.tex           # $a+b$
├── display_math.tex          # $$\int_0^1$$
├── fractions.tex             # Fraction positioning
├── multi_page.tex            # Page breaking
└── full_article.tex          # Complete document
```

---

## Dependencies

### Required
- Tree-sitter LaTeX parser (existing)
- FreeType (existing)
- Arena allocator (existing)

### Font Files
- Computer Modern TFM files (standard TeX distribution)
- Latin Modern OTF (open source, matches CM)

### Optional
- OpenType MATH table support (for modern math fonts)

---

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| TFM parsing complexity | Use existing metrics from tex_simple_math as fallback |
| Knuth-Plass edge cases | Start with greedy, add optimal incrementally |
| Font rendering quality | Use proven FreeType code from Radiant |
| Scope creep | Focus on article class first, defer others |

---

## Success Criteria

### Phase 1 ✅ ACHIEVED
- [x] Load CMR10 metrics (with built-in fallback)
- [x] Measure "Hello World" correctly
- [x] 25 unit tests passing

### Phase 2 ✅ ACHIEVED
- [x] Break paragraph into lines
- [x] Knuth-Plass optimal algorithm working
- [x] 37 total tests passing

### Phase 3 ✅ ACHIEVED
- [x] Generate multi-page document
- [x] Page breaks at correct positions
- [x] 25 tests for VList and page breaking

### Phase 4 ✅ ACHIEVED
- [x] Math bridge with all atom types and spacing
- [x] Inline math ($...$) integration
- [x] Display math ($$...$$) centering
- [x] Fractions, radicals, scripts
- [x] 41 tests passing

### Phase 5 ✅ ACHIEVED
- [x] DVI output with correct opcodes
- [x] PDF output opens in viewers
- [x] 37 tests for output backends
- [x] Round-trip DVI validation working

---

## Appendix A: Code Status

### Active Files (24 total)
- `tex_node.hpp/cpp` - Unified node system (30+ types) ✅
- `tex_glue.hpp` - Glue/spacing primitives ✅
- `tex_tfm.hpp/cpp` - TFM parser with built-in fallbacks ✅
- `tex_hlist.hpp/cpp` - HList builder (ligatures, kerning) ✅
- `tex_linebreak.hpp/cpp` - Knuth-Plass optimal line breaking ✅
- `tex_vlist.hpp/cpp` - VList builder ✅
- `tex_pagebreak.hpp/cpp` - Optimal page breaking ✅
- `tex_math_bridge.hpp/cpp` - Math integration (Phase 4) ✅
- `tex_hyphen.hpp/cpp` - Liang's hyphenation algorithm ✅
- `tex_dvi_out.hpp/cpp` - DVI output generation ✅
- `tex_pdf_out.hpp/cpp` - PDF output generation ✅
- `dvi_parser.hpp/cpp` - DVI validation tool ✅
- `tex_font_metrics.hpp` - Font metric structures ✅

### Removed Files (19 total)
The following legacy/broken files were removed after Phase 4 completion:
- `tex_ast.hpp` - Superseded by `tex_node.hpp`
- `tex_box.hpp/cpp` - Superseded by `tex_node.hpp`
- `tex_ast_builder.hpp/cpp` - Was broken
- `tex_paragraph.hpp/cpp` - Superseded by `tex_linebreak.hpp`
- `tex_typeset.hpp/cpp` - Was broken
- `tex_output.hpp/cpp` - Was broken
- `tex_math_layout.hpp/cpp` - Legacy math
- `tex_radiant_bridge.hpp/cpp` - Legacy bridge
- `tex_radiant_font.hpp/cpp` - Legacy font bridge
- `tex_simple_math.hpp/cpp` - Superseded by tex_math_bridge

---

## Appendix B: TeXBook Reference Chapters

| Chapter | Topic | Implementation File | Status |
|---------|-------|---------------------|--------|
| 12 | Glue | tex_glue.hpp | ✅ |
| 14 | Line breaking | tex_linebreak.cpp | ✅ |
| 15 | Page breaking | tex_pagebreak.cpp | ✅ |
| 17 | Math modes | tex_math_bridge.cpp | ✅ |
| 18 | Math spacing | tex_math_bridge.cpp | ✅ |
| Appendix A | DVI format | tex_dvi_out.cpp | ✅ |
| Appendix G | Math typesetting | tex_math_bridge.cpp | ✅ |
| Appendix H | Hyphenation | tex_hyphen.cpp | ✅ |

---

## Next Steps

1. ~~**Phase 4**: Bridge Radiant math layout to TexNode system~~ ✅ COMPLETE
2. ~~**Cleanup**: Remove legacy files after migration complete~~ ✅ COMPLETE (19 files removed)
3. ~~**Hyphenation**: Implement TeX hyphenation patterns~~ ✅ COMPLETE (Liang's algorithm)
4. **SVG Output**: Add SVG backend for web rendering (optional)
5. **Lambda Integration**: Connect TeX typesetter to Lambda document pipeline
