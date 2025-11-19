# LaTeX to HTML Conversion - Enhancement Plan to 100% Pass Rate

**Date**: November 19, 2025  
**Current Status**: 19/30 tests passing (63.3%), 11 tests skipped  
**Target**: 30/30 tests passing (100%)  
**Estimated Effort**: 10-17 hours over 3-4 weeks

---

## Executive Summary

Lambda's LaTeX to HTML converter is **architecturally sound** and production-ready for most use cases. The current 63.3% pass rate is primarily due to **parser bugs** rather than formatter limitations. This document provides a comprehensive analysis comparing Lambda's implementation with LaTeX.js, identifies root causes of test failures, and proposes a phased enhancement plan to achieve 100% test compatibility.

**Key Finding**: The HTML formatter in `format-latex-html.cpp` is well-designed. The main blocker is the LaTeX parser's inability to correctly extract environment names from `\begin{name}` constructs.

---

## Current Implementation Analysis

### Test Results Breakdown

**Passing Tests (19/30):**
- ✅ Basic text formatting (`\textbf`, `\textit`, `\texttt`)
- ✅ Emphasis commands (`\emph`)
- ✅ Underline and strikethrough (`\underline`, `\sout`)
- ✅ Font size commands (`\tiny`, `\small`, `\large`, etc.)
- ✅ Basic sectioning (`\section`, `\subsection`, `\subsubsection`)
- ✅ Multiple sections with hierarchy
- ✅ Section titles with formatting
- ✅ Simple text paragraphs
- ✅ Paragraph breaks (`\n\n`) and line breaks (`\\`, `\newline`)
- ✅ Itemize environment (when directly invoked)
- ✅ Nested formatting (partially working)

**Skipped/Failing Tests (11/30):**

| Test | Issue Category | Root Cause | Priority |
|------|---------------|------------|----------|
| `formatting_tex_6` - "text alignment" | Parser Bug | `\begin{center/flushleft/flushright}` not parsing | P1 |
| `sectioning_tex_2` - "document with title" | Formatter Bug | Title/author/date metadata extraction incomplete | P2 |
| `basic_text_tex_3` - "UTF-8 text and punctuation" | Formatter Bug | Thin space `\,` rendered as regular space, missing dash conversion | P2 |
| `basic_text_tex_4` - "special characters" | Formatter Bug | Missing space after special characters like `\$`, `\#` | P2 |
| `basic_text_tex_6` - "verbatim text" | Parser Bug | Inline `\verb\|...\|` delimiter parsing broken | P3 |
| `environments_tex_2` - "enumerate environment" | Parser Bug | All environments misparsed as `itemize` | P1 |
| `environments_tex_3` - "nested lists" | Parser Bug | Nested environment parsing fails | P1 |
| `environments_tex_4` - "quote environment" | Parser Bug | Treated as `itemize` instead of `quote` | P1 |
| `environments_tex_5` - "verbatim environment" | Parser Bug | Treated as `itemize` instead of `verbatim` | P1 |
| `environments_tex_6` - "center environment" | Parser Bug | Treated as `itemize` instead of `center` | P1 |
| `environments_tex_7` - "mixed environments" | Parser Bug | Mixed list/quote parsing fails | P1 |

### Issue Categorization

#### Priority 1: Parser Environment Extraction (6 tests)
**Impact**: Blocks 6 tests (environments_tex_2, 3, 4, 5, 6, 7 + formatting_tex_6)  
**Effort**: Medium (4-8 hours)  
**File**: `lambda/input/input-latex.cpp`

The parser is not correctly extracting environment names from `\begin{envname}...\end{envname}` constructs. All environments are being treated as `itemize`.

#### Priority 2: Character Spacing and Typography (3 tests)
**Impact**: Blocks 3 tests (basic_text_tex_3, 4, sectioning_tex_2)  
**Effort**: Low (3-4 hours)  
**File**: `lambda/format/format-latex-html.cpp`

Missing:
- Dash conversion (`--` → en-dash, `---` → em-dash)
- Thin space rendering (`\,` → `&thinsp;`)
- Space after special characters (`\$`, `\#`, etc.)
- Title/author/date metadata extraction from nested elements

#### Priority 3: Inline Verbatim (1 test)
**Impact**: Blocks 1 test (basic_text_tex_6)  
**Effort**: Low (2-3 hours)  
**File**: `lambda/input/input-latex.cpp`

The `\verb|...|` command with arbitrary delimiters is not parsing correctly.

---

## Comparison with LaTeX.js

Based on analysis of LaTeX.js source code at `/Users/henryluo/Projects/LaTeX.js/src/`, here's what Lambda needs to match:

### LaTeX.js Features Already in Lambda ✅

1. **HTML Generation Framework** (`html-generator.ls`)
   - Element creation functions (`create()`)
   - Block vs inline element detection
   - CSS class-based styling

2. **Text Formatting** (`latex.ltx.ls:249-270`)
   - `\textbf`, `\textit`, `\emph` with attribute handling
   - Group management (Lambda handles this via AST structure)

3. **Sectioning** (`html-generator.ls:48-54`)
   - Full hierarchy: part, chapter, section, subsection, etc.
   - Lambda has section, subsection, subsubsection

4. **List Environments** (`latex.ltx.ls:390-439`)
   - Itemize, enumerate, description lists
   - Lambda has itemize and enumerate when parser works

5. **Document Metadata** (`html-generator.ls:41-44`)
   - Title, author, date elements
   - Lambda stores these but extraction needs work

### LaTeX.js Features Missing in Lambda 🔄

#### 1. **Dash Conversion** (`symbols.ls:13-14`)
```livescript
# LaTeX.js implementation:
* '--'     he.decode '&ndash;'  # U+2013 en-dash
* '---'    he.decode '&mdash;'  # U+2014 em-dash
```

**Lambda needs**: Text preprocessing in `append_escaped_text()` to convert:
- `--` → `–` (U+2013)
- `---` → `—` (U+2014)

#### 2. **Thin Space Entity** (`symbols.ls:48`)
```livescript
# LaTeX.js implementation:
* \thinspace    he.decode '&thinsp;'  # U+2009
```

**Lambda needs**: Change `\,` rendering from regular space to `&thinsp;` HTML entity.

#### 3. **Environment Name Extraction** (`latex.ltx.ls:378-439`)
```livescript
# LaTeX.js implementation:
\itemize    : (items) -> # Separate handler for each environment
\enumerate  : (items) -> # ...
\quote      : -> @g.startlist!; [ @g.create @g.quote ]
```

**Lambda needs**: Parser fix to correctly identify environment type and pass to formatter.

#### 4. **Special Character Spacing**
LaTeX.js adds proper spacing context around special characters. Lambda needs trailing space after control symbols.

---

## Root Cause Analysis

### Issue #1: Environment Name Not Extracted (Critical)

**Current Behavior:**
```cpp
// In format-latex-html.cpp:819 - process_environment()
static void process_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem || !elem->items || elem->length == 0) return;
    
    // Expects environment name in first child
    TypeId content_type = get_type_id(elem->items[0]);
    if (content_type == LMD_TYPE_STRING) {
        String* env_name = (String*)elem->items[0].pointer;
        // All environments come through as "itemize"!
    }
}
```

**Root Cause:**
The LaTeX parser in `input-latex.cpp` is not correctly parsing the tree-sitter AST for `\begin{envname}` constructs. The environment name should be extracted from the tree-sitter node and stored as the first child element, but this extraction is failing or incomplete.

**Evidence:**
- Test output shows `process_itemize` being called for all environments
- Formatter has handlers for `quote`, `verbatim`, `center`, etc. that are never invoked
- Debug logs would show `env_name->chars` always equals "itemize"

### Issue #2: Missing Typography Features

**Missing Dash Conversion:**
```cpp
// Current: append_escaped_text() only handles HTML entities
static void append_escaped_text(StringBuf* html_buf, const char* text) {
    for (const char* p = text; *p; p++) {
        switch (*p) {
            case '<': stringbuf_append_str(html_buf, "&lt;"); break;
            case '>': stringbuf_append_str(html_buf, "&gt;"); break;
            // ... but no dash conversion
        }
    }
}
```

**Missing Thin Space:**
```cpp
// Current: line 472 in format-latex-html.cpp
else if (strcmp(cmd_name, "thinspace") == 0) {
    stringbuf_append_char(html_buf, ' '); // Regular space - WRONG!
}
```

**Missing Special Character Spacing:**
```cpp
// Current: line 465
else if (strcmp(cmd_name, "literal") == 0) {
    process_element_content_simple(html_buf, elem, pool, depth);
    // No trailing space added
}
```

### Issue #3: Metadata Extraction Incomplete

**Current Implementation:**
```cpp
// format-latex-html.cpp:778 - process_title()
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (elem->items && elem->length > 0) {
        TypeId content_type = get_type_id(elem->items[0]);
        if (content_type == LMD_TYPE_STRING) {
            String* title_str = (String*)elem->items[0].pointer;
            doc_state.title = strdup(title_str->chars);
        }
    }
}
```

**Problem**: Only extracts if first child is a string. Fails when title contains formatting like `\title{My \textbf{Bold} Title}`.

**Solution**: Need recursive text extraction from all children, not just first string child.

### Issue #4: Inline Verbatim Delimiter Parsing

**Current Behavior:**
The parser doesn't correctly handle `\verb|text|` where `|` can be any character delimiter. The tree-sitter grammar needs to capture the delimiter and extract content between matching delimiters.

**LaTeX Spec:**
- `\verb|text|` uses `|` as delimiter
- `\verb/text/` uses `/` as delimiter
- Any non-letter, non-space character can be a delimiter
- Content between delimiters is literal text (no escaping)

---

## Enhancement Plan - Detailed

### Phase 1: Critical Parser Fixes (Week 1)

**Goal**: Fix environment name extraction  
**Impact**: 25/30 tests passing (83%)  
**Effort**: 4-8 hours

#### Task 1.1: Debug Environment Parsing

**File**: `lambda/input/input-latex.cpp`

**Steps**:
1. Add comprehensive logging to environment parsing:
```cpp
log_debug("Parsing environment node:");
log_debug("  node_type: %s", ts_node_type(env_node));
log_debug("  child_count: %d", ts_node_child_count(env_node));

for (int i = 0; i < ts_node_child_count(env_node); i++) {
    TSNode child = ts_node_child(env_node, i);
    log_debug("  child[%d] type: %s", i, ts_node_type(child));
    log_debug("  child[%d] text: %.*s", i, text_len, text_start);
}
```

2. Run test with `environments_tex_2` (enumerate) and capture logs
3. Compare with test with `environments_tex_1` (itemize) to find difference
4. Identify which tree-sitter node contains environment name

#### Task 1.2: Fix Environment Name Extraction

**Expected tree-sitter structure** (to be confirmed with logs):
```
begin_environment
├── command_name: "begin"
├── argument: "{enumerate}"  ← Extract this!
└── content: [...]
```

**Fix approach**:
```cpp
// In parse_environment() function:
TSNode name_node = ts_node_child_by_field_name(env_node, "argument", 8);
if (!ts_node_is_null(name_node)) {
    // Extract text from argument node
    const char* name_start = ts_node_start_byte(name_node) + source;
    uint32_t name_len = ts_node_end_byte(name_node) - ts_node_start_byte(name_node);
    
    // Remove braces: "{enumerate}" -> "enumerate"
    if (name_len >= 2 && name_start[0] == '{' && name_start[name_len-1] == '}') {
        name_start++;
        name_len -= 2;
    }
    
    // Create string and store as first child
    String* env_name = string_new_from_cstr_length(pool, name_start, name_len);
    array_append(env_element->items, s2it(env_name), pool);
}
```

#### Task 1.3: Test and Validate

**Test commands**:
```bash
# Build
make build

# Test single environment
./test/test_latex_html_fixtures.exe --gtest_filter="*enumerate*"

# Test all environments
./test/test_latex_html_fixtures.exe --gtest_filter="*environments*"
```

**Success criteria**:
- `environments_tex_2` (enumerate) shows `<ol>` instead of `<ul>`
- `environments_tex_4` (quote) shows `<div class="latex-quote">`
- `environments_tex_5` (verbatim) shows `<pre class="latex-verbatim">`
- 6 additional tests pass

---

### Phase 2: Typography and Metadata (Week 2)

**Goal**: Fix character spacing, dash conversion, thin space, metadata  
**Impact**: 28/30 tests passing (93%)  
**Effort**: 3-4 hours

#### Task 2.1: Add Dash Conversion

**File**: `lambda/format/format-latex-html.cpp:900` (in `append_escaped_text()`)

**Implementation**:
```cpp
static void append_escaped_text(StringBuf* html_buf, const char* text) {
    if (!text) return;
    
    for (const char* p = text; *p; p++) {
        // Check for em-dash (---)
        if (*p == '-' && *(p+1) == '-' && *(p+2) == '-') {
            stringbuf_append_str(html_buf, "—"); // U+2014 em-dash
            p += 2; // Skip next two dashes
        }
        // Check for en-dash (--)
        else if (*p == '-' && *(p+1) == '-') {
            stringbuf_append_str(html_buf, "–"); // U+2013 en-dash
            p += 1; // Skip next dash
        }
        // HTML entity escaping
        else if (*p == '<') {
            stringbuf_append_str(html_buf, "&lt;");
        }
        else if (*p == '>') {
            stringbuf_append_str(html_buf, "&gt;");
        }
        else if (*p == '&') {
            stringbuf_append_str(html_buf, "&amp;");
        }
        else if (*p == '"') {
            stringbuf_append_str(html_buf, "&quot;");
        }
        else {
            stringbuf_append_char(html_buf, *p);
        }
    }
}
```

**Test**:
```bash
./test/test_latex_html_fixtures.exe --gtest_filter="*basic_text_tex_5*"
```

Expected: `pages 13–67, yes—or no?` (en-dash and em-dash rendered correctly)

#### Task 2.2: Fix Thin Space Rendering

**File**: `lambda/format/format-latex-html.cpp:472`

**Change**:
```cpp
// BEFORE:
else if (strcmp(cmd_name, "thinspace") == 0) {
    stringbuf_append_char(html_buf, ' '); // Regular space
}

// AFTER:
else if (strcmp(cmd_name, "thinspace") == 0) {
    stringbuf_append_str(html_buf, "&thinsp;"); // HTML thin space entity (U+2009)
}
```

**Test**:
```bash
./test/test_latex_html_fixtures.exe --gtest_filter="*basic_text_tex_3*"
```

Expected: `80 000` with proper thin space rendering (visually narrower than regular space)

#### Task 2.3: Add Special Character Spacing

**File**: `lambda/format/format-latex-html.cpp:465`

**Implementation**:
```cpp
else if (strcmp(cmd_name, "literal") == 0) {
    // Render literal character content with HTML escaping
    process_element_content_simple(html_buf, elem, pool, depth);
    // Add trailing space for special characters that need it
    // This matches LaTeX behavior where \$ produces "$ " not "$"
    stringbuf_append_char(html_buf, ' ');
}
```

**Alternative approach** (more precise):
Store which literal characters need trailing space and check:
```cpp
else if (strcmp(cmd_name, "literal") == 0) {
    size_t start_pos = html_buf->length;
    process_element_content_simple(html_buf, elem, pool, depth);
    
    // Check last character added
    if (html_buf->length > start_pos) {
        char last_char = html_buf->str[html_buf->length - 1];
        // Add space after money symbols, hash, percent
        if (last_char == '$' || last_char == '#' || last_char == '%') {
            stringbuf_append_char(html_buf, ' ');
        }
    }
}
```

**Test**:
```bash
./test/test_latex_html_fixtures.exe --gtest_filter="*basic_text_tex_4*"
```

Expected: `# $ $ ^ & _ { } ~ \ %` with proper spacing between characters

#### Task 2.4: Fix Metadata Extraction

**File**: `lambda/format/format-latex-html.cpp:778-820`

**Problem**: Current code only extracts if first child is string. Need recursive extraction.

**Implementation**:
```cpp
// Helper function for recursive text extraction
static void extract_text_recursive(StringBuf* buf, Element* elem, Pool* pool) {
    if (!elem || !elem->items) return;
    
    for (int i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);
        
        if (type == LMD_TYPE_STRING) {
            String* str = (String*)child.pointer;
            if (str && str->chars) {
                stringbuf_append_str(buf, str->chars);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* child_elem = (Element*)child.pointer;
            extract_text_recursive(buf, child_elem, pool);
        }
    }
}

// Updated process_title, process_author, process_date
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;
    
    // Use temporary buffer to extract all text
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);
    
    String* title_str = stringbuf_to_string(temp_buf);
    if (title_str && title_str->len > 0) {
        doc_state.title = strdup(title_str->chars);
    }
    
    stringbuf_free(temp_buf);
}

// Same pattern for process_author() and process_date()
```

**Test**:
```bash
./test/test_latex_html_fixtures.exe --gtest_filter="*sectioning_tex_2*"
```

Expected: Title, author, and date rendered correctly even with formatting inside

---

### Phase 3: Inline Verbatim (Week 3)

**Goal**: Fix `\verb|...|` command  
**Impact**: 29/30 tests passing (97%)  
**Effort**: 2-3 hours

#### Task 3.1: Analyze Verbatim Parser

**File**: `lambda/input/input-latex.cpp`

**Investigation needed**:
1. How is `\verb` currently parsed?
2. Does tree-sitter grammar support delimiter extraction?
3. Where is the delimiter character stored in AST?

**Debug approach**:
```cpp
// Add logging when parsing \verb command
log_debug("Found verb command");
log_debug("  Total children: %d", ts_node_child_count(verb_node));

TSNode delim_node = ts_node_child(verb_node, 1); // Delimiter should be here
log_debug("  Delimiter node type: %s", ts_node_type(delim_node));

const char* delim_start = source + ts_node_start_byte(delim_node);
log_debug("  Delimiter char: '%c'", *delim_start);
```

#### Task 3.2: Implement Delimiter-Based Parsing

**Expected tree-sitter structure**:
```
verb_command
├── command_name: "verb"
├── delimiter: "|"
├── content: "text"
└── delimiter: "|"
```

**Implementation**:
```cpp
// Parse verb command with arbitrary delimiter
static Element* parse_verb_command(TSNode node, const char* source, Pool* pool) {
    Element* verb_elem = create_element("verb", pool);
    
    // Extract delimiter from first child after command name
    TSNode delim_node = ts_node_child(node, 1);
    const char* delim_char = source + ts_node_start_byte(delim_node);
    
    // Find content between delimiters
    TSNode content_node = ts_node_child(node, 2);
    const char* content_start = source + ts_node_start_byte(content_node);
    uint32_t content_len = ts_node_end_byte(content_node) - ts_node_start_byte(content_node);
    
    // Store content as string (literal, no processing)
    String* content_str = string_new_from_cstr_length(pool, content_start, content_len);
    array_append(verb_elem->items, s2it(content_str), pool);
    
    return verb_elem;
}
```

**Formatter handling** (already exists in format-latex-html.cpp:467):
```cpp
else if (strcmp(cmd_name, "verb") == 0) {
    stringbuf_append_str(html_buf, "<code class=\"latex-verbatim\">");
    process_element_content_simple(html_buf, elem, pool, depth);
    stringbuf_append_str(html_buf, "</code>");
}
```

**Test**:
```bash
./test/test_latex_html_fixtures.exe --gtest_filter="*basic_text_tex_6*"
```

Expected: `<code class="latex-verbatim">verbatim</code>` and `<code class="latex-verbatim">Any{thing</code>`

---

### Phase 4: Final Validation (Week 4)

**Goal**: Ensure all 30 tests pass  
**Impact**: 30/30 tests passing (100%)  
**Effort**: 1-2 hours

#### Task 4.1: Run Full Test Suite

```bash
# Build everything
make build-test

# Run all LaTeX HTML tests
./test/test_latex_html_fixtures.exe

# Check for any remaining failures
./test/test_latex_html_fixtures.exe 2>&1 | grep -E "(PASSED|FAILED|SKIPPED)"
```

#### Task 4.2: Address Edge Cases

If any tests still fail:
1. Examine test output and expected vs actual HTML
2. Add debug logging to identify issue
3. Apply targeted fix
4. Re-test

#### Task 4.3: Performance Testing

```bash
# Test with larger documents
./lambda.exe convert test/input/comprehensive_latex.tex -t html -o /tmp/test_output.html

# Measure conversion time
time ./lambda.exe convert large_document.tex -t html -o /tmp/output.html
```

**Expected**: Sub-second processing for typical documents (< 100KB)

#### Task 4.4: Documentation Update

Update `vibe/Latex_to_Html.md` with:
- Final pass rate: 30/30 (100%) ✅
- All implemented features
- Known limitations (math expressions still excluded)
- Usage examples

---

## Implementation Checklist

### Phase 1: Parser Environment Fix ✅ / ❌
- [ ] Add debug logging to environment parsing
- [ ] Identify tree-sitter node structure for environments
- [ ] Extract environment name from `\begin{envname}`
- [ ] Store environment name as first child element
- [ ] Test with enumerate (should produce `<ol>`)
- [ ] Test with quote (should produce `<div class="latex-quote">`)
- [ ] Test with verbatim (should produce `<pre class="latex-verbatim">`)
- [ ] Test with center/flushleft/flushright
- [ ] Verify 6 tests now pass (environments_tex_2, 3, 4, 5, 6, 7)

### Phase 2: Typography and Metadata ✅ / ❌
- [ ] Implement dash conversion (`--` → `–`, `---` → `—`)
- [ ] Test dash conversion with basic_text_tex_5
- [ ] Fix thin space rendering (`\,` → `&thinsp;`)
- [ ] Test thin space with basic_text_tex_3
- [ ] Add trailing space after special characters
- [ ] Test special character spacing with basic_text_tex_4
- [ ] Implement recursive text extraction helper
- [ ] Update process_title() with recursive extraction
- [ ] Update process_author() with recursive extraction
- [ ] Update process_date() with recursive extraction
- [ ] Test metadata with sectioning_tex_2
- [ ] Verify 3 tests now pass (basic_text_tex_3, 4, sectioning_tex_2)

### Phase 3: Inline Verbatim ✅ / ❌
- [ ] Debug current `\verb` parsing behavior
- [ ] Identify delimiter extraction in tree-sitter
- [ ] Implement delimiter-based content extraction
- [ ] Update verb command parser
- [ ] Test with `\verb|text|`
- [ ] Test with `\verb/text/`
- [ ] Test with `\verb+text+`
- [ ] Verify basic_text_tex_6 passes

### Phase 4: Final Validation ✅ / ❌
- [ ] Run full test suite
- [ ] Verify 30/30 tests pass
- [ ] Performance test with large documents
- [ ] Update documentation
- [ ] Commit changes with clear message
- [ ] Update version/changelog

---

## Risk Assessment and Mitigation

### High Risk: Parser Modifications

**Risk**: Breaking existing functionality when modifying parser  
**Mitigation**:
- Make incremental changes
- Run full test suite after each change
- Use git branches for experimentation
- Add comprehensive logging before modifying logic

### Medium Risk: Text Processing Edge Cases

**Risk**: Dash conversion breaking valid text with double/triple dashes  
**Mitigation**:
- Only convert in text content, not code/verbatim blocks
- Add tests for edge cases
- Consider context (inside verbatim should not convert)

### Low Risk: Performance Regression

**Risk**: Recursive text extraction might be slower  
**Mitigation**:
- Only use recursive extraction for metadata (title/author/date)
- Profile if performance becomes an issue
- Consider caching if needed

---

## Success Criteria

### Must Have
- ✅ 30/30 tests passing (100%)
- ✅ All environments correctly identified and rendered
- ✅ Typography features match LaTeX.js (dashes, spaces)
- ✅ Metadata extraction handles nested formatting

### Should Have
- ✅ Sub-second processing for typical documents
- ✅ Clean, semantic HTML output
- ✅ Comprehensive test coverage
- ✅ Clear documentation

### Nice to Have
- ⭐ Error messages for unsupported features
- ⭐ Graceful degradation for malformed LaTeX
- ⭐ Performance benchmarks documented

---

## Timeline and Milestones

### Week 1: Parser Environment Fix
- **Start**: Day 1
- **Milestone**: Environment name extraction working
- **Deliverable**: 6 environment tests passing (25/30 total)
- **Review**: Ensure all environment types render correctly

### Week 2: Typography and Metadata
- **Start**: Day 8
- **Milestone**: All typography features implemented
- **Deliverable**: 3 additional tests passing (28/30 total)
- **Review**: Visual inspection of rendered HTML for typography quality

### Week 3: Inline Verbatim
- **Start**: Day 15
- **Milestone**: Verbatim delimiter parsing working
- **Deliverable**: 1 additional test passing (29/30 total)
- **Review**: Test with various delimiter characters

### Week 4: Final Validation
- **Start**: Day 22
- **Milestone**: 100% test pass rate achieved
- **Deliverable**: Production-ready converter with full documentation
- **Review**: Code review, performance testing, documentation review

---

## Conclusion

Lambda's LaTeX to HTML converter is **well-architected** and the formatter code is **production-ready**. The path to 100% test compatibility is clear:

1. **Fix the parser** (6 tests) → 83% pass rate
2. **Add typography features** (3 tests) → 93% pass rate
3. **Fix inline verbatim** (1 test) → 97% pass rate
4. **Final validation** → 100% pass rate ✅

The total effort is estimated at 10-17 hours spread over 3-4 weeks, with most of the work focused on fixing the parser's environment extraction logic. Once these targeted fixes are complete, Lambda will have a **fully-featured LaTeX to HTML converter** matching LaTeX.js capabilities for document structure processing.

**Next Steps**:
1. Review this plan with team
2. Begin Phase 1: Parser environment fix
3. Track progress in this document
4. Update `Latex_to_Html.md` upon completion

---

**Document Status**: ✅ Complete  
**Last Updated**: November 19, 2025  
**Author**: AI Analysis / Lambda Development Team
