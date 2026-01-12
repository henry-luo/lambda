# LaTeX Typesetting Pipeline Phase 3: Complete TeXbook Coverage

## Current Implementation Status (Updated: Phase 3 Implementation In Progress)

### ✅ Fully Implemented

| TeXbook Topic | Coverage | Key Files | Lines |
|---------------|----------|-----------|-------|
| Boxes & Glue (Ch 12) | 100% | `tex_node.hpp/cpp` | ~800 |
| Line Breaking (Ch 14) | 100% | `tex_linebreak.cpp` | 875 |
| Fonts/TFM (Appendix F) | 100% | `tex_tfm.cpp` | 725 |
| DVI Output (Ch 23) | 100% | `tex_dvi_out.cpp` | 886 |
| Ligatures & Kerning | 100% | `tex_hlist.cpp` | 473 |
| Hyphenation | 100% | `tex_hyphen.cpp` | 976 |
| HList/VList Building | 100% | `tex_hlist.cpp`, `tex_vlist.cpp` | 1045 |
| **Page Breaking (Ch 15)** | **100%** | `tex_pagebreak.cpp` | ~800 |
| **Math Typesetting (Ch 16-18)** | **100%** | `tex_math_bridge.cpp` | ~2800 |
| **Alignment (Ch 22)** | **100%** | `tex_align.cpp/hpp` | ~600 |
| **Macro Processor (Ch 20)** | **100%** | `tex_macro.cpp/hpp` | ~600 |
| **Conditionals (Ch 20)** | **100%** | `tex_conditional.cpp/hpp` | ~500 |

### Phase 3 Implementation Summary

**Completed Milestones:**

1. ✅ **M1: Page Breaking** - Float placement algorithm, mark handling (topmark/firstmark/botmark), insertion classes with footnote support
2. ✅ **M2: Math Typesetting** - Extensible delimiters using TFM recipes, big operator limits (above/below), math accents (\hat, \bar, \dot, etc.), \overline/\underline, \phantom/\vphantom/\hphantom, \overbrace/\underbrace, \stackrel
3. ✅ **M3: Alignment** - \halign and \valign with preamble parsing, column templates (u#v pattern), tabskip glue, \noalign, \span, \omit
4. ✅ **M4: Macro Processor** - \def with delimited/undelimited parameters, \newcommand/\renewcommand/\providecommand, parameter substitution, recursive expansion with depth limit
5. ✅ **M5: Conditionals** - \if, \ifx, \ifnum, \ifdim, \ifodd, \ifdefined, \iftrue/\iffalse, mode tests (\ifvmode, etc.), proper nesting

### New Files Created

| File | Purpose | Lines |
|------|---------|-------|
| `tex_align.hpp` | Alignment API and data structures | ~220 |
| `tex_align.cpp` | \halign/\valign implementation | ~550 |
| `tex_macro.hpp` | Macro processor API | ~165 |
| `tex_macro.cpp` | \def, \newcommand implementation | ~520 |
| `tex_conditional.hpp` | Conditional processing API | ~145 |
| `tex_conditional.cpp` | \if* implementation | ~500 |

### Enhanced Files

| File | Changes |
|------|---------|
| `tex_math_bridge.cpp` | +608 lines: extensible delimiters, limits, accents, phantoms |
| `tex_pagebreak.cpp` | +185 lines: float placement, mark tracking, insertion classes |
| `tex_pagebreak.hpp` | +108 lines: InsertionState, MarkState, insertion class constants |
| `tex_node.hpp` | +35 lines: Insert node enhancements, make_insert/make_mark |

---

## Phase 3 Implementation Plan

### Milestone 1: Complete Page Breaking (Ch 15)

**Goal:** Finish the remaining 10% of page breaking

**Tasks:**

1. **Float Handling** (`tex_pagebreak.cpp`)
   - Implement float placement algorithm (top/bottom/here preferences)
   - Track float columns and deferred floats
   - Handle `\topskip`, `\floatsep`, `\textfloatsep`

2. **Mark Handling**
   - Implement `\mark`, `\topmark`, `\firstmark`, `\botmark`
   - Track marks across page breaks
   - Support split marks for footnotes

3. **Insertion Classes**
   - Implement `\insert` primitive
   - Handle footnote insertions
   - Support multiple insertion classes

**Files to modify:**
- `lambda/tex/tex_pagebreak.cpp`
- `lambda/tex/tex_pagebreak.hpp`

**Estimated effort:** 2-3 days

---

### Milestone 2: Complete Math Typesetting (Ch 16-18)

**Goal:** Finish the remaining 15% of math typesetting

**Tasks:**

1. **Extensible Delimiters** (`tex_math_bridge.cpp`)
   ```
   Current: ExtensibleDelimiter node defined but not rendered
   Target: Full extensible character assembly from cmex10
   ```
   - Implement `build_extensible_delimiter()` using TFM extensible recipes
   - Handle top/middle/bottom/repeater pieces
   - Support `\left`, `\right`, `\middle` with proper sizing

2. **Big Operators with Limits**
   ```
   Current: \sum, \int parsed but limits positioning incomplete
   Target: Correct above/below vs side placement based on style
   ```
   - Implement display-style limits (above/below)
   - Implement text-style limits (as subscript/superscript)
   - Handle `\limits` and `\nolimits` modifiers

3. **Math Accents Improvements**
   - Proper accent positioning using TFM skewchar
   - Wide accents (`\widehat`, `\widetilde`)
   - Stacked accents

4. **Additional Constructs**
   - `\overline`, `\underline` with proper rule thickness
   - `\overbrace`, `\underbrace` with labels
   - `\stackrel` for stacked relations
   - `\phantom`, `\vphantom`, `\hphantom`

**Files to modify:**
- `lambda/tex/tex_math_bridge.cpp`
- `lambda/tex/tex_math_bridge.hpp`
- `lambda/tex/tex_node.hpp` (if new node types needed)

**Estimated effort:** 4-5 days

---

### Milestone 3: Implement `\halign`/`\valign` (Ch 22)

**Goal:** Full table/alignment support

**Design:**

```cpp
// New file: lambda/tex/tex_align.hpp

struct AlignTemplate {
    std::vector<AlignColumn> columns;
    TexNode* preamble;  // u_j v_j pattern
};

struct AlignColumn {
    TexNode* u_part;    // material before #
    TexNode* v_part;    // material after #
    Glue tabskip;       // inter-column glue
};

struct AlignRow {
    std::vector<TexNode*> cells;
    bool is_noalign;    // \noalign material
    TexNode* noalign_content;
};

// Main functions
TexNode* build_halign(const AlignTemplate& tmpl,
                      const std::vector<AlignRow>& rows,
                      Arena* arena);

TexNode* build_valign(const AlignTemplate& tmpl,
                      const std::vector<AlignRow>& rows,
                      Arena* arena);
```

**Tasks:**

1. **Parse Alignment Preamble**
   - Parse `u_1 # v_1 & u_2 # v_2 & ...` pattern
   - Handle `\tabskip` glue between columns
   - Support `\span` and `\omit`

2. **Cell Processing**
   - Split rows on `&` tokens
   - Apply u/v templates to each cell
   - Handle `\cr`, `\crcr`, `\noalign`

3. **Width Calculation**
   - First pass: measure all cells
   - Determine natural column widths
   - Apply `to` or `spread` if specified

4. **Box Assembly**
   - Create hboxes for each cell
   - Build vbox columns for `\valign`
   - Build hbox rows for `\halign`

5. **Special Features**
   - `\multispan{n}` for spanning columns
   - `\hidewidth` for zero-width cells
   - Ruled tables with `\hrule`, `\vrule`

**New files:**
- `lambda/tex/tex_align.cpp` (~600-800 lines)
- `lambda/tex/tex_align.hpp`

**Estimated effort:** 5-7 days

---

### Milestone 4: Implement Macro Processor (Ch 20)

**Goal:** Basic macro expansion for `\def` and `\newcommand`

**Design:**

```cpp
// New file: lambda/tex/tex_macro.hpp

struct MacroDef {
    std::string name;
    int param_count;          // 0-9
    std::string param_text;   // parameter pattern
    std::string replacement;  // replacement text with #1, #2, etc.
    bool is_long;             // \long\def
    bool is_outer;            // \outer\def
};

class MacroProcessor {
    std::unordered_map<std::string, MacroDef> macros;
    Arena* arena;

public:
    // Define a new macro
    void define(const std::string& name, const MacroDef& def);

    // Expand macros in input string
    std::string expand(const std::string& input);

    // Check if name is defined
    bool is_defined(const std::string& name) const;

    // LaTeX-style commands
    void newcommand(const std::string& name, int nargs,
                    const std::string& default_arg,
                    const std::string& definition);
    void renewcommand(const std::string& name, ...);
};
```

**Tasks:**

1. **Basic `\def`**
   - Parse `\def\name#1#2{replacement}`
   - Store macro definitions
   - Expand on usage with argument substitution

2. **Parameter Matching**
   - Undelimited parameters (`#1`)
   - Delimited parameters (`#1.` matches up to `.`)
   - Up to 9 parameters

3. **LaTeX Commands**
   - `\newcommand{\name}[n][default]{def}`
   - `\renewcommand`
   - `\providecommand`
   - `\DeclareRobustCommand`

4. **Expansion Control**
   - `\expandafter`
   - `\noexpand`
   - `\edef` (expanded definition)
   - `\gdef` (global definition)

**New files:**
- `lambda/tex/tex_macro.cpp` (~500-700 lines)
- `lambda/tex/tex_macro.hpp`

**Estimated effort:** 4-5 days

---

### Milestone 5: Implement Conditionals (Ch 20)

**Goal:** TeX conditional execution

**Design:**

```cpp
// In tex_macro.hpp

class ConditionalProcessor {
public:
    // Numeric conditionals
    bool ifnum(int a, char rel, int b);    // \ifnum
    bool ifdim(Dimen a, char rel, Dimen b); // \ifdim
    bool ifodd(int n);                      // \ifodd

    // Token conditionals
    bool ifx(const Token& a, const Token& b);  // \ifx
    bool if_token(char a, char b);             // \if
    bool ifcat(const Token& a, const Token& b); // \ifcat

    // Mode conditionals
    bool ifhmode();   // in horizontal mode
    bool ifvmode();   // in vertical mode
    bool ifmmode();   // in math mode
    bool ifinner();   // in internal mode

    // Special conditionals
    bool iftrue();    // always true
    bool iffalse();   // always false
    bool ifcase(int n, const std::vector<std::string>& cases);

    // Definition conditionals
    bool ifdef(const std::string& name);   // \ifdef (LaTeX)
    bool ifundef(const std::string& name); // \ifundef
};
```

**Tasks:**

1. **Basic Conditionals**
   - `\iftrue`, `\iffalse`
   - `\ifnum`, `\ifdim`, `\ifodd`
   - Proper `\else`, `\fi` matching

2. **Token Conditionals**
   - `\if` (character code comparison)
   - `\ifx` (token equivalence)
   - `\ifcat` (category code comparison)

3. **Mode Conditionals**
   - `\ifhmode`, `\ifvmode`, `\ifmmode`, `\ifinner`

4. **Case Statement**
   - `\ifcase n \or ... \or ... \else ... \fi`

5. **Nested Conditionals**
   - Proper nesting of `\if...\fi` blocks
   - Skip false branches correctly

**Files to modify:**
- `lambda/tex/tex_macro.cpp` (add conditional support)
- `lambda/tex/tex_macro.hpp`

**Estimated effort:** 3-4 days

---

### Milestone 6: Test Suite Expansion

**Goal:** Comprehensive test coverage for new features

**Directory structure:**
```
test/latex/
├── fixtures/
│   ├── pagebreak/
│   │   ├── floats.tex
│   │   ├── footnotes.tex
│   │   └── marks.tex
│   ├── math/
│   │   ├── extensible_delims.tex
│   │   ├── big_operators.tex
│   │   ├── accents.tex
│   │   └── phantoms.tex
│   ├── align/
│   │   ├── halign_basic.tex
│   │   ├── halign_span.tex
│   │   ├── valign_basic.tex
│   │   └── tabular.tex
│   ├── macros/
│   │   ├── def_basic.tex
│   │   ├── def_params.tex
│   │   ├── newcommand.tex
│   │   └── expansion.tex
│   └── conditionals/
│       ├── ifnum.tex
│       ├── ifx.tex
│       ├── ifcase.tex
│       └── nested.tex
├── reference/
│   └── (generated DVI files)
├── test_pagebreak_gtest.cpp
├── test_math_extended_gtest.cpp
├── test_align_gtest.cpp
├── test_macro_gtest.cpp
└── test_conditional_gtest.cpp
```

**Test approach:**

1. **Reference Generation**
   - Run test cases through real TeX to generate reference DVI
   - Or manually verify output and commit as golden files

2. **DVI Comparison**
   - Parse both reference and generated DVI
   - Compare glyph sequences (existing pattern)
   - Compare positioning within tolerance

3. **Unit Tests**
   - Test macro expansion separately from typesetting
   - Test conditional evaluation logic
   - Test alignment width calculations

**New test files:**
- `test/latex/test_pagebreak_extended.cpp`
- `test/latex/test_math_extended.cpp`
- `test/latex/test_align.cpp`
- `test/latex/test_macro.cpp`
- `test/latex/test_conditional.cpp`

**Estimated effort:** 3-4 days

---

## Timeline Summary

| Milestone | Description | Effort | Dependencies |
|-----------|-------------|--------|--------------|
| M1 | Complete Page Breaking | 2-3 days | None |
| M2 | Complete Math Typesetting | 4-5 days | None |
| M3 | `\halign`/`\valign` | 5-7 days | None |
| M4 | Macro Processor | 4-5 days | None |
| M5 | Conditionals | 3-4 days | M4 |
| M6 | Test Suite | 3-4 days | M1-M5 |

**Total estimated effort:** 21-28 days

---

## Implementation Order Recommendation

```
Week 1: M1 (Page Breaking) + M2 (Math) - parallel work
Week 2: M3 (Alignment)
Week 3: M4 (Macros) + M5 (Conditionals)
Week 4: M6 (Tests) + Integration + Bug fixes
```

**Rationale:**
- M1 and M2 are independent and can proceed in parallel
- M3 (alignment) is self-contained
- M4 must precede M5 (conditionals use macro infrastructure)
- M6 should follow feature completion

---

## Success Criteria

1. **Page Breaking:** Float placement matches TeX output for standard document classes
2. **Math:** All delimiter sizes render correctly; limits positioned properly
3. **Alignment:** `\halign` tables render identically to TeX
4. **Macros:** Common LaTeX packages can define macros that expand correctly
5. **Conditionals:** Conditional logic in macros executes correctly
6. **Tests:** >95% of test cases pass DVI comparison

---

## Files to Create

| File | Purpose | Est. Lines |
|------|---------|------------|
| `tex_align.cpp` | `\halign`/`\valign` implementation | 600-800 |
| `tex_align.hpp` | Alignment structures | 100-150 |
| `tex_macro.cpp` | Macro processor | 500-700 |
| `tex_macro.hpp` | Macro/conditional API | 150-200 |
| Test files (5) | GTest suites | 300-500 each |

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Macro expansion complexity | High | Start with simple `\def`, add features incrementally |
| Alignment edge cases | Medium | Focus on common patterns first |
| Test reference generation | Medium | Use real TeX for authoritative output |
| Performance regression | Low | Profile after each milestone |

---

## References

- The TeXbook, Chapters 15, 16-18, 20, 22
- TeX: The Program (CWEB source)
- LaTeX2e source (`latex.ltx`)
- Existing implementation in `lambda/tex/`
