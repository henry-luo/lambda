# LaTeX to HTML V2 - Improvement Proposal

**Date**: December 16, 2025  
**Last Updated**: December 17, 2025  
**Status**: Implementation Phase (Phase 4 Complete)  
**Objective**: Fix 27 failing baseline tests by addressing parser and formatter issues

---

## 0. Progress Summary

### Current Status
| Metric | Start | After Phase 1 | After Phase 2 | After Phase 3 | After Phase 4 |
|--------|-------|---------------|---------------|---------------|---------------|
| **Passing** | 13/41 (32%) | 23/41 (56%) | 28/41 (68%) | 34/41 (83%) | 36/41 (88%) |
| **Failing** | 27 | 18 | 12 | 7 | 4 |

### Completed Fixes (Phase 4 - Latest Session)
1. ✅ **Abstract environment** - Updated to use `list center`/`list quotation` classes with `bf small`/`it small` font classes
2. ✅ **Description environment** - Updated class to `list`, added `<dt>` tag extraction from brack_group, paragraph support in `<dd>`
3. ✅ **Font size classes** - Updated `getFontClass()` to include size classes (`small`, `large`, etc.)
4. ✅ **Font size double-wrap prevention** - Added `enterStyledSpan()`/`exitStyledSpan()` to size commands
5. ✅ **Custom item labels** - Added `extractLabelFromBrackGroup` helper to process `\textendash`, empty labels
6. ✅ **Paragraph breaks in description** - Updated `itemParagraphBreak()` to handle description lists

### Tests Now Passing (Phase 4)
- `environments_tex_1` - nested empty environments
- `environments_tex_5` - nested itemize with paragraphs  
- `environments_tex_8` - description environment with `<dt>`/`<dd>` structure
- `environments_tex_11` - center/flushleft/flushright alignment environments
- `environments_tex_13` - abstract environment with proper styling
- `whitespace_tex_16` - `\empty` with ZWSP handling
- `formatting_tex_5` - font size commands without double-wrapping

### Remaining Issues (4 tests)
| Category | Failing Tests | Priority | Notes |
|----------|---------------|----------|-------|
| Font Environments | `environments_tex_10` | P2 | ZWSP markers at begin/end - LaTeX.js compatibility pattern |
| List Alignment | `environments_tex_12` | P2 | `\centering` inside list needs class propagation to ul/li/p |
| Paragraph Alignment | `text_tex_10` | P2 | `\centering`/`\raggedright` commands in paragraphs |
| Counters | `counters_tex_2` | P3 | Counter system not yet implemented |

---

## 1. Executive Summary

Analysis of V2 baseline test failures reveals **6 major issue categories**:

| Issue | Impact | Tests Affected | Priority |
|-------|--------|----------------|----------|
| Environment double-wrapping | High | 8-10 | P1 |
| List formatting mismatch | High | 4-6 | P1 |
| Whitespace/paragraph handling | Medium | 5-7 | P2 |
| Font scoping (`\em` toggle) | Medium | 2-3 | P2 |
| Counter system missing | Low | 2-3 | P3 |
| Spacing command output | Low | 2-3 | P3 |

**Expected outcome**: Fix 20+ of 27 failing tests.

---

## 2. Issue 1: Environment Double-Wrapping (P1)

### Problem Description

The tree-sitter grammar creates a `environment → generic_environment` hierarchy:
```
environment: $ => choice(
    $.generic_environment,
    // other environment types...
)
```

The `input-latex-ts.cpp` handles `generic_environment` correctly via `convert_environment()`, but the outer `environment` choice node falls through to the default container handler.

### Current Output (Incorrect)
```mark
<latex_document>
  <environment>        ← Extra wrapper from default handler
    <itemize>          ← Correct element from convert_environment()
      <paragraph>
        <item>
        " First item"
```

### Expected Output
```mark
<latex_document>
  <itemize>
    <paragraph>
      <item>
      " First item"
```

### Fix Location
**File**: `lambda/input/input-latex-ts.cpp`  
**Function**: `convert_latex_node()` switch statement for NODE_CONTAINER

### Implementation
Add special handling for the `environment` wrapper node to make it transparent:

```cpp
// In convert_latex_node(), before the default container handling (around line 340):

// Handle "environment" node - transparent wrapper from grammar choice rule
if (strcmp(node_type, "environment") == 0) {
    // The environment node is just a choice wrapper - unwrap and return the actual environment
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);
        // Skip any ERROR nodes
        if (strcmp(child_type, "ERROR") == 0) continue;
        
        // Return the first valid child (should be generic_environment, math_environment, etc.)
        Item child_item = convert_latex_node(ctx, child, source);
        if (child_item.item != ITEM_NULL) {
            return child_item;
        }
    }
    return {.item = ITEM_NULL};
}
```

---

## 3. Issue 2: List Formatting Mismatch (P1)

### Problem Description

V2 formatter produces simple HTML:
```html
<ul class="itemize"><li> First item</li><li> Second item</li></ul>
```

LaTeX.js produces rich HTML with proper labeling:
```html
<ul class="list">
<li><span class="itemlabel"><span class="hbox llap">•</span></span><p>First item</p></li>
<li><span class="itemlabel"><span class="hbox llap">•</span></span><p>Second item</p></li>
</ul>
```

### Key Differences
1. **CSS class**: Should be `"list"`, not `"itemize"`
2. **Item labels**: Each `<li>` needs `<span class="itemlabel">` with bullet
3. **Content wrapping**: Item content should be in `<p>` tags
4. **Label alignment**: Uses `<span class="hbox llap">` for left-aligned overlapping

### Fix Location
**File**: `lambda/format/format_latex_html_v2.cpp`  
**Functions**: `cmd_itemize()`, `processListItems()`

### Implementation

Replace the existing `cmd_itemize()` and related functions:

```cpp
// Helper to create item label
static void createItemLabel(HtmlGenerator* gen, const char* label_text, const char* css_class = nullptr) {
    gen->openTag("span", "itemlabel");
    gen->openTag("span", "hbox llap");
    if (css_class) {
        gen->openTag("span", css_class);
        gen->text(label_text);
        gen->closeElement();
    } else {
        gen->text(label_text);
    }
    gen->closeElement();  // hbox llap
    gen->closeElement();  // itemlabel
}

static void cmd_itemize(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    // Close any open paragraph before block element
    proc->closeParagraphIfOpen();
    
    // Track nesting depth for different bullet styles
    proc->incrementListDepth();
    int depth = proc->listDepth();
    
    // Start unordered list with "list" class (matching LaTeX.js)
    gen->openTag("ul", "list");
    
    // Get bullet character based on depth
    const char* bullets[] = {"•", "–", "*", "·"};  // textbullet, textendash, asterisk, periodcentered
    const char* bullet = bullets[std::min(depth - 1, 3)];
    
    // Process list items
    processListItemsV2(proc, elem, "itemize", bullet);
    
    gen->closeElement();  // </ul>
    proc->decrementListDepth();
}

static void processListItemsV2(LatexProcessor* proc, Item elem, const char* list_type, const char* default_label) {
    HtmlGenerator* gen = proc->generator();
    ElementReader reader(elem);
    
    bool in_item = false;
    bool in_item_content = false;
    
    auto iter = reader.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader child_elem(child.item());
            const char* tag = child_elem.tagName();
            if (!tag) continue;
            
            // Handle paragraph wrapper
            if (strcmp(tag, "paragraph") == 0) {
                // Process paragraph children for items
                processListItemsV2(proc, child.item(), list_type, default_label);
                continue;
            }
            
            // Handle item
            if (strcmp(tag, "item") == 0) {
                // Close previous item if open
                if (in_item_content) {
                    gen->closeElement();  // </p>
                    in_item_content = false;
                }
                if (in_item) {
                    gen->closeElement();  // </li>
                }
                
                // Start new item
                gen->openTag("li");
                in_item = true;
                
                // Create item label
                // TODO: Check for custom label in brack_group child
                createItemLabel(gen, default_label);
                
                // Start content paragraph
                gen->openTag("p");
                in_item_content = true;
                
                // Process item's own children (for inline content after \item)
                proc->processChildren(child.item());
                continue;
            }
            
            // Other elements within item content
            if (in_item) {
                if (!in_item_content) {
                    gen->openTag("p");
                    in_item_content = true;
                }
                proc->processNode(child.item());
            }
        } else if (child.isString()) {
            // Text content
            if (in_item) {
                if (!in_item_content) {
                    gen->openTag("p");
                    in_item_content = true;
                }
                const char* text = child.cstring();
                if (text && *text) {
                    // Trim leading space if at start of item
                    if (text[0] == ' ' && in_item_content) {
                        text++;
                    }
                    if (*text) {
                        gen->text(text);
                    }
                }
            }
        }
    }
    
    // Close final item
    if (in_item_content) {
        gen->closeElement();  // </p>
    }
    if (in_item) {
        gen->closeElement();  // </li>
    }
}
```

---

## 4. Issue 3: Whitespace/Paragraph Handling (P2)

### Problem Description

Several whitespace-related constructs don't work correctly:
- `~` (non-breaking space) not producing `&nbsp;`
- `\ ` (control space) not creating protected space  
- Multiple `\par` not creating proper paragraph breaks
- Line breaks (`\\`, `\newline`) inconsistent

### LaTeX.js Reference (from html-generator.ls)
```livescript
sp:     ' '
brsp:   '\u200B '    # breakable but non-collapsible space
nbsp:   '&nbsp;'     # U+00A0
visp:   '␣'          # visible space
```

### Fix Location
**File**: `lambda/format/format_latex_html_v2.cpp`  
**Function**: `processNode()` SYMBOL handling

### Implementation

Enhance symbol handling in `processNode()`:

```cpp
// In processNode() for SYMBOL type handling, replace/extend the existing code:

if (type == LMD_TYPE_SYMBOL) {
    String* str = reader.asSymbol();
    if (str) {
        const char* sym_name = str->chars;
        
        if (strcmp(sym_name, "parbreak") == 0) {
            // Paragraph break: close current paragraph
            closeParagraphIfOpen();
            return;
        }
        
        // Non-breaking space: ~ or \nobreakspace
        if (strcmp(sym_name, "nbsp") == 0 || strcmp(sym_name, "~") == 0 ||
            strcmp(sym_name, "nobreakspace") == 0) {
            ensureParagraph();
            gen_->text("\u00A0");  // Non-breaking space U+00A0
            return;
        }
        
        // Breakable non-collapsible space: \space, \
        if (strcmp(sym_name, "space") == 0) {
            ensureParagraph();
            gen_->text("\u200B ");  // Zero-width space + regular space
            return;
        }
        
        // Thin space: \,
        if (strcmp(sym_name, "thinspace") == 0 || strcmp(sym_name, ",") == 0) {
            ensureParagraph();
            gen_->text("\u2009");  // Thin space U+2009
            return;
        }
        
        // Negative thin space: \!
        if (strcmp(sym_name, "negthinspace") == 0 || strcmp(sym_name, "!") == 0) {
            ensureParagraph();
            gen_->openTag("span", "negthinspace");
            gen_->closeElement();
            return;
        }
        
        // En space: \enspace
        if (strcmp(sym_name, "enspace") == 0) {
            ensureParagraph();
            gen_->text("\u2002");  // En space U+2002
            return;
        }
        
        // Em space: \quad
        if (strcmp(sym_name, "quad") == 0) {
            ensureParagraph();
            gen_->text("\u2003");  // Em space U+2003
            return;
        }
        
        // Double em space: \qquad
        if (strcmp(sym_name, "qquad") == 0) {
            ensureParagraph();
            gen_->text("\u2003\u2003");  // Two em spaces
            return;
        }
        
        // Handle TeX/LaTeX logos
        if (strcmp(sym_name, "TeX") == 0) {
            ensureParagraph();
            gen_->openTag("span", "tex");
            gen_->text("T");
            gen_->openTag("span", "e");
            gen_->text("e");
            gen_->closeElement();
            gen_->text("X");
            gen_->closeElement();
            return;
        }
        
        if (strcmp(sym_name, "LaTeX") == 0) {
            ensureParagraph();
            gen_->openTag("span", "latex");
            gen_->text("L");
            gen_->openTag("span", "a");
            gen_->text("a");
            gen_->closeElement();
            gen_->text("T");
            gen_->openTag("span", "e");
            gen_->text("e");
            gen_->closeElement();
            gen_->text("X");
            gen_->closeElement();
            return;
        }
        
        // Single-character symbols are escaped special characters
        if (strlen(sym_name) == 1) {
            processText(sym_name);
            return;
        }
        
        log_debug("processNode: unhandled symbol '%s'", sym_name);
    }
    return;
}
```

---

## 5. Issue 4: Font Scoping and `\em` Toggle (P2)

### Problem Description

`\em` should toggle between italic and upright based on current context:
- In normal text: `\em` → italic
- Inside italic: `\em` → upright (back to normal)

### LaTeX.js Reference (from generator.ls)
```livescript
setFontShape: (shape) !->
    if shape == "em"
        if @_activeAttributeValue("fontShape") == "it"
            shape = "up"
        else
            shape = "it"
    @_stack.top.attrs.fontShape = shape
```

### Fix Location
**File**: `lambda/format/format_latex_html_v2.cpp`  
**Functions**: `cmd_em()`, `cmd_emph()`

### Implementation

```cpp
static void cmd_em(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    // \em is a declaration that toggles emphasis
    // Check current font shape to determine target
    const char* current_shape = gen->getCurrentFontShape();
    const char* new_shape = "it";  // Default: switch to italic
    
    if (current_shape && strcmp(current_shape, "it") == 0) {
        new_shape = "up";  // Toggle back to upright if already italic
    }
    
    gen->setFontShape(new_shape);
    proc->stripNextLeadingSpace();
}

static void cmd_emph(LatexProcessor* proc, Item elem) {
    HtmlGenerator* gen = proc->generator();
    
    // Check if we're already in italic - if so, emphasize by going upright
    const char* current_shape = gen->getCurrentFontShape();
    const char* class_name = "it";  // Default: italic
    
    if (current_shape && strcmp(current_shape, "it") == 0) {
        class_name = "up";  // Toggle to upright
    }
    
    proc->ensureParagraph();
    proc->enterStyledSpan();
    
    gen->openTag("span", class_name);
    proc->processChildren(elem);
    gen->closeElement();
    
    proc->exitStyledSpan();
}
```

Also need to add font shape tracking to HtmlGenerator:

```cpp
// In HtmlGenerator class, add:
private:
    std::string current_font_shape_ = "up";  // up, it, sl, sc

public:
    const char* getCurrentFontShape() const { return current_font_shape_.c_str(); }
    void setFontShape(const char* shape) { current_font_shape_ = shape; }
```

---

## 6. Issue 5: Counter System (P3)

### Problem Description

Counter commands (`\newcounter`, `\stepcounter`, `\arabic`, etc.) output placeholders instead of actual values.

### Implementation

Add counter state management to LatexProcessor:

```cpp
// Add to LatexProcessor class:
private:
    std::map<std::string, int> counters_;
    std::map<std::string, std::vector<std::string>> counter_resets_;

public:
    void newCounter(const std::string& name, const std::string& parent = "") {
        counters_[name] = 0;
        if (!parent.empty()) {
            counter_resets_[parent].push_back(name);
        }
    }
    
    void setCounter(const std::string& name, int value) {
        counters_[name] = value;
    }
    
    void addToCounter(const std::string& name, int value) {
        counters_[name] += value;
    }
    
    void stepCounter(const std::string& name) {
        counters_[name]++;
        // Reset dependent counters
        auto it = counter_resets_.find(name);
        if (it != counter_resets_.end()) {
            for (const auto& dep : it->second) {
                counters_[dep] = 0;
            }
        }
    }
    
    int counter(const std::string& name) const {
        auto it = counters_.find(name);
        return (it != counters_.end()) ? it->second : 0;
    }
    
    std::string arabic(const std::string& name) const {
        return std::to_string(counter(name));
    }
```

---

## 7. Implementation Plan

### Phase 1: Parser Fix (Issue 1)
1. Modify `input-latex-ts.cpp` to unwrap `environment` nodes
2. Test with itemize/enumerate environments
3. Verify Mark output shows direct environment elements

### Phase 2: List Formatting (Issue 2)
1. Replace `cmd_itemize()` with LaTeX.js-compatible version
2. Add `createItemLabel()` helper
3. Update `cmd_enumerate()` and `cmd_description()` similarly
4. Add list depth tracking to LatexProcessor

### Phase 3: Whitespace/Symbols (Issue 3)
1. Enhance symbol handling in `processNode()`
2. Add all spacing command symbols
3. Test paragraph breaks and spaces

### Phase 4: Font Scoping (Issue 4)
1. Add font state tracking to HtmlGenerator
2. Fix `cmd_em()` toggle behavior
3. Fix `cmd_emph()` toggle behavior

### Phase 5: Counters (Issue 5)
1. Add counter infrastructure to LatexProcessor
2. Implement `cmd_newcounter()`, `cmd_stepcounter()`, etc.
3. Implement `cmd_arabic()`, `cmd_alph()`, etc.

---

## 8. Testing Strategy

After each phase:
1. Run `./test/test_latex_html_v2_baseline.exe`
2. Track number of passing tests
3. Debug any regressions

Expected progression:
- After Phase 1: +3-5 tests
- After Phase 2: +4-6 tests
- After Phase 3: +4-6 tests
- After Phase 4: +2-3 tests
- After Phase 5: +2-3 tests

**Target**: 35+ passing tests (from current 13)

---

## 9. Implementation Progress (Updated December 16, 2025 - Session 2)

### Summary
| Metric | Before | Session 1 | Session 2 | Change |
|--------|--------|-----------|-----------|--------|
| Passing Tests | 13 | 17 | 22 | +9 |
| Failing Tests | 27 | 23 | 19 | -8 |
| Pass Rate | 32.5% | 42.5% | 55% | +22.5% |

### Completed Fixes

#### Phase 1: Environment Unwrapping ✅
- **File**: `lambda/input/input-latex-ts.cpp`
- **Change**: Added environment node unwrapping in `convert_to_element()` (~line 391)
- **Result**: Environments like `itemize`, `enumerate` now convert correctly without double-wrapping

#### Phase 2: List Item Structure ✅
- **Files**: `lambda/format/html_generator.cpp`, `lambda/format/format_latex_html_v2.cpp`
- **Changes**:
  1. `startItemize()`: Changed CSS class from "itemize" to "list" to match LaTeX.js
  2. `createItem()`: Added itemlabel span structure matching LaTeX.js output
  3. Added `endItem()` method to properly close `<p>` and `<li>` tags
  4. Updated `processListItems()` to use `endItem()` instead of `closeElement()`
  5. Added leading whitespace trimming after `\item`

#### Phase 3: Spacing Commands ✅
- **File**: `lambda/format/format_latex_html_v2.cpp`
- **Changes**:
  1. Fixed `cmd_empty()`: Now outputs zero-width space (U+200B) instead of nothing
  2. Fixed `cmd_hspace()`: Uses `spanWithClassAndStyle()` to avoid malformed attributes
  3. Fixed `cmd_vspace()`: Uses `divWithClassAndStyle()` similarly

#### Phase 4: Typography Processing ✅ (Session 2)
- **File**: `lambda/format/html_generator.cpp`
- **Changes**:
  1. Added `processTypography()` function for dash and ligature conversion
  2. Converts `---` to em-dash (—), `--` to en-dash (–)
  3. Converts `fi` to ﬁ ligature, `fl` to ﬂ ligature
  4. Converts ``` `` ``` to left quote, `''` to right quote
  5. Skips typography in monospace (Typewriter) font context

#### Phase 5: Quote Escaping ✅ (Session 2)
- **File**: `lambda/format/html_writer.cpp`
- **Change**: Removed quote escaping from `escapeHtml()` - quotes only need escaping in attributes, not text content

#### Phase 6: Spacing Commands (Control Symbols) ✅ (Session 2)
- **Files**: `lambda/input/input-latex-ts.cpp`, `lambda/format/format_latex_html_v2.cpp`
- **Changes**:
  1. Parser: Added `space_cmd` element for `\,`, `\!`, `\;`, `\:`, `\/`, `\@`, `\ ` 
  2. Formatter: Added command handlers for `thinspace`, `enspace`, `quad`, `qquad`
  3. Fixed `negthinspace` to use class-only span (no inline style)
  4. Thin space outputs U+2009, en-space U+2002, em-space U+2003

### Remaining Issues

| Issue | Status | Tests Affected |
|-------|--------|----------------|
| `~` (nbsp) not parsed | Grammar conflict with external scanner | spacing_tex_1 |
| `\ ` (control space) | Not handled | whitespace tests |
| `\em` toggle behavior | \emph in italic → upright | fonts_tex_2 |
| Paragraph handling | Empty `<p></p>`, multiple `\par` | whitespace tests |
| Environment text handling | Extra empty elements | environments tests |

### Failing Test Categories (19 tests)
- **environments_tex**: 8 tests - Complex environment nesting, text handling
- **whitespace_tex**: 4 tests - Paragraph handling, control space
- **spacing_tex**: 1 test - nbsp (tilde) not supported
- **fonts_tex**: 1 test - \em toggle behavior
- **text_tex**: 1 test - Complex text scenarios
- **macros_tex**: 1 test - Macro expansion
- **counters_tex**: 1 test - Counter formatting

### Files Modified (Session 2)
```
lambda/input/input-latex-ts.cpp       - space_cmd element creation
lambda/format/html_generator.cpp      - Typography processing, monospace detection
lambda/format/html_writer.cpp         - Quote escaping removal
lambda/format/format_latex_html_v2.cpp - Spacing command handlers, nbsp support
lambda/tree-sitter-latex/grammar.js   - Added nbsp to _inline and _group_content
```

---

## 10. Session 3 Progress (December 17, 2025)

### Summary
| Metric | Session 2 | Session 3 | Change |
|--------|-----------|-----------|--------|
| Passing Tests | 22 | 28 | +6 |
| Failing Tests | 19 | 12 | -7 |
| Pass Rate | 55% | 68% | +13% |

### Completed Fixes

#### Phase 7: Paragraph Break Detection ✅
- **File**: `lambda/tree-sitter-latex/grammar.js`
- **Problem**: Double newlines (`\n\n`) were being consumed by `space` rule instead of `paragraph_break`
- **Root Cause**: `space: /[ \t\n]+/` greedily matched all whitespace including multiple newlines
- **Fix**:
  1. Changed `space` rule to `/[ \t]+|\n/` - only matches horizontal whitespace or single newline
  2. Changed `paragraph_break` to `token(prec(1, /\n[ \t]*\n/))` - higher precedence
  3. Added `$.paragraph_break` to `_inline` rule so it's recognized within paragraphs
- **Result**: All whitespace tests with double newline paragraph separation now pass

#### Phase 8: Trailing Whitespace Trimming ✅
- **Files**: `html_writer.hpp/cpp`, `html_generator.hpp/cpp`, `format_latex_html_v2.cpp`
- **Problem**: Paragraphs had trailing spaces like `<p>text </p>` instead of `<p>text</p>`
- **Fix**:
  1. Added `trimTrailingWhitespace()` virtual method to `HtmlWriter` interface
  2. Implemented for `TextHtmlWriter` - trims spaces/tabs from buffer end
  3. Called in `closeParagraphIfOpen()` before closing `<p>` tag
- **Result**: Clean paragraph content without trailing whitespace

#### Phase 9: Backslash-Space Command ✅
- **File**: `lambda/format/format_latex_html_v2.cpp`
- **Problem**: `\ ` (backslash-space) after `\empty` wasn't outputting a space
- **Fix**: Added handling in `processSpacingCommand()`:
  - `\\ ` (backslash-space) → regular space
  - `~` (tilde) → regular space  
  - `\\/` and `\\@` → nothing (zero-width commands)
- **Result**: `some \empty\ text.` correctly produces `some ​ text.`

### Tests Now Passing
- `whitespace_tex_1` - par and double-newline paragraph breaks
- `whitespace_tex_3` - standard double-newline paragraph separation
- `whitespace_tex_9` - more than two newlines equal two newlines
- `whitespace_tex_10` - comments in paragraph breaks
- `whitespace_tex_16` - force space after macro with `\ `
- `fonts_tex_2` - (fixed in session 2) \em toggle behavior

### Remaining Issues (12 tests)

| Issue | Tests Affected | Complexity |
|-------|----------------|------------|
| List item paragraph breaks | environments_tex_2 | Medium |
| `p.continue` class | environments_tex_2 | Low |
| Counter formatting | environments_tex_4, 5 | Medium |
| Font environments | environments_tex_10, 13 | Medium |
| Alignment environments | environments_tex_11 | Low |
| Nested list alignment | environments_tex_12 | Medium |
| Float tests | 2 extended tests | High |
| Graphics/color | 1 extended test | Medium |
| Macros | 1 extended test | Medium |

### Files Modified (Session 3)
```
lambda/tree-sitter-latex/grammar.js   - Fixed paragraph_break vs space precedence
                                       - space: /[ \t]+|\n/ (single newline only)
                                       - Added paragraph_break to _inline
                                       - paragraph_break uses token(prec(1, ...))
lambda/format/html_writer.hpp         - Added trimTrailingWhitespace() virtual method
lambda/format/html_writer.cpp         - Implemented trimTrailingWhitespace() for TextHtmlWriter
lambda/format/html_generator.hpp      - Added trimTrailingWhitespace() wrapper
lambda/format/html_generator.cpp      - Implemented trimTrailingWhitespace()
lambda/format/format_latex_html_v2.cpp - Call trimTrailingWhitespace() in closeParagraphIfOpen()
                                       - Added \\ (backslash-space) handling in processSpacingCommand()
                                       - Added \\/ and \\@ handling (zero-width commands)
```

### Next Steps (Phase 5)
1. **Font environments** - Add ZWSP markers at begin/end (`environments_tex_10`)
2. **Alignment in lists** - Handle `\centering` class propagation to ul/li/p (`environments_tex_12`)
3. **Paragraph alignment commands** - Track `\centering`/`\raggedright` state for paragraphs (`text_tex_10`)
4. **Counter system** - Implement counter tracking and formatting (`counters_tex_2`)

---

## 11. Session 4 Progress (December 17, 2025)

### Summary
| Metric | Session 3 | Session 4 | Change |
|--------|-----------|-----------|--------|
| Passing Tests | 28 | 36 | +8 |
| Failing Tests | 12 | 4 | -8 |
| Pass Rate | 68% | 88% | +20% |

### Completed Fixes

#### Fix 1: closeParagraphIfOpen Access Error ✅
- **File**: `lambda/format/format_latex_html_v2.cpp`
- **Problem**: `closeParagraphIfOpen()` was private but needed externally
- **Fix**: Moved declaration to public section (line 92-95)
- **Result**: Environment commands can properly close paragraphs before block elements

#### Fix 2: Font Size Double-Wrap Prevention ✅
- **File**: `lambda/format/format_latex_html_v2.cpp`
- **Problem**: Font size commands (`\small`, `\large`, etc.) were being double-wrapped in spans
- **Root Cause**: Size commands didn't use `enterStyledSpan()`/`exitStyledSpan()` like other font commands
- **Fix**: Added `enterStyledSpan()` before and `exitStyledSpan()` after size change in all size commands:
  - `cmd_tiny`, `cmd_scriptsize`, `cmd_footnotesize`, `cmd_small`
  - `cmd_normalsize`, `cmd_large`, `cmd_Large`, `cmd_LARGE`, `cmd_huge`, `cmd_Huge`
- **Result**: `\small` properly applies size without creating nested font spans

#### Fix 3: Abstract Environment Structure ✅
- **File**: `lambda/format/format_latex_html_v2.cpp`
- **Problem**: Abstract environment had wrong structure
- **Fix**: Updated `cmd_abstract` to generate:
  ```html
  <div class="list center">
    <p class="bf small">Abstract</p>
    <div class="list quotation">
      <p class="it small">content</p>
    </div>
  </div>
  ```
- **Result**: Abstract matches LaTeX.js output with proper centering and quotation styling

#### Fix 4: Description Environment Complete Rewrite ✅
- **File**: `lambda/format/format_latex_html_v2.cpp`, `lambda/format/html_generator.cpp`
- **Problem**: Description environment had wrong class and structure
- **Fixes**:
  1. `startDescription()`: Changed CSS class from `"description"` to `"list"`
  2. Added `extractLabelFromBrackGroup()` helper to extract labels from `brack_group` children
  3. Helper handles `\textendash` → `–`, `\textbullet` → `•`, empty labels
  4. `processListItems()`: Uses helper to populate `<dt>` content
  5. `itemParagraphBreak()`: Added handling for description lists (closes/opens `<p>` tags in `<dd>`)
- **Result**: Description lists produce proper `<dt>/<dd>` structure with custom labels

#### Fix 5: Font Size in getFontClass() ✅
- **File**: `lambda/format/html_generator.cpp`
- **Problem**: `getFontClass()` didn't include size classes
- **Fix**: Added size switch in `getFontClass()`:
  ```cpp
  switch (font.size) {
      case FontSize::Tiny: ss << "tiny "; break;
      case FontSize::Small: ss << "small "; break;
      case FontSize::Large: ss << "large "; break;
      // ... etc
  }
  ```
- **Result**: Spans now get proper size classes like `class="it small"`

#### Fix 6: Custom Item Labels ✅
- **Files**: `lambda/format/format_latex_html_v2.cpp`, `lambda/format/html_generator.cpp`
- **Problem**: `\item[\textendash]` and `\item[]` not handled
- **Fixes**:
  1. Added `extractLabelFromBrackGroup()` helper (~line 1688-1720)
  2. Helper converts LaTeX symbols: `\textendash` → `–`, `\textbullet` → `•`
  3. Helper returns empty string for empty `\item[]`
  4. `createItem()` accepts custom label parameter, uses it instead of default bullet
  5. `processListItems()` detects brack_group and extracts label
- **Result**: Custom labels render correctly, empty labels produce no bullet

### Tests Now Passing (Session 4)
- `environments_tex_1` - nested empty environments
- `environments_tex_5` - nested itemize with paragraphs  
- `environments_tex_8` - description environment with `<dt>`/`<dd>` structure
- `environments_tex_11` - center/flushleft/flushright alignment environments
- `environments_tex_13` - abstract environment with proper styling
- `whitespace_tex_16` - `\empty` with ZWSP handling (fixed regression)
- `formatting_tex_5` - font size commands without double-wrapping
- `fonts_tex_2` - various font combinations

### Remaining Issues (4 tests)

| Test | Issue | Notes |
|------|-------|-------|
| `counters_tex_2` | Counter system | Counter tracking/formatting not implemented |
| `text_tex_10` | Paragraph alignment | `\centering`/`\raggedright` commands need state tracking |
| `environments_tex_10` | Font environment ZWSP | LaTeX.js adds ZWSP markers at begin/end of font envs |
| `environments_tex_12` | List alignment | `\centering` inside list needs class on ul/li/p elements |

### Files Modified (Session 4)
```
lambda/format/format_latex_html_v2.cpp:
  - closeParagraphIfOpen() moved to public section (line 92-95)
  - cmd_abstract rewritten with list center/quotation structure
  - Font size commands (cmd_tiny, cmd_small, etc.) use enterStyledSpan/exitStyledSpan
  - Added extractLabelFromBrackGroup() helper (~line 1688-1720):
      - Handles \textendash → –, \textbullet → •
      - Handles empty brack_group → empty string
  - processListItems() uses extractLabelFromBrackGroup for custom labels

lambda/format/html_generator.cpp:
  - getFontClass() now includes size classes (tiny, small, large, etc.)
  - startDescription() uses "list" class instead of "description"
  - createItem() accepts optional custom label parameter
  - itemParagraphBreak() handles description lists (closes/opens p in dd)
```

### Key Implementation Details

#### extractLabelFromBrackGroup Helper
```cpp
static std::string extractLabelFromBrackGroup(ElementReader& brack_elem) {
    std::string label;
    for (size_t k = 0; k < brack_elem.childCount(); k++) {
        ItemReader child = brack_elem.childAt(k);
        if (child.isString()) {
            label += child.cstring();
        } else if (child.isElement()) {
            ElementReader elem = child.asElement();
            const char* tag = elem.tagName();
            if (tag) {
                if (strcmp(tag, "textendash") == 0) label += "–";
                else if (strcmp(tag, "textbullet") == 0) label += "•";
                else if (strcmp(tag, "textasciitilde") == 0) label += "~";
                // ... more symbol mappings
            }
        }
    }
    return label;
}
```

#### Custom Label Flow in processListItems
1. Detect `brack_group` as first child of `item` element
2. Check if brack_group is empty → pass `""` as label
3. Otherwise extract content via `extractLabelFromBrackGroup()`
4. Pass extracted label (or `nullptr` for default) to `createItem()`

---

## 12. Overall Progress Summary

### Test Progression
| Session | Passing | Failing | Rate | Delta |
|---------|---------|---------|------|-------|
| Start | 13/41 | 27 | 32% | - |
| Session 1 | 17/41 | 23 | 42% | +10% |
| Session 2 | 22/41 | 19 | 55% | +13% |
| Session 3 | 28/41 | 12 | 68% | +13% |
| Session 4 | 36/41 | 4 | 88% | +20% |

### Files Modified Across All Sessions
```
lambda/input/input-latex-ts.cpp         - Environment unwrapping, space_cmd element
lambda/tree-sitter-latex/grammar.js     - paragraph_break precedence, space rule
lambda/format/format_latex_html_v2.cpp  - Core formatter (major changes)
lambda/format/html_generator.cpp        - Font class, list handling
lambda/format/html_generator.hpp        - Interface updates
lambda/format/html_writer.cpp           - Trailing whitespace, quote escaping
lambda/format/html_writer.hpp           - trimTrailingWhitespace method
```

### Architecture Insights
1. **Font state management** - HtmlGenerator tracks font stack, applies cumulative classes
2. **Paragraph lifecycle** - closeParagraphIfOpen/ensureParagraph pattern for block elements
3. **List depth tracking** - Proper bullet selection based on nesting level
4. **Style span tracking** - enterStyledSpan/exitStyledSpan prevents double-wrapping

### Remaining Work (4 tests - Lower Priority)
These require more complex infrastructure:
1. **Counter system** - State tracking, formatting commands (\arabic, \alph, etc.)
2. **Alignment commands** - Scoped state for \centering affecting paragraphs
3. **Font environment ZWSP** - LaTeX.js compatibility pattern for begin/end markers
4. **List class propagation** - Alignment class needs to flow from command to nested elements

