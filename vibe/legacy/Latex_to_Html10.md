# LaTeX to HTML V2 Formatter Progress Report

**Date:** December 13, 2025  
**Status:** In Progress  
**Baseline Tests:** 13 passing / 27 failing (out of 41 total)

## Overview

The LaTeX to HTML V2 formatter (`lambda/format/format_latex_html_v2.cpp`) converts LaTeX documents to HTML output. This session focused on improving font handling and fixing structural issues to match the latex.js baseline.

## Completed Improvements

### 1. Font Declaration Support ✅
**Problem:** Font declaration commands like `\bfseries`, `\em`, `\itshape` were not affecting subsequent text.

**Solution:** 
- Modified `processText()` to check `gen_->getFontClass(gen_->currentFont())` 
- When font state differs from default, text is wrapped in a `<span>` with appropriate CSS class
- Added `strip_next_leading_space_` flag to handle LaTeX's space-consuming behavior after declaration commands

**Files Changed:**
- `lambda/format/format_latex_html_v2.cpp` - Added font state checking in processText()
- `lambda/format/html_generator.cpp` - Modified `getFontClass()` to not output "rm" for Roman (default)

### 2. Double-Span Prevention ✅
**Problem:** Text-styling commands like `\textbf{}` were creating double-nested spans:
```html
<!-- Before: -->
<span class="bf"><span class="bf">Bold text</span></span>

<!-- After: -->
<span class="bf">Bold text</span>
```

**Solution:**
- Added `styled_span_depth_` counter with `enterStyledSpan()`/`exitStyledSpan()` methods
- Modified `processText()` to skip font wrapping when `inStyledSpan()` is true
- Updated all text-styling commands (`cmd_textbf`, `cmd_textit`, `cmd_emph`, etc.) to use the styled span tracking

### 3. Added Missing Commands ✅
- Added `cmd_em` for `\em` declaration command
- Added `cmd_par` for `\par` paragraph break command
- Registered commands in command table

## Outstanding Issues

### High Priority

#### 1. Paragraph Break Handling (whitespace_tex_1, whitespace_tex_10, whitespace_tex_11)
**Issue:** Multiple blank lines in LaTeX source should create paragraph breaks. Currently, content runs together or paragraphs are split incorrectly.

**Expected behavior:**
```latex
first paragraph

second paragraph
```
Should produce two separate `<p>` elements.

**Root cause:** The parser may not be preserving blank line information, or the processor doesn't recognize double-newlines as paragraph separators.

#### 2. Zero-Width Space Output (whitespace_tex_15, whitespace_tex_16)
**Issue:** `\empty{}` and `\empty\ ` should output a zero-width space character (`&#x200B;` / `\u200B`).

**Current output:** Regular space or nothing
**Expected output:** `some ​ text.` (with zero-width space)

#### 3. Nested \emph Toggle (fonts_tex_2)
**Issue:** When `\emph{}` is used inside an italic context (from `\em`), the baseline expects a nested span structure.

**Expected:**
```html
<span class="it">You can also </span>
<span class="it"><span class="up">emphasize</span></span>
<span class="it"> text.</span>
```

**Actual:**
```html
<span class="it">You can also </span>
<span class="up">emphasize</span>
<span class="it"> text.</span>
```

**Note:** Our output is semantically correct (toggles to upright), but structurally different from latex.js baseline.

### Medium Priority

#### 4. Environment Class Names (environments_tex_*)
**Issue:** Environment wrappers use different class naming conventions.

| Environment | Expected Class | Actual Class |
|------------|---------------|--------------|
| center | `list center` | `center` |
| flushleft | `list flushleft` | `flushleft` |
| itemize | `list` | `itemize` |

#### 5. List Item Structure (environments_tex_2, environments_tex_12)
**Issue:** List items need `itemlabel` spans and `hbox llap` structure.

**Expected:**
```html
<li><span class="itemlabel"><span class="hbox llap">•</span></span><p>first item</p></li>
```

**Actual:**
```html
<li> first item</li>
```

#### 6. Continuation Paragraphs (environments_tex_11)
**Issue:** Paragraphs following certain environments should have `class="continue"`.

### Low Priority

#### 7. Font Ligatures
**Issue:** latex.js applies typographic ligatures (fi → ﬁ, fl → ﬂ). We output standard ASCII.

**Note:** This is a deliberate design choice; ligatures can be applied via CSS or post-processing if needed.

#### 8. Macro Expansion Tests (macros_tex_1)
**Issue:** User-defined macro tests may have edge cases around parameter substitution.

## Architecture Notes

### Font State System
The formatter uses a dual approach for font styling:

1. **Declaration Commands** (`\bfseries`, `\em`, `\itshape`, etc.)
   - Modify `currentFont()` state in HtmlGenerator
   - Set `strip_next_leading_space_` flag
   - Let `processText()` wrap subsequent text in spans

2. **Styling Commands** (`\textbf{}`, `\textit{}`, `\emph{}`, etc.)
   - Create explicit span elements
   - Set `styled_span_depth_` to prevent double-wrapping
   - Process children within the span

### Key Data Structures
```cpp
// In LatexProcessor:
bool strip_next_leading_space_;  // Set by declarations
int styled_span_depth_;          // Nesting counter for styled spans
bool in_paragraph_;              // Paragraph state tracking

// In HtmlGenerator:
FontContext currentFont();       // Series, Shape, Family, Size
std::string getFontClass(FontContext);  // Returns CSS classes
```

## Suggested Follow-up Actions

### Immediate (Next Session)
1. **Fix paragraph detection** - Investigate how blank lines are represented in the parsed LaTeX tree and implement proper paragraph boundary detection
2. **Fix `\empty` command** - Output zero-width space (`\u200B`) instead of regular space

### Short Term
3. **Update environment class names** - Add "list" prefix to alignment environments
4. **Implement itemlabel structure** - Add proper label spans to list items
5. **Add continuation paragraph support** - Track paragraph context for `class="continue"`

### Long Term
6. **Review latex.js baseline expectations** - Some differences may be acceptable (e.g., ligatures, nested emph structure)
7. **Add comprehensive test coverage** - Create unit tests for edge cases discovered during baseline comparison
8. **Performance optimization** - Profile font state checking overhead

## Test Commands

```bash
# Build and run baseline tests
make build && make build-test
./test/test_latex_html_v2_baseline.exe

# Run specific test
./test/test_latex_html_v2_baseline.exe --gtest_filter='*fonts_tex_1*'

# Run other test suites
./test/test_latex_html_v2_lists_envs.exe      # 15 passing
./test/test_latex_html_v2_floats.exe
./test/test_latex_html_v2_bibliography.exe
```

## Files Modified This Session

| File | Changes |
|------|---------|
| `lambda/format/format_latex_html_v2.cpp` | Font state checking, styled span tracking, cmd_em, cmd_par |
| `lambda/format/html_generator.cpp` | getFontClass() default handling |

---

*Last updated: December 13, 2025*
