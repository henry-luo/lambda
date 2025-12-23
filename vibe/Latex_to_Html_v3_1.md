# LaTeX to HTML V3.1 - Design Document

**Date**: December 23, 2025  
**Status**: 📋 **Proposal**  
**Objective**: Extend V2 formatter to full LaTeX.js feature parity with CSS, document classes, fonts, and packages support

**Prerequisite**: V2 baseline complete (97/101 tests pass, 4 skipped)

---

## 1. Executive Summary

### Goal

Transform the current V2 formatter into a production-ready LaTeX to HTML converter that matches LaTeX.js functionality, enabling proper rendering of documents like `latex-showcase.tex` with:

1. **CSS Stylesheets** - Proper styling based on document class (article, book, report)
2. **Document Classes** - article, book, report with class-specific sectioning and counters
3. **Font Support** - Computer Modern fonts (Serif, Sans, Typewriter, Slanted variants)
4. **LaTeX Packages** - hyperref, multicol, textgreek, textcomp, gensymb, stix, graphicx, xcolor, etc.

### Architecture Overview

```
LaTeX Source (.tex)
    ↓
Tree-sitter Parser (input-latex-ts.cpp)
    ↓
Lambda Element Tree
    ↓
V3.1 Formatter (format_latex_html_v2.cpp - extended)
    ↓
HTML Document with:
    - CSS stylesheets (base.css, article.css, etc.)
    - Font face declarations (Computer Modern)
    - Document class-specific structure
    - Package-enabled symbols and commands
    ↓
Radiant Layout Engine (renders HTML/CSS)
    ↓
Visual Output (SVG/PNG/PDF/Screen)
```

---

## 2. CSS Stylesheet System

### 2.1 Source Files Analysis

LaTeX.js uses a layered CSS architecture:

| File | Purpose | Lines | Key Features |
|------|---------|-------|--------------|
| `base.css` | Core styling for all documents | 817 | Font imports, page layout, spacing, boxes, environments |
| `article.css` | Article-specific styles | 40 | Section heading sizes, margins |
| `book.css` | Book/report styles | 50 | Chapter headings, additional structure |
| `katex.css` | Math rendering | - | KaTeX math styling |
| `cmu.css` | Font imports | 14 | Imports Computer Modern font faces |

### 2.2 Implementation Strategy

**Phase 1: CSS Asset Management**

Create a CSS resource system that:
1. Copies CSS files to output directory structure
2. Generates `<link>` tags in HTML head
3. Supports configurable asset paths (relative, absolute, embedded)

**Directory structure for assets:**
```
output/
├── css/
│   ├── base.css
│   ├── article.css
│   ├── book.css
│   └── katex.css
├── fonts/
│   ├── cmu.css
│   ├── Serif/
│   ├── Sans/
│   ├── Typewriter/
│   └── Serif Slanted/
└── js/
    └── base.js
```

**Implementation in C++:**

```cpp
// New file: lambda/format/latex_assets.hpp
class LatexAssets {
public:
    enum class OutputMode {
        LINK,      // Generate <link> tags pointing to external files
        EMBED,     // Embed CSS directly in <style> tags
        INLINE     // Return raw CSS for programmatic use
    };
    
    // Copy assets to destination directory
    static bool copyAssetsTo(const char* dest_dir);
    
    // Get stylesheet links for document class
    static std::string getStylesheetLinks(const char* doc_class, 
                                          const char* base_url = nullptr);
    
    // Get embedded stylesheet content
    static std::string getEmbeddedStyles(const char* doc_class);
    
    // Get JavaScript for dynamic features (marginpar positioning, etc.)
    static std::string getScript();
};
```

**Phase 2: CSS Variable System**

LaTeX.js uses CSS custom properties extensively. Key variables to implement:

```css
:root {
    --size: 10pt;              /* Base font size */
    --line-height: 1.2;
    --parindent: 1.5em;
    --parskip: 0px;
    --leftmargini: 2.5em;      /* List margins by level */
    --labelsep: 0.5rem;
    --fboxrule: 0.4pt;
    --fboxsep: 3pt;
    /* ... 50+ more variables */
}
```

**C++ Length System Enhancement:**

```cpp
// Extend LatexProcessor to track and output CSS variables
class LatexProcessor {
    std::map<std::string, Length> lengths_;
    
    void setLength(const char* name, Length len);
    Length getLength(const char* name);
    
    // Generate CSS variable declarations
    std::string generateLengthCSS();
};
```

### 2.3 Key CSS Classes to Support

**Font Classes:**
- `.rm`, `.sf`, `.tt` - Font families
- `.md`, `.bf` - Font weights
- `.up`, `.it`, `.sl`, `.sc` - Font shapes
- `.tiny`, `.small`, `.large`, `.Large`, `.LARGE`, `.huge`, `.Huge` - Sizes

**Layout Classes:**
- `.body`, `.page` - Page structure
- `.margin-left`, `.margin-right`, `.marginpar` - Margins
- `.onecolumn`, `.twocolumn`, `.multicols` - Column layouts

**Box Classes:**
- `.hbox`, `.parbox`, `.frame` - Box types
- `.llap`, `.rlap`, `.clap` - Overlapping boxes
- `.phantom`, `.smash` - Size manipulation

**Environment Classes:**
- `.list`, `.quote`, `.quotation`, `.verse` - Text environments
- `.picture`, `.picture-canvas`, `.put-obj` - Picture environment

---

## 3. Document Class System

### 3.1 Architecture

LaTeX.js document classes are hierarchical:

```
Base (base.ls)
  ├── Article (article.ls)
  ├── Report (report.ls)
  │     └── Book (book.ls)
```

### 3.2 Base Class Features

The base class defines:

**Counters:**
- `part`, `section`, `subsection`, `subsubsection`, `paragraph`, `subparagraph`
- `figure`, `table`

**Lengths:**
- Page geometry: `paperheight`, `paperwidth`, `textwidth`
- Margins: `oddsidemargin`, `marginparwidth`, `marginparsep`, `marginparpush`

**Class Options:**
- Paper sizes: `a4paper`, `a5paper`, `b5paper`, `letterpaper`, `legalpaper`, `executivepaper`
- Layout: `oneside`, `twoside`, `onecolumn`, `twocolumn`, `landscape`
- Font sizes: `10pt`, `11pt`, `12pt`

**Sectioning:**
- `\part`, `\section`, `\subsection`, etc.
- Counter formatting: `\thepart`, `\thesection`, etc.
- `\maketitle` with title, author, date

### 3.3 Article Class

```cpp
// Additional features over Base:
- CSS: css/article.css
- secnumdepth: 3
- tocdepth: 3
- \refname: "References"
- \tableofcontents
- \abstract environment
- \appendix
```

### 3.4 Report/Book Class

```cpp
// Additional features:
- CSS: css/book.css
- \chapter sectioning command
- \chaptername: "Chapter"
- \bibname: "Bibliography"
- \frontmatter, \mainmatter, \backmatter
- Chapter-based counter resets
```

### 3.5 C++ Implementation

```cpp
// New file: lambda/format/latex_docclass.hpp

class DocumentClass {
public:
    virtual ~DocumentClass() = default;
    
    virtual const char* cssFile() const = 0;
    virtual void initCounters(LatexProcessor* proc) = 0;
    virtual void initLengths(LatexProcessor* proc) = 0;
    virtual void processOptions(const std::vector<std::string>& options) = 0;
    
    // Sectioning
    virtual void registerSectionCommands(LatexProcessor* proc) = 0;
    
    // Counter formatting
    virtual std::string theCounter(const char* name, int value) = 0;
};

class ArticleClass : public DocumentClass {
public:
    const char* cssFile() const override { return "css/article.css"; }
    void initCounters(LatexProcessor* proc) override;
    // ...
};

class BookClass : public DocumentClass {
public:
    const char* cssFile() const override { return "css/book.css"; }
    void initCounters(LatexProcessor* proc) override;
    // Has chapter support
};

// Factory
std::unique_ptr<DocumentClass> createDocumentClass(const char* name);
```

---

## 4. Font Support

### 4.1 Computer Modern Fonts

LaTeX.js includes Computer Modern Unicode fonts in WOFF format:

| Font Family | Files | Purpose |
|-------------|-------|---------|
| Serif (cmun-serif) | cmunrm.woff, cmunti.woff, cmunbx.woff, cmunbi.woff | Main text |
| Sans (cmun-sans) | cmunss.woff, cmunsi.woff, cmunsx.woff, cmunso.woff | Sans-serif |
| Typewriter (cmun-typewriter) | cmuntt.woff, cmunit.woff, cmuntb.woff, cmuntx.woff | Monospace |
| Serif Slanted (cmun-serif-slanted) | cmunsl.woff, cmunbl.woff | Slanted text |
| Typewriter Slanted | cmunst.woff | Slanted mono |

### 4.2 Font CSS Structure

```css
/* cmu.css - imports individual font stylesheets */
@import url("./Sans/cmun-sans.css");
@import url("./Serif/cmun-serif.css");
@import url("./Serif Slanted/cmun-serif-slanted.css");
@import url("./Typewriter/cmun-typewriter.css");
@import url("./Typewriter Slanted/cmun-typewriter-slanted.css");
```

```css
/* Serif/cmun-serif.css */
@font-face {
    font-family: 'Computer Modern Serif';
    src: url('cmunrm.woff') format('woff');
    font-weight: normal;
    font-style: normal;
}
/* ... variants for bold, italic, bold-italic */
```

### 4.3 Implementation Strategy

**Option A: Bundle Fonts (Recommended for standalone)**
- Copy font files to output directory
- Reference via relative URLs in CSS

**Option B: CDN/External (For web deployment)**
- Reference fonts from CDN
- Configurable base URL

**Option C: Embedded (For single-file output)**
- Base64-encode fonts into CSS
- Larger file size but self-contained

```cpp
// Font configuration in LatexAssets
struct FontConfig {
    enum class Mode { BUNDLED, CDN, EMBEDDED };
    Mode mode = Mode::BUNDLED;
    std::string cdn_base_url;
};
```

---

## 5. Package Support

### 5.1 Package Architecture

Each LaTeX package provides:
1. **New commands** - Additional macros
2. **Symbols** - Unicode character mappings
3. **Environments** - New environment handlers

### 5.2 Package Implementation Plan

**Priority 1: Core Packages (for latex-showcase.tex)**

| Package | Commands/Symbols | Implementation Effort |
|---------|------------------|----------------------|
| `hyperref` | `\href`, `\url`, `\nolinkurl` | Low (already partial) |
| `multicol` | `multicols` environment | Medium |
| `textgreek` | Greek letters (α-ω, Α-Ω) | Low (symbol map) |
| `textcomp` | Extended symbols (℃, ¶, etc.) | Low (symbol map) |
| `gensymb` | `\degree`, `\celsius`, `\ohm`, `\micro` | Low (symbol map) |
| `stix` | Math symbols, card suits | Low |
| `comment` | `comment` environment | Low (ignore content) |

**Priority 2: Graphics/Color Packages**

| Package | Commands | Implementation |
|---------|----------|----------------|
| `graphicx` | `\includegraphics`, `\rotatebox`, `\scalebox` | Medium |
| `xcolor` | `\textcolor`, `\colorbox`, `\definecolor` | Already implemented |
| `picture`, `pict2e` | Picture environment | Complex (SVG generation) |

### 5.3 Package Registry

```cpp
// New file: lambda/format/latex_packages.hpp

class LatexPackage {
public:
    virtual ~LatexPackage() = default;
    virtual const char* name() const = 0;
    virtual void registerCommands(LatexProcessor* proc) = 0;
    virtual void registerSymbols(SymbolTable& symbols) = 0;
    virtual void processOptions(const std::vector<std::string>& options) = 0;
};

class PackageRegistry {
    std::map<std::string, std::function<std::unique_ptr<LatexPackage>()>> factories_;
public:
    void registerPackage(const char* name, ...);
    std::unique_ptr<LatexPackage> loadPackage(const char* name);
    bool isProvided(const char* name);  // Built-in packages
};

// Individual package implementations
class HyperrefPackage : public LatexPackage { ... };
class MulticolPackage : public LatexPackage { ... };
class TextgreekPackage : public LatexPackage { ... };
```

### 5.4 Symbol Tables

Implement symbol maps for each package:

```cpp
// textgreek symbols
static const std::map<std::string, const char*> TEXTGREEK_SYMBOLS = {
    {"textalpha", "α"},
    {"textbeta", "β"},
    {"textgamma", "γ"},
    // ... 52 Greek letters total
};

// gensymb symbols
static const std::map<std::string, const char*> GENSYMB_SYMBOLS = {
    {"degree", "°"},
    {"celsius", "℃"},
    {"perthousand", "‰"},
    {"ohm", "Ω"},
    {"micro", "μ"},
};

// textcomp symbols
static const std::map<std::string, const char*> TEXTCOMP_SYMBOLS = {
    {"textborn", "⭑"},
    {"textdied", "†"},
    {"textpilcrow", "¶"},
    {"textdblhyphen", "⹀"},
    // ... 30+ symbols
};
```

---

## 6. Implementation Phases

### Phase 1: CSS Infrastructure (Estimated: 2-3 days)

**Tasks:**
1. Create `latex_assets.hpp/cpp` - Asset management
2. Copy CSS files from `latex-js/src/css/` to Lambda resources
3. Implement stylesheet link generation
4. Update HTML output to include proper `<head>` section
5. Test with basic document

**Files to create/modify:**
- `lambda/format/latex_assets.hpp` (new)
- `lambda/format/latex_assets.cpp` (new)
- `lambda/format/format_latex_html_v2.cpp` (modify)
- `lambda/format/html_generator.hpp` (modify for head section)

**Deliverables:**
- HTML output includes `<link>` to CSS files
- CSS variables work correctly
- Font classes apply proper styling

### Phase 2: Font System (Estimated: 1-2 days)

**Tasks:**
1. Copy font files from `latex-js/src/fonts/`
2. Create font CSS import structure
3. Implement font path configuration
4. Test with various font commands

**Files:**
- Font resources (copy from latex-js)
- `lambda/format/latex_fonts.hpp` (new, optional)

**Deliverables:**
- Computer Modern fonts render correctly
- All font variants work (rm, sf, tt, sl, sc, bf, it)

### Phase 3: Document Classes (Estimated: 2-3 days)

**Tasks:**
1. Create `DocumentClass` base class
2. Implement `ArticleClass`, `ReportClass`, `BookClass`
3. Add class option parsing
4. Implement counter management per class
5. Add sectioning command variations

**Files:**
- `lambda/format/latex_docclass.hpp` (new)
- `lambda/format/latex_docclass.cpp` (new)
- `lambda/format/format_latex_html_v2.cpp` (modify)

**Deliverables:**
- `\documentclass{article}` selects correct CSS and behavior
- Class options work (a4paper, 12pt, etc.)
- Sectioning commands formatted per class

### Phase 4: Package System (Estimated: 3-4 days)

**Tasks:**
1. Create package registry infrastructure
2. Implement priority 1 packages:
   - `hyperref` - links
   - `multicol` - multi-column layout
   - `textgreek` - Greek letters
   - `textcomp` - extended symbols
   - `gensymb` - scientific symbols
   - `stix` - math symbols
   - `comment` - comment environment
3. Handle `\usepackage` command

**Files:**
- `lambda/format/latex_packages.hpp` (new)
- `lambda/format/latex_packages.cpp` (new)
- `lambda/format/packages/` (new directory)
  - `pkg_hyperref.cpp`
  - `pkg_multicol.cpp`
  - `pkg_textgreek.cpp`
  - `pkg_textcomp.cpp`
  - `pkg_gensymb.cpp`
  - `pkg_stix.cpp`

**Deliverables:**
- `\usepackage{textgreek}` enables Greek letters
- All symbols from packages render correctly
- `multicols` environment works

### Phase 5: Picture Environment (Estimated: 3-4 days)

**Tasks:**
1. Implement picture coordinate system
2. Add `\put` command
3. Implement drawing primitives:
   - `\line`, `\vector`
   - `\circle`, `\circle*`
   - `\oval`
   - `\qbezier`
4. Generate SVG output for pictures

**Files:**
- `lambda/format/latex_picture.hpp` (new)
- `lambda/format/latex_picture.cpp` (new)

**Deliverables:**
- Picture environment renders as SVG
- All primitives from latex-showcase.tex work

### Phase 6: Integration & Testing (Estimated: 2-3 days)

**Tasks:**
1. Test with `latex-showcase.tex`
2. Fix any remaining issues
3. Add integration tests
4. Update documentation
5. Performance optimization

**Deliverables:**
- `latex-showcase.tex` renders correctly
- All baseline tests pass
- Documentation complete

---

## 7. Testing Strategy

### 7.1 Test Categories

1. **CSS Tests** - Verify CSS classes apply correctly
2. **Font Tests** - Verify all font variants render
3. **Document Class Tests** - Each class-specific feature
4. **Package Tests** - Each package's commands/symbols
5. **Integration Tests** - Full documents like latex-showcase.tex

### 7.2 Test Files

```
test/latex/
├── fixtures_v3/
│   ├── css_base.tex
│   ├── css_fonts.tex
│   ├── docclass_article.tex
│   ├── docclass_book.tex
│   ├── pkg_hyperref.tex
│   ├── pkg_multicol.tex
│   ├── pkg_textgreek.tex
│   └── ...
└── test_latex_html_v3.cpp
```

### 7.3 Visual Regression

For complex rendering (pictures, layouts):
1. Render to HTML
2. Use Radiant to render to PNG
3. Compare against reference images

---

## 8. Resource Management

### 8.1 Asset Embedding Options

**For CLI usage:**
```bash
# Copy assets to output directory
./lambda.exe render document.tex -o output/ --assets=copy

# Embed all assets in single HTML file
./lambda.exe render document.tex -o output.html --assets=embed

# Use CDN for assets
./lambda.exe render document.tex -o output.html --assets=cdn --cdn-url=https://cdn.example.com/
```

**For programmatic usage:**
```cpp
FormatOptions opts;
opts.asset_mode = AssetMode::EMBED;  // or LINK, CDN
opts.base_url = nullptr;  // or "https://..."
Item html = format_latex_html_v3(input, opts);
```

### 8.2 File Size Considerations

| Asset Type | Size (uncompressed) | Size (gzip) |
|------------|---------------------|-------------|
| base.css | ~20KB | ~4KB |
| article.css | ~1KB | ~0.5KB |
| All fonts | ~500KB | ~400KB |
| katex.css | ~25KB | ~5KB |
| Total (embedded) | ~550KB | ~410KB |

---

## 9. Dependencies

### 9.1 New Dependencies

None required - all functionality implemented in C++.

### 9.2 Optional Enhancements

- **KaTeX C++ port** - For native math rendering (currently uses text-based fallback)
- **Hyphenation library** - For automatic hyphenation (like Hypher in LaTeX.js)

---

## 10. Migration Path

### 10.1 Backward Compatibility

V3.1 is a superset of V2:
- All V2 tests continue to pass
- V2 API remains functional
- New features are additive

### 10.2 API Changes

```cpp
// V2 API (remains supported)
Item format_latex_html_v2(Input* input, bool text_mode);

// V3.1 API (new)
struct FormatOptionsV3 {
    bool text_mode = true;
    const char* base_url = nullptr;
    AssetMode asset_mode = AssetMode::LINK;
    bool include_doctype = true;
    bool standalone_html = true;  // Include <html>, <head>, <body>
};
Item format_latex_html_v3(Input* input, const FormatOptionsV3& opts);
```

---

## 11. Success Criteria

### 11.1 Minimum Viable Product

1. ✅ `latex-showcase.tex` renders with proper styling
2. ✅ CSS classes applied correctly
3. ✅ Computer Modern fonts display
4. ✅ Document class options work (article/book/report)
5. ✅ Basic packages work (hyperref, textgreek, textcomp, gensymb)

### 11.2 Full Feature Parity

1. ✅ All LaTeX.js packages implemented
2. ✅ Picture environment with SVG output
3. ✅ Multi-column layouts
4. ✅ Margin notes with proper positioning
5. ✅ All CSS from latex-js working

### 11.3 Performance Targets

- Parse + format time for latex-showcase.tex: < 100ms
- Memory usage: < 50MB for typical documents

---

## 12. Appendix: latex-showcase.tex Analysis

### Commands Used

**Document Structure:**
- `\documentclass{article}`
- `\usepackage{comment, multicol, hyperref, calc, pict2e, picture, textgreek, textcomp, gensymb, stix}`
- `\setcounter{secnumdepth}{2}`
- `\title`, `\author`, `\date`, `\maketitle`
- `\begin{abstract}`, `\section`, `\subsection`

**Text Formatting:**
- `\texttt`, `\textbackslash`, `\emph`, `\textbf`, `\textit`
- Font sizes: `\footnotesize`, `\small`, `\normalsize`, `\large`, `\Large`, `\LARGE`, `\huge`, `\Huge`
- `\underline`, `\textsf`, `\textsc`, `\textsl`

**Environments:**
- `itemize`, `enumerate`, `description`
- `quote`, `quotation`, `verse`
- `center`, `flushleft`, `flushright`
- `multicols`
- `picture`
- `comment`

**Boxes:**
- `\mbox`, `\fbox`, `\makebox`, `\framebox`, `\parbox`
- `\llap`, `\rlap`, `\smash`, `\phantom`, `\hphantom`, `\vphantom`

**Spacing:**
- `\bigskip`, `\medskip`, `\smallskip`
- `\negthinspace`, `\,`, `\enspace`, `\quad`, `\qquad`, `\hspace`
- `~` (non-breaking space)

**Special Characters:**
- `\$`, `\&`, `\%`, `\#`, `\_`, `\{`, `\}`, `\~{}`, `\^{}`, `\textbackslash`
- `\symbol{"00A9}`, `\char"A9`, `^^A9`, `^^^^00A9`

**Symbols (from packages):**
- Greek: `\textalpha` through `\textOmega`
- Currency: `\texteuro`, `\textcent`, `\textsterling`, `\pounds`, etc.
- Math: `\textperthousand`, `\textonehalf`, `\textdiv`, `\texttimes`, etc.
- Misc: `\checkmark`, `\textcopyright`, `\textregistered`, `\AE`, `\oe`, etc.

**Picture Commands:**
- `\setlength{\unitlength}{...}`
- `\put`, `\line`, `\vector`, `\circle`, `\circle*`, `\oval`, `\qbezier`
- `\thicklines`, `\thinlines`, `\linethickness`
- `\multiput`, `\frame`

**References:**
- `\label`, `\ref`
- `Section~\ref{...}`

**Math:**
- Inline: `$...$`
- Display: `$$...$$`

**Links:**
- `\url{...}`

---

## 13. Timeline Summary

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| Phase 1: CSS Infrastructure | 2-3 days | None |
| Phase 2: Font System | 1-2 days | Phase 1 |
| Phase 3: Document Classes | 2-3 days | Phase 1 |
| Phase 4: Package System | 3-4 days | Phase 1, 3 |
| Phase 5: Picture Environment | 3-4 days | Phase 1 |
| Phase 6: Integration & Testing | 2-3 days | All |
| **Total** | **13-19 days** | |

**Recommended Start**: Phase 1 (CSS Infrastructure) - provides foundation for all other phases.
