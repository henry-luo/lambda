# HTML5 Parser Implementation Plan

**Target**: Full HTML5 specification compliance for `lambda/input/input-html.cpp`

**Current State**: Basic parsing with limited error recovery and HTML5 features

**Goal**: Implement the complete HTML5 parsing algorithm with tree construction, error recovery, and all spec-defined features

---

## Phase 1: Foundation & Architecture (2-3 weeks)

### 1.1 Parser State Machine Infrastructure
**Priority**: CRITICAL
**Effort**: Medium
**Dependencies**: None

- [ ] Create `HtmlParserState` enum with all insertion modes:
  - `INITIAL`, `BEFORE_HTML`, `BEFORE_HEAD`, `IN_HEAD`, etc.
  - All 24 insertion modes from HTML5 spec
- [ ] Add `Parser` context structure:
  ```c++
  struct HtmlParser {
      Input* input;
      const char* html_start;
      const char* html_current;
      HtmlParserState state;
      Stack* open_elements;
      List* active_formatting_elements;
      Element* head_element;
      Element* form_element;
      bool scripting_enabled;
      bool foster_parenting;
      bool frameset_ok;
  };
  ```
- [ ] Implement stack operations for open elements
- [ ] Implement list operations for active formatting elements
- [ ] Add state transition logging for debugging

**Testing**: Unit tests for state transitions and stack operations

---

### 1.2 Tokenizer Separation
**Priority**: HIGH
**Effort**: Medium
**Dependencies**: None

- [ ] Split parsing into tokenizer + tree construction phases
- [ ] Create token types: `DOCTYPE`, `START_TAG`, `END_TAG`, `COMMENT`, `CHARACTER`, `EOF`
- [ ] Implement tokenizer state machine (13 states from spec)
- [ ] Add token buffer/queue for lookahead
- [ ] Preserve token source locations for error reporting

**Testing**: Tokenizer tests with all token types and edge cases

---

## Phase 2: Core HTML5 Parsing Algorithm (3-4 weeks)

### 2.1 Implicit Element Creation
**Priority**: HIGH
**Effort**: Medium
**Dependencies**: 1.1

- [ ] Implement implicit `<html>` creation
- [ ] Implement implicit `<head>` creation
- [ ] Implement implicit `<body>` creation
- [ ] Add auto-closing for `<p>` when block elements encountered
- [ ] Add auto-closing for `<li>` in lists
- [ ] Add proper `<colgroup>` handling

**Testing**:
- Test documents without `<html>`, `<head>`, `<body>`
- Test nested block elements in `<p>`
- Test list item auto-closing

---

### 2.2 Insertion Mode Implementation
**Priority**: HIGH
**Effort**: Large
**Dependencies**: 1.1, 1.2, 2.1

Implement each insertion mode sequentially:

1. [ ] **Initial Mode** (1 day)
   - Handle whitespace, comments, DOCTYPE
   - Transition to "before html"

2. [ ] **Before HTML Mode** (1 day)
   - Create implicit `<html>` element
   - Handle start/end tags

3. [ ] **Before Head Mode** (1 day)
   - Create implicit `<head>` element
   - Handle head-related tags

4. [ ] **In Head Mode** (2-3 days)
   - Handle `<meta>`, `<title>`, `<base>`, `<link>`, `<style>`, `<script>`
   - Implement raw text parsing for `<script>`, `<style>`

5. [ ] **In Head Noscript Mode** (1 day)
   - Handle `<noscript>` content based on scripting flag

6. [ ] **After Head Mode** (1 day)
   - Transition to "in body"
   - Create implicit `<body>`

7. [ ] **In Body Mode** (5-7 days) - MOST COMPLEX
   - Handle all content elements
   - Implement "scope" checking
   - Handle formatting elements
   - Implement "have an element in scope" algorithm
   - Handle all block and inline elements
   - Implement proper nesting rules

8. [ ] **Text Mode** (1 day)
   - For raw text elements (script, style, textarea, title)

9. [ ] **In Table Mode** (2-3 days)
   - Foster parenting for misnested content
   - Proper table structure validation
   - Handle caption, colgroup, tbody, thead, tfoot

10. [ ] **In Table Text Mode** (1 day)
    - Pending table character tokens

11. [ ] **In Caption Mode** (1 day)
    - Table caption handling

12. [ ] **In Column Group Mode** (1 day)
    - Column and column group handling

13. [ ] **In Table Body Mode** (1 day)
    - tbody, thead, tfoot content

14. [ ] **In Row Mode** (1 day)
    - Table row content

15. [ ] **In Cell Mode** (1-2 days)
    - td, th content handling

16. [ ] **In Select Mode** (1-2 days)
    - Select element special parsing

17. [ ] **In Select In Table Mode** (1 day)
    - Select nested in table

18. [ ] **In Template Mode** (2 days)
    - Template element special handling
    - DocumentFragment creation

19. [ ] **After Body Mode** (1 day)
    - Content after `</body>`

20. [ ] **In Frameset Mode** (1 day)
    - Frameset parsing (deprecated but in spec)

21. [ ] **After Frameset Mode** (1 day)
    - Content after `</frameset>`

22. [ ] **After After Body Mode** (1 day)
    - Content after `</html>`

23. [ ] **After After Frameset Mode** (1 day)
    - Content after frameset and `</html>`

**Testing**: Comprehensive test suite for each insertion mode with positive and negative cases

---

### 2.3 Adoption Agency Algorithm
**Priority**: HIGH
**Effort**: Medium-Large
**Dependencies**: 2.2

- [ ] Implement the adoption agency algorithm (8 steps from spec)
- [ ] Handle misnested formatting elements:
  - `<b><i></b></i>` → `<b><i></i></b><i></i>`
  - `<a><div><a>` → proper nesting correction
- [ ] Implement "reconstruct active formatting elements"
- [ ] Add formatting element markers (like Noah's Ark clause)

**Testing**:
- Test all misnested formatting element patterns
- Test deeply nested formatting elements
- Test adoption agency edge cases

---

### 2.4 Foster Parenting
**Priority**: MEDIUM
**Effort**: Medium
**Dependencies**: 2.2

- [ ] Implement foster parenting algorithm
- [ ] Handle text nodes in table context
- [ ] Handle elements in table context that shouldn't be there
- [ ] Move misnested content before table

**Testing**:
- Test text directly in `<table>`
- Test `<div>` in table
- Test complex misnesting scenarios

---

## Phase 3: Advanced Features (2-3 weeks)

### 3.1 Foreign Content (SVG, MathML)
**Priority**: MEDIUM
**Effort**: Medium-Large
**Dependencies**: 2.2

- [ ] Add namespace tracking (`HTML`, `SVG`, `MATHML`)
- [ ] Implement foreign content parsing mode
- [ ] Add SVG attribute name adjustments (e.g., `viewBox`)
- [ ] Add MathML attribute name adjustments
- [ ] Handle integration points (SVG `<foreignObject>`, MathML `<annotation-xml>`)
- [ ] Implement adjusted insertion location algorithm
- [ ] Handle self-closing tags in foreign content

**Testing**:
- Test inline SVG parsing
- Test inline MathML parsing
- Test mixed HTML/SVG/MathML documents
- Test attribute name adjustments

---

### 3.2 Template Element
**Priority**: MEDIUM
**Effort**: Medium
**Dependencies**: 2.2

- [ ] Create DocumentFragment for template content
- [ ] Implement template insertion mode stack
- [ ] Handle template element parsing
- [ ] Proper nesting of templates

**Testing**:
- Test basic template elements
- Test nested templates
- Test template with various content

---

### 3.3 Character Encoding
**Priority**: LOW
**Effort**: Medium
**Dependencies**: None

- [ ] Implement BOM detection (UTF-8, UTF-16LE, UTF-16BE)
- [ ] Parse `<meta charset>` attribute
- [ ] Parse `<meta http-equiv="content-type">`
- [ ] Implement encoding confidence algorithm
- [ ] Add encoding conversion (if needed)
- [ ] Handle encoding errors

**Testing**:
- Test various encodings
- Test BOM detection
- Test meta charset parsing

---

### 3.4 Complete Entity Support
**Priority**: MEDIUM
**Effort**: Small-Medium
**Dependencies**: None

- [ ] Add full HTML5 entity table (2,000+ entities)
- [ ] Implement entity ambiguity resolution
- [ ] Support legacy named entities without semicolon
- [ ] Add entity validation and error reporting
- [ ] Optimize entity lookup (hash table or trie)

**Testing**:
- Test all HTML5 named entities
- Test ambiguous entity references
- Test legacy entities without semicolon

---

## Phase 4: Error Handling & Recovery (1-2 weeks)

### 4.1 Parse Error Tracking
**Priority**: HIGH
**Effort**: Medium
**Dependencies**: 1.2

- [ ] Define all HTML5 parse error types (90+ error types)
- [ ] Add structured error reporting:
  ```c++
  struct ParseError {
      const char* error_code;
      const char* message;
      int line;
      int column;
      const char* context;
  };
  ```
- [ ] Implement error collection (list of errors)
- [ ] Add error severity levels
- [ ] Generate error messages with context

**Testing**: Test error reporting for all error types

---

### 4.2 Error Recovery
**Priority**: MEDIUM
**Effort**: Medium
**Dependencies**: 4.1, 2.2

- [ ] Implement "scope" checking algorithms:
  - "has an element in scope"
  - "has an element in button scope"
  - "has an element in list item scope"
  - "has an element in table scope"
  - "has an element in select scope"
- [ ] Auto-close unclosed tags at EOF
- [ ] Handle unexpected end tags gracefully
- [ ] Implement "clear back to table context" algorithms
- [ ] Handle duplicate attributes (ignore duplicates)

**Testing**:
- Test unclosed tags at EOF
- Test unexpected end tags
- Test duplicate attributes
- Test all scope-checking scenarios

---

## Phase 5: Quirks Mode & Special Cases (1 week)

### 5.1 Quirks Mode Detection
**Priority**: LOW
**Effort**: Small
**Dependencies**: 2.1

- [ ] Implement DOCTYPE analysis for quirks mode
- [ ] Add `quirks_mode` flag to parser state:
  - `NO_QUIRKS` (standards mode)
  - `QUIRKS` (quirks mode)
  - `LIMITED_QUIRKS` (limited quirks mode)
- [ ] Apply quirks mode parsing differences

**Testing**: Test various DOCTYPE declarations

---

### 5.2 Form-Associated Elements
**Priority**: LOW
**Effort**: Small
**Dependencies**: 2.2

- [ ] Track current form element pointer
- [ ] Associate form controls with form owner
- [ ] Handle `form` attribute references
- [ ] Implement form pointer null behavior

**Testing**: Test form-associated elements

---

### 5.3 Special Element Rules
**Priority**: LOW
**Effort**: Medium
**Dependencies**: 2.2

- [ ] Implement `<select>` content model rules
- [ ] Handle `<option>` and `<optgroup>` properly
- [ ] Implement `<frameset>` special rules
- [ ] Handle `<noscript>` based on scripting flag
- [ ] Implement `frameset-ok` flag behavior

**Testing**:
- Test select/option/optgroup parsing
- Test frameset elements
- Test noscript with scripting on/off

---

## Phase 6: Performance & Optimization (1-2 weeks)

### 6.1 Memory Optimization
**Priority**: MEDIUM
**Effort**: Medium
**Dependencies**: All previous phases

- [ ] Optimize stack allocations
- [ ] Reduce temporary string allocations
- [ ] Pool token objects
- [ ] Optimize entity lookup (trie or perfect hash)
- [ ] Profile memory usage

**Testing**: Memory profiling and leak detection

---

### 6.2 Performance Optimization
**Priority**: MEDIUM
**Effort**: Medium
**Dependencies**: All previous phases

- [ ] Profile parsing performance
- [ ] Optimize hot paths (tokenization, tree construction)
- [ ] Add fast paths for common cases
- [ ] Benchmark against other parsers
- [ ] Optimize string operations

**Testing**: Performance benchmarks

---

## Phase 7: Testing & Validation (2-3 weeks)

### 7.1 Comprehensive Test Suite
**Priority**: CRITICAL
**Effort**: Large
**Dependencies**: All previous phases

- [ ] Import HTML5lib test suite (1000+ tests)
- [ ] Create tests for all insertion modes
- [ ] Create tests for all error types
- [ ] Add fuzzing tests
- [ ] Add conformance tests
- [ ] Test against real-world HTML documents

**Testing**: Achieve >95% pass rate on HTML5lib tests

---

### 7.2 Regression Testing
**Priority**: HIGH
**Effort**: Medium
**Dependencies**: 7.1

- [ ] Ensure existing tests still pass
- [ ] Add regression tests for fixed bugs
- [ ] Automated test runs in CI/CD
- [ ] Test backward compatibility

---

## Phase 8: Documentation & Polish (1 week)

### 8.1 Documentation
**Priority**: MEDIUM
**Effort**: Small
**Dependencies**: All previous phases

- [ ] Document parser architecture
- [ ] Add inline code comments
- [ ] Create developer guide for parser
- [ ] Document deviations from spec (if any)
- [ ] Update API documentation

---

### 8.2 API Cleanup
**Priority**: MEDIUM
**Effort**: Small
**Dependencies**: All previous phases

- [ ] Review public API
- [ ] Add configuration options (quirks mode, scripting flag, etc.)
- [ ] Add parser statistics API
- [ ] Add error retrieval API
- [ ] Version the parser implementation

---

## Implementation Order Summary

### Priority 1 (Must Have - Core Compliance)
1. Parser State Machine Infrastructure (1.1)
2. Tokenizer Separation (1.2)
3. Implicit Element Creation (2.1)
4. Insertion Mode Implementation (2.2) - Focus on most common modes first
5. Adoption Agency Algorithm (2.3)
6. Parse Error Tracking (4.1)
7. Comprehensive Test Suite (7.1)

### Priority 2 (Should Have - Enhanced Compliance)
8. Foster Parenting (2.4)
9. Foreign Content (3.1)
10. Complete Entity Support (3.4)
11. Error Recovery (4.2)
12. Template Element (3.2)
13. Regression Testing (7.2)

### Priority 3 (Nice to Have - Full Compliance)
14. Character Encoding (3.3)
15. Quirks Mode Detection (5.1)
16. Special Element Rules (5.3)
17. Form-Associated Elements (5.2)
18. Performance Optimization (6.2)
19. Memory Optimization (6.1)
20. Documentation (8.1)
21. API Cleanup (8.2)

---

## Estimated Timeline

- **Phase 1**: 2-3 weeks
- **Phase 2**: 3-4 weeks
- **Phase 3**: 2-3 weeks
- **Phase 4**: 1-2 weeks
- **Phase 5**: 1 week
- **Phase 6**: 1-2 weeks
- **Phase 7**: 2-3 weeks
- **Phase 8**: 1 week

**Total Estimated Time**: 13-18 weeks (3-4.5 months) for full implementation

---

## Incremental Delivery Strategy

### Milestone 1: Basic Tree Construction (Weeks 1-3)
- Deliverable: Parser with state machine and basic insertion modes
- Value: Foundation for all future work

### Milestone 2: Core Parsing (Weeks 4-7)
- Deliverable: Most common insertion modes working, implicit elements
- Value: Can parse 80% of real-world HTML correctly

### Milestone 3: Advanced Features (Weeks 8-10)
- Deliverable: Adoption agency, foster parenting, foreign content
- Value: Can parse 95% of real-world HTML correctly

### Milestone 4: Error Handling (Weeks 11-12)
- Deliverable: Complete error tracking and recovery
- Value: Production-ready parser with good error messages

### Milestone 5: Full Compliance (Weeks 13-15)
- Deliverable: All special cases, quirks mode, form elements
- Value: 100% spec compliance

### Milestone 6: Optimization & Polish (Weeks 16-18)
- Deliverable: Optimized, documented, tested parser
- Value: Production-grade, maintainable implementation

---

## Success Criteria

1. ✅ Pass >95% of HTML5lib test suite
2. ✅ Parse all major website homepages correctly
3. ✅ Performance within 2x of fastest parsers (lexbor, gumbo)
4. ✅ Zero memory leaks
5. ✅ Complete error reporting for all parse errors
6. ✅ Full documentation and examples

---

## Notes

- This is an aggressive but realistic timeline for one developer
- Can be parallelized if multiple developers are available
- Some phases can be done in parallel (e.g., entity support while working on insertion modes)
- Testing should be continuous throughout, not just at the end
- Consider using existing test suites (HTML5lib, Web Platform Tests)

---

## References

- [HTML5 Specification - Parsing](https://html.spec.whatwg.org/multipage/parsing.html)
- [HTML5lib Test Suite](https://github.com/html5lib/html5lib-tests)
- [Web Platform Tests](https://github.com/web-platform-tests/wpt)
- [Gumbo Parser](https://github.com/google/gumbo-parser) - Reference implementation
- [Lexbor](https://github.com/lexbor/lexbor) - High-performance HTML5 parser
