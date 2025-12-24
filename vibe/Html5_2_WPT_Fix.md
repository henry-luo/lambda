# HTML5 Parser WPT Compliance Fix Proposal

**Goal**: Achieve 100% compliance with W3C/WHATWG HTML5 Web Platform Tests (WPT)
**Current Status**: 349/364 tests passing (95.9%) ✅ IN PROGRESS
**Target**: 364/364 tests passing (100%)

## Executive Summary

**UPDATE December 23, 2025**: Major progress achieved. We've gone from 0% to **95.6% pass rate** through systematic implementation of the HTML5 parsing specification.

### Progress History

| Date | Passed | Total | Pass Rate | Milestone |
|------|--------|-------|-----------|----------|
| Dec 23 (baseline) | 0 | 112 | 0% | Fragment parser only |
| Dec 23 (tree builder) | 91 | 364 | 25% | HTML5 tree builder implemented |
| Dec 23 (hr/p fix) | 94 | 364 | 25.8% | Fixed `<hr>` and `</p>` handling |
| Dec 23 (attributes) | 107 | 364 | 29.4% | Fixed attribute handling |
| Dec 23 (AAA partial) | 260 | 364 | 71.4% | Adoption Agency, table modes, entity parsing |
| Dec 23 (RCDATA/RAWTEXT) | 281 | 364 | 77.2% | RCDATA/RAWTEXT states, TEXT mode, metadata tags |
| Dec 23 (entity/frameset/blocks) | 328 | 364 | 90.1% | Legacy entity prefix matching, IN_FRAMESET mode, block element end tag handling |
| Dec 23 (quirks/body attrs/nobr) | 341 | 364 | 93.7% | Quirks mode for table/p, body/html attribute merging, nobr AAA |
| Dec 23 (caption/colgroup/foster) | 348 | 364 | 95.6% | IN_CAPTION/IN_COLUMN_GROUP modes, foster parenting for table text |
| Dec 23 (foster text merge) | **349** | 364 | **95.9%** | Foster parented text merges with adjacent text nodes |

### What's Been Implemented ✅

1. **HTML5 Tokenizer** (`lambda/input/html5/html5_tokenizer.cpp`)
   - Full state machine with 20+ tokenizer states
   - Attribute name/value parsing
   - Comment and DOCTYPE handling
   - Start/end tag token emission
   - **RCDATA state** for `<title>`, `<textarea>` content
   - **RAWTEXT state** for `<style>`, `<script>`, `<noscript>`, `<noframes>` content
   - **PLAINTEXT state** for `<plaintext>` content
   - Last start tag tracking for appropriate end tag matching
   - Character entity reference decoding (named + numeric)
   - **Legacy entity prefix matching** (`&notit;` → `¬` + `it;`)
   - Bogus comment handling for malformed markup (`<!`, `<?`, etc.)

2. **HTML5 Tree Builder** (`lambda/input/html5/html5_tree_builder.cpp`)
   - **Insertion modes**: INITIAL, BEFORE_HTML, BEFORE_HEAD, IN_HEAD, AFTER_HEAD, IN_BODY, AFTER_BODY, AFTER_AFTER_BODY, IN_TABLE, IN_TABLE_BODY, IN_ROW, IN_CELL, **TEXT**, **IN_SELECT**, **IN_FRAMESET**, **AFTER_FRAMESET**, **IN_CAPTION**, **IN_COLUMN_GROUP**
   - Open elements stack with push/pop operations
   - Active formatting elements list with marker support
   - Implicit element creation (html, head, body)
   - **Adoption Agency Algorithm** (AAA) for misnested formatting elements
   - Metadata tags handled after `</head>` (link, meta, base, script, style, etc.)
   - **Ignore leading newline** for `<textarea>`, `<pre>`, `<listing>`
   - **Block element end tags** generate implied end tags per WHATWG spec
   - **Quirks mode detection** - no DOCTYPE → quirks mode (affects table/p behavior)
   - **Body/html attribute merging** - subsequent `<body>` tags merge attrs to existing
   - **`<nobr>` and `<a>` AAA handling** - nested tags close previous one via AAA
   - **Foster parenting** - text in table context inserted before table element

3. **Element Handling**
   - Block elements closing `<p>` in button scope
   - `<hr>` closing `<p>` correctly
   - Heading auto-closing (`<h1>` closed by `<h2>`, etc.)
   - Void element handling (`<br>`, `<hr>`, `<img>`, etc.)
   - Attribute parsing and transfer to elements
   - `<image>` → `<img>` conversion per spec
   - `<textarea>` RCDATA mode switching
   - Boolean attributes with empty string values
   - `<li>`, `<dd>`, `<dt>` closing previous siblings (not stopped by `<div>`/`<p>`)

4. **Recent Fixes (Latest Session)**
   - ✅ IN_CAPTION mode - proper caption handling and implicit closing
   - ✅ IN_COLUMN_GROUP mode - `<col>` handling and implicit colgroup closing
   - ✅ Foster parenting for text in table context
   - ✅ `<plaintext>` element handling (PLAINTEXT tokenizer state)
   - ✅ MarkEditor.array_insert support for Element types

### Remaining Work ❌ (16 tests)

1. **Complex AAA/Anchor in Table** (~10 tests)
   - Nested `<a>` tags with tables: `<a><table><td><a>...`
   - `<a>` foster parented from table contexts
   - `<marquee>` and `<a>` interaction

2. **SELECT Element Handling** (~2 tests)
   - Nested `<select>` elements
   - `<b>` inside `<select>` context

3. **Colgroup Edge Cases** (~2 tests)
   - Multiple colgroup elements with interleaved content
   - `<col>` outside colgroup

4. **Script Type Edge Case** (~1 test)
   - Non-JS script type (`type=text/x-foobar`) preserving end tag case

5. **Table Plaintext** (~1 test)
   - `<plaintext>` inside `<table>` should be foster parented

---

## Test Failure Analysis

### Category 1: Missing Implicit Structure Creation (28 tests)

**Problem**: When parsing incomplete HTML (e.g., `<html>`, `<head>`, `<body>`), the parser doesn't create implicit sibling elements as required by HTML5.

**Examples**:
- Input: `<html>` → Expected: `<html><head><body>` (but we produce only `<html>`)
- Input: `<head>` → Expected: `<html><head><body>` (but we produce only `<html><head>`)
- Input: `<body>` → Expected: `<html><head><body>` (but we produce only `<html><body>`)
- Input: `</head>` → Expected: `<html><head><body>` (closing tag creates structure!)

**Root Cause**: Our wrapping logic only activates when content *lacks* `<html>`, but HTML5 requires implicit element creation even when structure exists.

**HTML5 Spec Reference**: [12.2 Parsing HTML documents](https://html.spec.whatwg.org/multipage/parsing.html#parsing)

**Impact**: 25% of failures

**Test Cases**:
```
c99d322c35 - <html>
d8473f7b5c - <head>
ac7703fbb5 - <body>
a00121213e - <html><head>
447f22e6a4 - <html><head></head>
83cce5b1e1 - <html><body></html>
b9e809bc45 - <body></html>
60302916ab - <head></html>
f4ad8e574f - </head>
d38fe13d87 - </body>
e5f2e91cbf - </html>
+ 17 more...
```

### Category 2: Text Content Parsing Failure (12 tests)

**Problem**: Text nodes adjacent to void elements (like `<br>`) are being lost or not created.

**Examples**:
- Input: `Line1<br>Line2<br>Line3<br>Line4`
- Expected: Text "Line1", `<br>`, Text "Line2", `<br>`, Text "Line3", `<br>`, Text "Line4"
- Actual: Only three `<br>` elements, all text content missing

**Root Cause**: The parser likely treats text before/after void elements incorrectly, possibly:
1. Text tokenization failing near self-closing tags
2. Text nodes not being flushed before void elements
3. Buffer management issue where text is overwritten

**Impact**: 12% of failures

**Test Cases**:
```
2433aa5c08 - Line1<br>Line2<br>Line3<br>Line4
4235382bf1 - Test (plain text failing to parse at all!)
1fc4fad5ee - < (bare less-than)
f85417e345 - <# (malformed tag with text)
277ea1a5aa - </ (bare close tag)
+ 7 more...
```

### Category 3: Auto-Closing Tag Rules (15 tests)

**Problem**: Block-level elements should auto-close previous elements of the same or certain types, but our parser doesn't implement these rules.

**Examples**:
- Input: `<h1>Hello<h2>World`
- Expected: `<h1>Hello</h1><h2>World</h2>` (h2 auto-closes h1)
- Actual: Nested or malformed structure

- Input: `<p><hr></p>`
- Expected: `<p></p><hr><p></p>` (hr auto-closes p, then p reopens)
- Actual: `<p><hr></p>` (invalid nesting)

**Root Cause**: No implementation of HTML5 insertion mode rules that determine when to implicitly close tags.

**HTML5 Spec Reference**: [12.2.6 Tree construction](https://html.spec.whatwg.org/multipage/parsing.html#tree-construction) - "When the steps below require the UA to insert an HTML element..."

**Impact**: 13% of failures

**Test Cases**:
```
260db4c31f - <h1>Hello<h2>World
daa9d8440e - <p><hr></p>
d7607fdd41 - <h1><h2>
+ 12 more...
```

### Category 4: Adoption Agency Algorithm (18 tests)

**Problem**: When formatting elements (like `<a>`, `<b>`, `<i>`) are improperly nested with block elements, HTML5 uses the "Adoption Agency Algorithm" to reconstruct proper nesting.

**Examples**:
- Input: `<a><p>X<a>Y</a>Z</p></a>`
- Expected: Complex restructuring where second `<a>` forces first to close, then reopens after
- Actual: Direct parsing without restructuring

- Input: `<b><button>foo</b>bar`
- Expected: Button adopts the `<b>` internally, "bar" is outside both
- Actual: Improper nesting

**Root Cause**: No adoption agency algorithm implementation. This is one of the most complex parts of HTML5 parsing.

**HTML5 Spec Reference**: [12.2.6.4 The "adoption agency algorithm"](https://html.spec.whatwg.org/multipage/parsing.html#adoption-agency-algorithm)

**Impact**: 16% of failures

**Test Cases**:
```
59d9c5e294 - <a><p>X<a>Y</a>Z</p></a>
1e788677da - <b><button>foo</b>bar
240ee32b47 - <!DOCTYPE html><span><button>foo</span>bar
e85dc012a4 - <p><b><div><marquee></p></b></div>X
cc9c12dd44 - <a X>0<b>1<a Y>2
+ 13 more...
```

### Category 5: Comment and Bogus Token Handling (16 tests)

**Problem**: Malformed markup like `<?`, `<!`, `<#`, etc. should be parsed as comments (bogus comments), but our parser doesn't handle these edge cases.

**Examples**:
- Input: `<?` → Expected: `<!-- ? -->` (bogus comment)
- Input: `<!` → Expected: `<!--  -->` (empty comment)
- Input: `<?COMMENT?>` → Expected: `<!-- ?COMMENT? -->` (bogus comment)

**Root Cause**: Tokenizer lacks bogus comment state handling for malformed markup.

**HTML5 Spec Reference**: [12.2.5.52 Bogus comment state](https://html.spec.whatwg.org/multipage/parsing.html#bogus-comment-state)

**Impact**: 14% of failures

**Test Cases**:
```
619aa59341 - <?
40fd9f6e4a - <?#
82120b3520 - <!
a9f265e67b - <!#
6325e1d53c - <?COMMENT?>
451c02faba - <!COMMENT>
fefda34292 - </ COMMENT >
+ 9 more...
```

### Category 6: Foster Parenting (8 tests)

**Problem**: When table elements are encountered with improper content (e.g., text or formatting elements outside cells), HTML5 "foster parents" them before the table.

**Examples**:
- Input: `<b><table><td><i></table>`
- Expected: `<b>` wraps table, `<i>` inside `<td>`
- Actual: Improper structure

**Root Cause**: No foster parenting mechanism for table-related insertion mode violations.

**HTML5 Spec Reference**: [12.2.6.1 Creating and inserting nodes - foster parenting](https://html.spec.whatwg.org/multipage/parsing.html#foster-parent)

**Impact**: 7% of failures

**Test Cases**:
```
18b58d1de1 - <b><table><td><i></table>
88eb936910 - <b><table><td></b><i></table>X
a2d3321d1e - <a><table><td><a><table></table><a></tr><a></table><b>X</b>C<a>Y
+ 5 more...
```

### Category 7: Script and Style Tag Handling (3 tests)

**Problem**: Content inside `<script>` and `<style>` tags should be parsed as raw text (including what looks like HTML), but our parser may be parsing it structurally.

**Examples**:
- Input: `<script><div></script></div>`
- Expected: `<script>` contains text "<div>", then `</div>` is a separate element
- Actual: Possible nested structure

**Root Cause**: Parser doesn't switch to RAWTEXT or RCDATA state for script/style content.

**HTML5 Spec Reference**: [12.2.5.2 RCDATA state](https://html.spec.whatwg.org/multipage/parsing.html#rcdata-state), [12.2.5.5 RAWTEXT state](https://html.spec.whatwg.org/multipage/parsing.html#rawtext-state)

**Impact**: 3% of failures

**Test Cases**:
```
c440d1f022 - <script><div></script></div><title><p></title><p><p>
d60de29dd2 - <!DOCTYPE html><style> EOF
+ 1 more...
```

### Category 8: Special Element Interactions (8 tests)

**Problem**: Elements like `<select>`, `<option>`, `<optgroup>`, `<li>`, etc. have special parsing rules for what can be nested inside them and how they close.

**Examples**:
- Input: `<select><b><option><select><option></b></select>X`
- Expected: Complex restructuring based on select's content model
- Actual: Direct nesting

**Root Cause**: Missing "in select" and "in select in table" insertion modes.

**Impact**: 7% of failures

**Test Cases**:
```
0b27e026dd - <select><b><option><select><option></b></select>X
1dfb5ce6c1 - <!DOCTYPE html>A<option>B<optgroup>C<select>D</option>E
f158c8b44e - <!DOCTYPE html><li>hello<li>world<ul>how<li>do</ul>you</body><!--do-->
+ 5 more...
```

## Current Implementation Gap Analysis

### What We Have ✓
1. **HTML5 Tokenizer**: Character-by-character tokenization with state machine (html5_tokenizer.cpp)
2. **Token Types**: Start tag, end tag, character, comment, DOCTYPE, EOF tokens
3. **Attribute Collection**: Token attributes properly stored and transferred to elements
4. **Tree Builder Skeleton**: Basic tree construction with insertion mode framework
5. **Open Elements Stack**: Basic stack implementation for tracking open elements
6. **Basic Insertion Modes**: Initial, before HTML, before head, in head, after head, in body, after body implemented
7. **Implicit Element Creation**: Auto-creates `<html>`, `<head>`, `<body>` as needed
8. **Auto-closing Rules**: Basic auto-close for `<p>`, `<li>`, heading tags
9. **Void Elements**: Proper handling of self-closing elements (br, hr, img, etc.)
10. **MarkBuilder Integration**: Proper Lambda data structure construction
11. **Adoption Agency Algorithm**: Core AAA for misnested formatting elements ✅ NEW
12. **Active Formatting Elements**: List with marker support for AAA ✅ NEW
13. **RCDATA/RAWTEXT States**: `<title>`, `<textarea>`, `<style>`, `<script>` content ✅ NEW
14. **TEXT Insertion Mode**: For processing raw text element content ✅ NEW
15. **Character Entity References**: Named and numeric entity decoding ✅ NEW
16. **Table Modes**: Basic IN_TABLE, IN_TABLE_BODY, IN_ROW, IN_CELL modes ✅ NEW
17. **Metadata After Head**: `<link>`, `<meta>`, etc. correctly inserted after `</head>` ✅ NEW
18. **Bogus Comment Handling**: Malformed markup like `<!`, `<?` converted to comments ✅ NEW

### What We're Missing ✗
1. ~~**Table Insertion Modes**: "in table", "in table body", "in row", "in cell" modes incomplete~~ ✅ DONE
2. **Implicit Table Elements**: `<tbody>`, `<tr>` not always auto-created correctly
3. ~~**Adoption Agency Algorithm**: Not implemented for handling misnested formatting elements~~ ✅ DONE (partial)
4. ~~**Active Formatting Elements**: List not properly maintained for AAA~~ ✅ DONE
5. **Foster Parenting**: Not fully implemented for content appearing in wrong table context
6. **Select Modes**: "in select", "in select in table" modes not implemented
7. ~~**RCDATA/RAWTEXT States**: Script/style content not properly handled as raw text~~ ✅ DONE
8. **Character References**: Some edge cases with semicolon-less entities remain
9. **Template Handling**: Template element not properly supported
10. **Scope Checking**: Some element scope algorithms need refinement

## Architectural Recommendations

### Option A: Full HTML5 Parser State Machine (Recommended)

**Approach**: Implement the complete HTML5 parsing algorithm as specified in WHATWG spec.

**Pros**:
- 100% spec compliance
- Handles all edge cases correctly
- Future-proof for new tests
- Matches browser behavior exactly

**Cons**:
- Large implementation effort (~4,000-6,000 LOC)
- Complex state machine with 80+ states
- Requires careful implementation to avoid bugs

**Estimated Effort**: 4-6 weeks for experienced developer

**Implementation Components**:
1. **Tokenizer State Machine** (12.2.5)
   - 80+ states for character-by-character processing
   - Token emission (start tag, end tag, comment, DOCTYPE, character, EOF)
   - Error handling and recovery

2. **Tree Constructor** (12.2.6)
   - 27 insertion modes
   - Stack of open elements
   - List of active formatting elements
   - Head element pointer
   - Form element pointer
   - Scripting flag
   - Frameset-ok flag

3. **Core Algorithms**:
   - Adoption agency algorithm (12.2.6.4)
   - Foster parenting (12.2.6.1)
   - Generic RCDATA/RAWTEXT parsing (12.2.5.2, 12.2.5.5)
   - Misnested tags handling
   - Scope checking (has element in scope, has element in button scope, etc.)

### Option B: Incremental Improvement (Not Recommended)

**Approach**: Add patches for each failing test category incrementally.

**Pros**:
- Can see progress quickly
- Less daunting initially

**Cons**:
- Will never reach 100% (edge cases will remain)
- Accumulates technical debt
- Harder to maintain
- May need rewrite anyway

**Why Not Recommended**: HTML5 parsing is fundamentally a state machine problem. Trying to solve it with ad-hoc rules creates a fragile, unmaintainable system that will never fully work.

## Proposed Implementation Plan

### Phase 1: Foundation (Week 1)
**Goal**: Set up core data structures

1. **Define Token Types**:
   ```cpp
   enum TokenType {
       TOKEN_DOCTYPE,
       TOKEN_START_TAG,
       TOKEN_END_TAG,
       TOKEN_COMMENT,
       TOKEN_CHARACTER,
       TOKEN_EOF
   };

   struct Token {
       TokenType type;
       String* tag_name;      // for tags
       String* data;          // for comments, characters, DOCTYPE
       Map* attributes;       // for start tags
       bool self_closing;     // for start tags
   };
   ```

2. **Define Parser State**:
   ```cpp
   enum InsertionMode {
       INITIAL,
       BEFORE_HTML,
       BEFORE_HEAD,
       IN_HEAD,
       AFTER_HEAD,
       IN_BODY,
       IN_TABLE,
       IN_SELECT,
       // ... 27 total modes
   };

   struct Html5Parser {
       Arena* arena;
       Pool* pool;

       // Token stream
       const char* input;
       size_t pos;

       // Parsing state
       InsertionMode mode;
       Element* document;
       Element* head_element;
       Element* form_element;

       // Stacks
       List* open_elements;           // Stack of open elements
       List* active_formatting;       // List of active formatting elements

       // Flags
       bool scripting_enabled;
       bool frameset_ok;
       bool foster_parenting;

       // Tokenizer state
       int tokenizer_state;
       Token* current_token;
   };
   ```

3. **Create Stack Helper Functions**:
   ```cpp
   Element* current_node(Html5Parser* parser);
   void push_element(Html5Parser* parser, Element* elem);
   Element* pop_element(Html5Parser* parser);
   bool has_element_in_scope(Html5Parser* parser, const char* tag_name);
   bool has_element_in_button_scope(Html5Parser* parser, const char* tag_name);
   bool has_element_in_table_scope(Html5Parser* parser, const char* tag_name);
   void generate_implied_end_tags(Html5Parser* parser);
   void reconstruct_active_formatting_elements(Html5Parser* parser);
   ```

**Deliverable**: Core data structures and helper functions (no actual parsing yet)

### Phase 2: Tokenizer (Week 2)
**Goal**: Implement HTML5 tokenizer state machine

1. **Implement Basic States**:
   - Data state (12.2.5.1)
   - Tag open state (12.2.5.6)
   - Tag name state (12.2.5.8)
   - End tag open state (12.2.5.7)
   - Comment start state (12.2.5.42)
   - Bogus comment state (12.2.5.52)

2. **Implement Attribute Parsing**:
   - Before attribute name state (12.2.5.32)
   - Attribute name state (12.2.5.33)
   - Attribute value states (12.2.5.36-38)

3. **Implement Special States**:
   - RCDATA state (12.2.5.2) - for `<title>`, `<textarea>`
   - RAWTEXT state (12.2.5.5) - for `<script>`, `<style>`
   - Script data states (12.2.5.6+) - for `<script>` content

4. **Character Reference Handling**:
   - Named character references (`&nbsp;`, `&lt;`, etc.)
   - Numeric character references (`&#65;`, `&#x41;`)

**Deliverable**: Working tokenizer that converts HTML string to token stream

**Test**: Should pass comment and bogus comment tests (Category 5: 16 tests)

### Phase 3: Tree Constructor Basics (Week 3)
**Goal**: Implement basic insertion modes

1. **Implement Initial Modes**:
   - Initial mode (12.2.6.4.1) - handles DOCTYPE
   - Before HTML mode (12.2.6.4.2) - creates `<html>`
   - Before head mode (12.2.6.4.3) - creates `<head>`
   - In head mode (12.2.6.4.4) - processes head content
   - After head mode (12.2.6.4.6) - transitions to body

2. **Implement "In Body" Mode** (12.2.6.4.7):
   - Character tokens → insert text
   - Start tag tokens → insert elements
   - End tag tokens → close elements
   - Special elements (p, h1-h6, etc.) → auto-close previous
   - Formatting elements (a, b, i, etc.) → add to active formatting elements

3. **Element Insertion Logic**:
   ```cpp
   void insert_html_element(Html5Parser* parser, Token* token);
   void insert_character(Html5Parser* parser, char c);
   void insert_comment(Html5Parser* parser, Token* token);
   ```

**Deliverable**: Can parse simple documents with proper structure

**Test**: Should pass implicit structure tests (Category 1: 28 tests) and text content tests (Category 2: 12 tests)

### Phase 4: Auto-Closing and Special Elements (Week 4)
**Goal**: Implement auto-closing rules and special element handling

1. **Generate Implied End Tags** (12.2.6.3):
   - When opening certain elements, close implied open elements
   - p tags close when block elements start
   - li tags close when another li starts
   - h1-h6 tags close when another heading starts

2. **Implement Table Modes**:
   - In table mode (12.2.6.4.9)
   - In table body mode (12.2.6.4.11)
   - In row mode (12.2.6.4.12)
   - In cell mode (12.2.6.4.13)

3. **Implement Select Modes**:
   - In select mode (12.2.6.4.16)
   - In select in table mode (12.2.6.4.17)

4. **Foster Parenting** (12.2.6.1):
   - When table elements receive unexpected content
   - Insert content before the table instead of inside

**Deliverable**: Auto-closing works, tables parse correctly

**Test**: Should pass auto-closing tests (Category 3: 15 tests), foster parenting tests (Category 6: 8 tests), and special element tests (Category 8: 8 tests)

### Phase 5: Adoption Agency Algorithm (Week 5)
**Goal**: Implement the adoption agency algorithm

1. **Active Formatting Elements List**:
   - Push formatting elements when opened
   - Reconstruct active formatting elements before inserting content
   - Clear to last marker on certain events

2. **Adoption Agency Algorithm** (12.2.6.4):
   - Find the subject element
   - Find the formatting element
   - Find the furthest block
   - Bookmark the formatting element
   - Perform complex node rearrangement
   - Insert at appropriate position

3. **Reconstruction**:
   - When inserting character or element in body mode
   - Reopen formatting elements that were closed prematurely

**Deliverable**: Complex formatting element nesting works correctly

**Test**: Should pass adoption agency tests (Category 4: 18 tests)

### Phase 6: Script/Style and Edge Cases (Week 6)
**Goal**: Handle remaining edge cases

1. **RCDATA/RAWTEXT Content**:
   - Switch tokenizer state when entering `<script>`, `<style>`
   - Parse content as plain text
   - Only exit on matching end tag

2. **Remaining Modes**:
   - In template mode (12.2.6.4.19)
   - After body mode (12.2.6.4.8)
   - After after body mode (12.2.6.4.20)
   - In frameset mode (12.2.6.4.21)

3. **Edge Case Handling**:
   - EOF in various states
   - Null character handling
   - Parse errors (log but continue)

**Deliverable**: All remaining tests pass

**Test**: Should pass script/style tests (Category 7: 3 tests) and remaining edge cases

### Phase 7: Testing and Optimization (Week 7)
**Goal**: Ensure all tests pass and optimize performance

1. **Run Full WPT Suite**: All 112 tests in html5lib_tests1.json
2. **Run Extended Suites**: html5lib_tests2-26.json (1,500+ additional tests)
3. **Performance Profiling**: Optimize hot paths
4. **Memory Optimization**: Ensure no leaks, minimize allocations
5. **Documentation**: Update design docs and code comments

**Deliverable**: 100% WPT compliance, production-ready code

## Implementation Guidelines

### Code Organization

```
lambda/input/
├── html5/
│   ├── html5_parser.h          // Main parser interface
│   ├── html5_parser.cpp        // Parser implementation
│   ├── html5_tokenizer.h       // Tokenizer interface
│   ├── html5_tokenizer.cpp     // Tokenizer state machine
│   ├── html5_tree_builder.h    // Tree constructor interface
│   ├── html5_tree_builder.cpp  // Insertion modes
│   ├── html5_adoption.cpp      // Adoption agency algorithm
│   └── html5_entities.h        // Character entity references
└── input-html.cpp              // Updated to use html5_parser
```

### Testing Strategy

1. **Unit Tests**: Test individual components (tokenizer, tree builder)
2. **Integration Tests**: Test full parsing pipeline
3. **WPT Tests**: Run official web platform tests
4. **Regression Tests**: Ensure existing functionality still works

### Code Style

- Follow existing Lambda codebase conventions
- Use C++ (not C) for new code (allows classes, better type safety)
- Extensive logging at debug level for state transitions
- Clear comments referencing HTML5 spec sections

## Alternative: Use Existing HTML5 Parser Library

### Option C: Integrate Existing Library

**Libraries to Consider**:
1. **gumbo-parser** (C, Google, Apache 2.0)
   - Pure C implementation
   - Spec-compliant HTML5 parser
   - Produces tree structure
   - ~8,000 LOC

2. **lexbor** (C, Apache 2.0)
   - Fast HTML5 parser
   - Full spec compliance
   - Modular design
   - ~30,000 LOC (includes CSS)

3. **myhtml** (C, LGPL/Apache 2.0)
   - High-performance parser
   - Multi-threaded capable
   - Full HTML5 compliance
   - ~40,000 LOC

**Pros**:
- Immediate 100% compliance
- Battle-tested code
- Maintained by experts
- Saves 6 weeks of development

**Cons**:
- External dependency
- Need adapter layer to Lambda data structures
- Less control over behavior
- License compatibility considerations
- Larger codebase to integrate

**Recommendation**: If schedule is critical, use **gumbo-parser**:
1. It's pure C (matches our style)
2. Relatively small and readable
3. Apache 2.0 license (permissive)
4. Well-documented
5. Used by Google, readability.com, etc.

**Integration Approach**:
1. Embed gumbo source in `lambda/input/html5/gumbo/`
2. Create `html5_gumbo_adapter.cpp` to convert gumbo tree to Lambda Elements
3. Update `input-html.cpp` to call gumbo parser
4. Map gumbo nodes to MarkBuilder API
5. Test with WPT suite

**Estimated Effort**: 1 week to integrate and test

## Risk Analysis

### Risks for Full Implementation (Option A)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Spec complexity underestimated | Medium | High | Add 2-week buffer, start early |
| Adoption agency bugs | High | Medium | Extensive test coverage, careful implementation |
| Performance degradation | Low | Medium | Profile early, optimize incrementally |
| Breaking existing functionality | Medium | High | Comprehensive regression tests |
| Schedule slippage | Medium | Medium | Weekly checkpoints, parallel testing |

### Risks for Library Integration (Option C)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| License compatibility issues | Low | High | Review licenses upfront |
| Integration complexity | Low | Medium | Prototype in first 2 days |
| External dependency maintenance | Medium | Low | Fork if needed, low update frequency |
| Performance issues | Low | Medium | Benchmark before committing |

## Success Metrics

1. **WPT Test Pass Rate**: 100% (112/112 tests in html5lib_tests1.json)
2. **Extended WPT**: >95% (1,500+ tests in html5lib_tests2-26.json)
3. **Performance**: <10% slower than current parser (which is already fast)
4. **Memory**: <20% more allocations than current parser
5. **Code Quality**: No new compiler warnings, passes linter
6. **Documentation**: All public APIs documented

## Recommendation

### Primary Recommendation: Option C (Integrate gumbo-parser)

**Rationale**:
- **Time to Market**: 1 week vs 6 weeks for full implementation
- **Quality**: Proven, battle-tested code vs new, potentially buggy implementation
- **Maintainability**: Small, focused integration code vs large state machine
- **Risk**: Low risk vs medium-high risk of implementation bugs
- **Cost-Benefit**: Best ROI for achieving 100% compliance

**Action Items**:
1. Download and review gumbo-parser source
2. Verify Apache 2.0 license compatibility
3. Create integration prototype (2-3 days)
4. Run WPT tests to confirm 100% compliance
5. Optimize adapter layer for performance
6. Document integration and API
7. Deploy to production

### Alternative: Option A (Full Implementation)

**When to Choose**:
- You want no external dependencies
- You have 6+ weeks available
- You want maximum control over parsing behavior
- You want to learn HTML5 parsing deeply
- You plan to extend beyond standard HTML5

**Action Items**:
- Follow 7-phase implementation plan above
- Dedicate 1 senior developer full-time
- Weekly progress reviews
- Start with tokenizer to validate approach

## Appendix A: HTML5 Parsing Overview

### Key Concepts

1. **Tokenization**: Character stream → tokens
2. **Tree Construction**: Tokens → DOM tree
3. **Insertion Modes**: Different rules for different contexts (27 modes)
4. **Open Elements Stack**: Track what elements are currently open
5. **Active Formatting Elements**: Track `<a>`, `<b>`, `<i>`, etc. for adoption agency
6. **Foster Parenting**: Special handling for table content violations
7. **Adoption Agency**: Complex algorithm for restructuring misplaced formatting elements

### State Machine Complexity

- **Tokenizer**: 80+ states
- **Tree Constructor**: 27 insertion modes
- **Total Spec Sections**: 12.2.5 (tokenization) + 12.2.6 (tree construction) = ~150 pages
- **Estimated LOC**: 4,000-6,000 lines of production code
- **Test Complexity**: 1,500+ edge cases in full WPT suite

### Why It's Hard

HTML5 parsing is intentionally designed to handle any input, no matter how malformed. This requires:
- Extensive error recovery
- Context-sensitive parsing (same tag behaves differently in different modes)
- Complex algorithms for fixing broken nesting
- Handling of legacy HTML (HTML 1.0, 2.0, 3.2, 4.01, XHTML)
- Browser compatibility quirks

## Appendix B: Test Case Reference

See full test details in `/test/html/wpt/html5lib_tests1.json` (674 lines, 112 tests).

**Test ID Format**: SHA-1 hash of test input
**Test Structure**: JSON array of objects with `test_id`, `file`, `input`, `expected`
**Expected Format**: Tree serialization with `|` indentation, attributes on separate lines

## Appendix C: Relevant Specifications

1. **WHATWG HTML Living Standard**: https://html.spec.whatwg.org/multipage/parsing.html
2. **W3C HTML5 Specification**: https://www.w3.org/TR/2014/REC-html5-20141028/syntax.html
3. **Web Platform Tests**: https://github.com/web-platform-tests/wpt
4. **html5lib**: https://github.com/html5lib/html5lib-python (reference implementation)

## Decision: Option A - Full HTML5 Parser Implementation

**Decision Date**: December 23, 2024
**Decision Maker**: Lambda Project Team
**Status**: Approved for Implementation

### Options Considered

#### Option A: Full HTML5 Parser State Machine ✓ SELECTED
**Summary**: Implement complete HTML5 parsing algorithm from WHATWG spec.

**Key Attributes**:
- Implementation effort: 4-6 weeks
- Code size: 4,000-6,000 LOC
- Compliance: 100% spec-compliant
- Dependencies: None (pure Lambda implementation)
- Maintainability: Full control over behavior
- Performance: Optimized for Lambda's use cases

**Advantages**:
1. Perfect integration with Lambda's MarkBuilder/MarkReader API
2. No external dependencies or license concerns
3. Full control over memory allocation (pool/arena)
4. Can optimize for Lambda's data processing patterns
5. Deep understanding of parsing internals enables future enhancements
6. Matches Lambda's philosophy of building from scratch

**Disadvantages**:
1. Longer initial development time
2. Requires careful implementation to avoid bugs
3. Need to maintain HTML5 spec compliance over time

#### Option B: Incremental Improvement ✗ REJECTED
**Summary**: Add ad-hoc patches for each failing test category.

**Why Rejected**:
- Will never achieve 100% compliance
- Accumulates technical debt rapidly
- Fragile, hard to maintain
- Fundamentally wrong approach for state machine problem
- Would likely require rewrite later anyway

#### Option C: Integrate gumbo-parser ✗ NOT CHOSEN
**Summary**: Embed Google's gumbo-parser library with adapter layer.

**Key Attributes**:
- Implementation effort: 1 week
- Code size: ~8,000 LOC (external) + ~500 LOC (adapter)
- Compliance: 100% spec-compliant (proven)
- Dependencies: gumbo-parser (Apache 2.0)
- Maintainability: External dependency to track

**Why Not Chosen**:
1. **Architectural Mismatch**: Gumbo uses its own tree structure; converting to Lambda Elements adds overhead and complexity
2. **Memory Management**: Gumbo uses malloc/free; Lambda uses pool/arena allocators. Bridging this gap creates inefficiency
3. **Integration Complexity**: Despite 1-week estimate, proper integration with Lambda's:
   - Reference counting system
   - String pooling (namepool)
   - Shape caching for maps
   - Type system (TypeElmt)
   Would add significant complexity
4. **Loss of Control**: Cannot optimize for Lambda-specific patterns (e.g., streaming processing, memory reuse)
5. **Future Limitations**: Hard to extend for Lambda-specific HTML features or optimizations
6. **Debugging**: Harder to debug issues in external library vs our own code
7. **Philosophy**: Lambda's strength is its integrated, ground-up design. External parsers break this coherence

**When Option C Would Be Better**:
- If Lambda were a general-purpose scripting language (it's not—it's specialized for data processing)
- If time to market was absolutely critical (HTML parsing is important but not blocking other features)
- If we had no systems programming expertise (we do)
- If we needed immediate compliance for compliance's sake (we need it right, not just fast)

### Rationale for Option A

**Primary Reasons**:

1. **Deep Lambda Integration**: Building the parser ourselves allows perfect integration with:
   - MarkBuilder API for tree construction
   - Pool/arena memory allocators (zero waste)
   - Name pooling for tag names and attribute names
   - Shape caching for element attribute maps
   - Reference counting system
   - Type system integration

2. **Performance Optimization**: We can optimize specifically for Lambda's use cases:
   - Batch processing of multiple HTML documents
   - Streaming HTML parsing for large files
   - Memory reuse across parse operations
   - Zero-copy string handling where possible

3. **Future Extensibility**: Full control enables future enhancements:
   - Custom HTML extensions for Lambda
   - Integration with CSS engine (Radiant)
   - SVG parsing (already have SVG renderer)
   - MathML support
   - Web Components support

4. **Learning and Expertise**: Implementing HTML5 parser provides deep knowledge of:
   - Web standards compliance
   - State machine design patterns
   - Error recovery strategies
   - Browser compatibility issues
   This knowledge benefits the entire Lambda project

5. **Code Quality**: Lambda's codebase is known for clarity and maintainability. An external parser would be an outlier in style and quality

6. **Long-term Maintainability**: We control the code, understand it fully, and can fix bugs immediately without waiting for upstream

### Implementation Strategy

Given the decision for Option A, we will follow the 7-phase implementation plan:

**Timeline**: 7 weeks (4-6 weeks core implementation + 1 week buffer + testing)

**Resource Allocation**: 1 senior developer full-time

**Quality Gates**: Each phase must pass its designated WPT tests before proceeding to next phase

**Risk Mitigation**:
- Weekly code reviews
- Continuous testing against WPT suite
- Parallel documentation of design decisions
- Fallback: If by Week 4 progress is <30% complete, reassess and potentially pivot to Option C

**Success Criteria**:
- 100% compliance with html5lib_tests1.json (112 tests)
- >95% compliance with extended WPT suite (1,500+ tests)
- Performance within 10% of current parser (which is already fast)
- Zero memory leaks
- Full API documentation

### Next Steps

**Completed:**
- ✅ **Phase 1**: Core data structures (tokens, parser state, stacks)
- ✅ **Phase 2**: Tokenizer state machine (20+ states, attribute parsing)
- ✅ **Phase 3**: Tree constructor basics (insertion modes, implicit elements)

**In Progress:**
- 🔄 **Phase 4**: Table mode handling (implicit tbody/tr creation)
- 🔄 **Phase 5**: Adoption Agency Algorithm for formatting elements

**Remaining:**
- ⏳ **Phase 6**: Script/style RAWTEXT handling
- ⏳ **Phase 7**: Character entity references
- ⏳ **Final**: Testing, optimization, edge cases

**Immediate Priority (to reach 50%+ pass rate):**
1. Implement table insertion modes (in_table, in_table_body, in_row, in_cell)
2. Auto-create `<tbody>` and `<tr>` when `<td>` appears directly in `<table>`
3. Implement foster parenting for misplaced table content

### Approval

- **Approved By**: Henry Luo (Project Lead)
- **Date**: December 23, 2024
- **Implementation Start**: Week of December 23, 2024

---

## Document Metadata

- **Author**: GitHub Copilot (AI Assistant)
- **Date**: December 23, 2024
- **Version**: 1.1
- **Status**: Decision Approved - Implementation Phase
- **Next Review**: Week 4 checkpoint (January 2025)
