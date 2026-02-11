# Readability.ls - Porting Mozilla Readability to Lambda Script

## Overview

This document proposes porting Mozilla's [Readability.js](https://github.com/mozilla/readability) to Lambda Script. Readability.js is a standalone library used for Firefox Reader View that extracts the main content from web pages, stripping away navigation, ads, and clutter.

### Original Library Stats
- **Lines of Code**: ~2,813 lines (Readability.js) + ~123 lines (isProbablyReaderable)
- **Core Algorithm**: Content scoring based on text density, class names, DOM structure
- **License**: Apache 2.0

## Feasibility Analysis

### ✅ Features That Can Be Ported

| Feature                 | Lambda Support | Notes                                     |
| ----------------------- | -------------- | ----------------------------------------- |
| HTML Parsing            | ✅ Full         | `html5_parse()` via `input()` function    |
| DOM Traversal           | ✅ Full         | `ElementReader`, `MarkReader` APIs        |
| Element Manipulation    | ✅ Full         | `MarkBuilder`, `MarkEditor`               |
| Text Content Extraction | ✅ Full         | `ElementReader.textContent()`             |
| String Operations       | ✅ Full         | Built-in string functions                 |
| Regex Pattern Matching  | ✅ Full         | RE2-based pattern matching                |
| Maps/Objects            | ✅ Full         | Native map type `{key: value}`            |
| Arrays/Lists            | ✅ Full         | Native array `[1,2,3]` and list `(1,2,3)` |
| Scoring Algorithm       | ✅ Full         | Pure numeric computation                  |
| Metadata Extraction     | ✅ Full         | Element attribute access                  |
| JSON-LD Parsing         | ✅ Full         | `input(data, 'json')`                     |
| URL Resolution          | ⚠️ Partial     | Need `url_resolve()` function             |

### ⚠️ Features Requiring Minor Extensions

| Feature | Current Status | Proposed Solution |
|---------|---------------|-------------------|
| Regex `test()` | ✅ Available | Pattern matching with `is` operator |
| String `includes()` | ✅ Available | `contains(str, substr)` |
| String `trim()` | ✅ Available | `trim(str)`, `trim_start(str)`, `trim_end(str)` |
| String `split()` | ✅ Available | `split(str, delim)` |
| `toLowerCase()` | ❌ Missing | **Add `lower(str)`, `upper(str)`** |
| `replaceAll()` | ✅ Available | `replace(str, old, new)` |
| `startsWith()` | ✅ Available | `starts_with(str, prefix)` |
| `endsWith()` | ✅ Available | `ends_with(str, suffix)` |
| URL resolution | ❌ Missing | **Add `url_resolve(base, relative)`** |

### ❌ Blocking Issues

**NONE** - All core functionality can be implemented with current Lambda features plus minor string function additions.

## Architecture Design

### Data Model

Lambda's Element type naturally maps to DOM nodes:

```lambda
// DOM element in Lambda
<div class: "article" id: "content"
    <h1 "Article Title">
    <p "First paragraph...">
    <p "Second paragraph...">
>
```

### Proposed Module Structure

```
utils/
└── readability.ls          # Main implementation
    ├── parse(html)         # Main entry point
    ├── is_readable(doc)    # Quick readability check  
    └── Internal functions
```

## Core Algorithm Port

### 1. Content Scoring

The heart of Readability is scoring elements by "content-ness":

```lambda
// Scoring weights by tag
fn get_base_score(tag: symbol) int {
    if tag == 'div { 5 }
    else if tag in ['pre, 'td, 'blockquote] { 3 }
    else if tag in ['address, 'ol, 'ul, 'dl, 'dd, 'dt, 'li, 'form] { -3 }
    else if tag in ['h1, 'h2, 'h3, 'h4, 'h5, 'h6, 'th] { -5 }
    else { 0 }
}

// Class/ID weight
fn get_class_weight(elem) int {
    let class_name = elem.class ?? "";
    let id = elem.id ?? "";
    let weight = 0;
    
    // Negative indicators
    if match(class_name, NEGATIVE_REGEX) { let weight = weight - 25 }
    if match(id, NEGATIVE_REGEX) { let weight = weight - 25 }
    
    // Positive indicators  
    if match(class_name, POSITIVE_REGEX) { let weight = weight + 25 }
    if match(id, POSITIVE_REGEX) { let weight = weight + 25 }
    
    weight
}
```

### 2. Pattern Constants

```lambda
// Regular expressions for content classification
let UNLIKELY_CANDIDATES = 
    "-ad-|ai2html|banner|breadcrumbs|combx|comment|community|" +
    "cover-wrap|disqus|extra|footer|gdpr|header|legends|menu|" +
    "related|remark|replies|rss|shoutbox|sidebar|skyscraper|" +
    "social|sponsor|supplemental|ad-break|agegate|pagination|" +
    "pager|popup|yom-remote";

let OK_MAYBE_CANDIDATE = "and|article|body|column|content|main|mathjax|shadow";

let POSITIVE = "article|body|content|entry|hentry|h-entry|main|page|" +
               "pagination|post|text|blog|story";

let NEGATIVE = "-ad-|hidden|^hid$| hid$| hid |^hid |banner|combx|" +
               "comment|com-|contact|footer|gdpr|masthead|media|" +
               "meta|outbrain|promo|related|scroll|share|shoutbox|" +
               "sidebar|skyscraper|sponsor|shopping|tags|widget";
```

### 3. Main Parse Function

```lambda
// Main entry point
pub fn parse(html: string) {
    let doc = input(html, 'html);
    
    // Extract metadata first
    let metadata = get_article_metadata(doc);
    
    // Prepare document
    let prepared = prep_document(doc);
    
    // Grab the article content
    let content = grab_article(prepared);
    
    if content == null { null }
    else {
        // Post-process
        let processed = post_process(content);
        
        {
            title: metadata.title,
            byline: metadata.byline,
            content: format(processed, 'html),
            text_content: get_text_content(processed),
            length: len(get_text_content(processed)),
            excerpt: metadata.excerpt,
            site_name: metadata.site_name,
            lang: get_lang(doc)
        }
    }
}
```

### 4. Element Scoring

```lambda
fn score_paragraph(elem, ancestors) {
    let inner_text = get_inner_text(elem);
    
    // Skip short paragraphs
    if len(inner_text) < 25 { return }
    
    let content_score = 1;
    
    // Add points for commas
    let content_score = content_score + count_char(inner_text, ',');
    
    // Add points per 100 chars (max 3)
    let content_score = content_score + min(floor(len(inner_text) / 100), 3);
    
    // Distribute score to ancestors
    for (i, ancestor in enumerate(ancestors)) {
        if i == 0 { 
            // Parent gets full score
            add_score(ancestor, content_score)
        } else if i == 1 {
            // Grandparent gets half
            add_score(ancestor, content_score / 2)
        } else {
            // Others get less
            add_score(ancestor, content_score / (i * 3))
        }
    }
}
```

### 5. Link Density Calculation

```lambda
fn get_link_density(elem) float {
    let text_length = len(get_inner_text(elem));
    if text_length == 0 { return 0.0 }
    
    let link_length = sum(
        for link in find_all(elem, "a") {
            let href = link.href ?? "";
            let coeff = if starts_with(href, "#") { 0.3 } else { 1.0 };
            len(get_inner_text(link)) * coeff
        }
    );
    
    link_length / text_length
}
```

## Required String Functions

Most string functions are already available in Lambda. Here's the current status:

### Already Available ✅

```lambda
// String manipulation
trim(s)                 // Remove leading/trailing whitespace
trim_start(s)           // Remove leading whitespace
trim_end(s)             // Remove trailing whitespace
split(s, delim)         // Split by delimiter
contains(s, sub)        // Check substring
starts_with(s, prefix)  // Check prefix
ends_with(s, suffix)    // Check suffix
replace(s, from, to)    // Replace occurrences

// Pattern matching (via `is` operator)
str is pattern          // Test if matches pattern
```

### Missing Functions (Need Implementation)

```lambda
// Case conversion
fn lower(s: string) string          // Convert to lowercase
fn upper(s: string) string          // Convert to uppercase

// URL operations
fn url_resolve(base: string, relative: string) string
fn url_parse(url: string) {scheme, host, path, query, fragment}
```

### Implementation Priority

| Function | Priority | Status | Used In |
|----------|----------|--------|---------|
| `trim()` | High | ✅ Done | Text extraction, comparison |
| `split()` | High | ✅ Done | Tokenization, word counting |
| `contains()` | High | ✅ Done | Pattern checking |
| `starts_with()` | High | ✅ Done | URL/class checking |
| `ends_with()` | High | ✅ Done | URL/class checking |
| `replace()` | High | ✅ Done | Content cleaning |
| `lower()` | **High** | ❌ Missing | Case-insensitive matching |
| `upper()` | Low | ❌ Missing | Rarely needed |
| `url_resolve()` | Medium | ❌ Missing | Link fixing |

## DOM Manipulation Requirements

Lambda already supports the needed DOM operations:

```lambda
// Element creation
let div = <div class: "content">

// Child iteration
for child in elem.children {
    // process
}

// Attribute access
let class_name = elem.class ?? ""
let id = elem.id ?? ""

// Text content
let text = text_content(elem)

// Element removal (via MarkEditor)
// Need: remove_child(parent, child)
// Need: replace_child(parent, old, new)
```

### Required MarkEditor Extensions

The current `MarkEditor` needs these operations:

```cpp
// Needed for Readability port
void remove_node(Element* node);
void replace_node(Element* old_node, Element* new_node);
void set_attribute(Element* elem, const char* name, Item value);
void remove_attribute(Element* elem, const char* name);
void set_tag(Element* elem, const char* new_tag);
```

## Metadata Extraction

```lambda
fn get_article_metadata(doc) {
    // Try JSON-LD first
    let json_ld = get_json_ld(doc);
    
    // Then meta tags
    let meta = get_meta_tags(doc);
    
    {
        title: json_ld.title ?? meta.title ?? get_title_from_dom(doc),
        byline: json_ld.byline ?? meta.author,
        excerpt: json_ld.excerpt ?? meta.description,
        site_name: json_ld.site_name ?? meta.og_site_name,
        published_time: json_ld.published ?? meta.article_published_time
    }
}

fn get_json_ld(doc) {
    let scripts = find_all(doc, "script[type='application/ld+json']");
    
    for script in scripts {
        let content = text_content(script);
        let parsed = input(content, 'json);
        
        if parsed."@type" in ARTICLE_TYPES {
            return {
                title: parsed.headline ?? parsed.name,
                byline: get_author_name(parsed.author),
                excerpt: parsed.description,
                site_name: parsed.publisher?.name,
                published: parsed.datePublished
            }
        }
    }
    
    null
}
```

## Testing Strategy

### Test Cases from Original

Readability.js has extensive test cases that can be ported:

```
test/
├── test-pages/           # ~100+ real-world test cases
│   ├── 001-nytimes/
│   │   ├── source.html   # Input HTML
│   │   ├── expected.html # Expected output
│   │   └── expected-metadata.json
│   ├── 002-medium/
│   └── ...
```

### Lambda Test Structure

```lambda
// test/readability/test_nytimes.ls
import readability: .utils.readability;

let source = input(@./fixtures/nytimes.html);
let expected = input(@./fixtures/nytimes_expected.json);

let result = readability.parse(source);

assert(result.title == expected.title);
assert(contains(result.content, expected.content_snippet));
assert(result.byline == expected.byline);
```

## Implementation Plan

### Phase 1: Core Infrastructure (Week 1)
1. Add string functions: `trim`, `lower`, `split`, `contains`, `match`
2. Add DOM manipulation to MarkEditor
3. Create basic module structure

### Phase 2: Scoring Algorithm (Week 2)  
1. Port content scoring logic
2. Port link density calculation
3. Port class weight scoring
4. Implement candidate selection

### Phase 3: Content Extraction (Week 3)
1. Port `_grabArticle` main loop
2. Port cleaning functions (`_clean`, `_cleanConditionally`)
3. Port `_prepArticle`

### Phase 4: Metadata & Finishing (Week 4)
1. Port metadata extraction (JSON-LD, meta tags)
2. Port `_postProcessContent`
3. Port `isProbablyReaderable`
4. Testing and refinement

## API Design

### Public API

```lambda
// utils/readability.ls

/// Parse HTML and extract readable content
/// Returns: {title, byline, content, textContent, length, excerpt, siteName, lang, dir, publishedTime}
/// 
/// Key: result.content is a NEW element tree, not a reference to input.
/// The input document is never modified (pure functional approach).
pub fn parse(html: string) {...}

/// Parse file and extract readable content
pub fn parse_file(file_path: path) {...}

/// Parse and save to file (procedure - handles I/O)
pub pn parse_and_save(html: string, output_path: path) {...}

/// Parse file and save clean version (procedure - handles I/O)  
pub pn parse_file_and_save(input_path: path, output_path: path) {...}

/// Get clean HTML string from content
pub fn to_html(html: string) string {...}

/// Extract just the article content element
pub fn extract_article(html: string) element? {...}

/// Quick check if document is probably readable
pub fn is_readable(html: string, options?: {
    min_content_length?: int,
    min_score?: int
}) bool {...}
```

### Usage Example

```lambda
import readability: .utils.readability

// Simple usage - get metadata and content
let article = readability.parse(html_content)
print(article.title)
print(article.textContent)

// Save extracted content to file
readability.parse_and_save(html_content, @./readable.html)

// Get HTML string for further processing
let clean_html = readability.to_html(html_content)

// Parse from file and save clean version
readability.parse_file_and_save(@./messy.html, @./clean.html)

// Check readability first
if readability.is_readable(html_content, null, null) {
    let article = readability.parse(html_content)
    // process...
}
```

### Functional Design Principles

1. **No Input Mutation**: The input HTML document is never modified
2. **New Output**: `result.content` is a new element tree, independent of input
3. **Pure Functions**: All `fn` declarations are pure (no side effects)
4. **Procedures for I/O**: `pn` declarations (like `parse_and_save`) handle file I/O
5. **Clean Text Extraction**: `result.textContent` provides script/style-free text

## Implementation Status (February 2026)

**Status: ✅ WORKING IMPLEMENTATION**

The readability module has been successfully implemented and tested.

**Location:** `utils/readability.ls` (~878 lines)

### Implemented API

| Function | Return Type | Description |
|----------|-------------|-------------|
| `parse(file_path)` | `map^` | Extract readable content with all metadata |
| `parse_doc(doc)` | `map` | Parse an already-loaded HTML element |
| `parse_and_save(input, output)` | `map^` | Parse and save cleaned HTML to file |
| `metadata(file_path)` | `map^` | Extract only metadata (faster than full parse) |
| `title(file_path)` | `string^` | Extract just the article title |
| `text(file_path)` | `string^` | Extract clean text content (no HTML) |
| `lang(file_path)` | `string^` | Get document language |
| `dir(file_path)` | `string^` | Get text direction (ltr/rtl) |
| `extract_article(file_path)` | `element^` | Extract just the article element |
| `is_readable(file_path, min_len, min_score)` | `bool^` | Check if document is readable |

### Return Structure

The `parse()` function returns:

```lambda
{
    title: string?,         // Article title
    byline: string?,        // Author info
    dir: string?,           // Text direction (ltr/rtl)
    content: element?,      // Extracted content element
    textContent: string,    // Clean text without HTML
    length: int,            // Text length
    excerpt: string?,       // Article excerpt/summary
    siteName: string?,      // Site name
    lang: string?,          // Language code
    publishedTime: string?  // Publication timestamp
}
```

### Usage Examples

```lambda
// IMPORTANT: Use non-aliased imports (aliased imports have a runtime bug)
import .utils.readability

// Parse an HTML file
let result = parse("./article.html")?

// Access extracted data
result.title       // "Article Title"
result.byline      // "By John Doe"
result.textContent // Clean text content
result.content     // <article> element
```

**Metadata only (faster):**
```lambda
import .utils.readability
let meta = metadata("./article.html")?
```

**Save cleaned content (in a procedure):**
```lambda
import .utils.readability

pn main() null^ {
    var result^err = parse_and_save("./input.html", "./output.html")
    if (err != null) { raise err }
    print("Cleaned article saved!")
    return null
}
```

### Implementation Notes

1. **File-Based Input Only**: Lambda's `input()` only accepts file paths, not raw HTML strings
2. **JSON-LD Disabled**: Would need `parse_json(string)` for inline JSON parsing
3. **Functional Design**: Produces NEW clean documents, never modifies input

### Test Files

Located in `temp/`:
- `test_readability.ls` - Full parse test
- `test_readability_metadata.ls` - Metadata extraction test
- `test_readability_save.ls` - Save functionality test (run mode)
- `test_readability_helpers.ls` - Helper function tests
- `test_article.html` - Sample HTML article

Run tests:
```bash
./lambda.exe temp/test_readability.ls
./lambda.exe temp/test_readability_metadata.ls
./lambda.exe run temp/test_readability_save.ls
```

### Known Issues & Workarounds

During implementation, several Lambda language issues were discovered (see `doc/Lambda_Known_Issues.md`):

1. **Aliased Imports Bug** (HIGH): `import alias: .path` causes runtime type_id=26 errors
   - *Workaround*: Use `import .path` without alias

2. **Compound Boolean in Where Clauses** (HIGH): `where x and y` doesn't filter correctly with nulls
   - *Workaround*: Compute conditions separately, then filter on result

3. **Float × Int Multiplication** (MEDIUM): `sqrt(1.0 * int_val)` causes "unknown mul type: 5, 4" error
   - *Workaround*: Use integer arithmetic only

4. **Missing `float()` Function** (MEDIUM): No runtime function to convert int to float
   - *Workaround*: Avoid float conversion

---

## Original Design Proposal

The sections below document the original feasibility analysis and design work.

---

## Conclusion

**Porting Readability.js to Lambda Script is HIGHLY FEASIBLE** with minimal additions needed:

### Already Available ✅
- HTML parsing via `input(html, 'html)`
- DOM traversal via `ElementReader`, `MarkReader`
- All core string functions: `trim`, `split`, `contains`, `starts_with`, `ends_with`, `replace`
- Pattern matching via `is` operator
- Element manipulation via `MarkBuilder`, `MarkEditor`
- Maps, arrays, lists for data structures

### Minor Additions Needed ⚠️
- **`lower(str)`** - Case conversion for case-insensitive matching (HIGH PRIORITY)
- **`upper(str)`** - Case conversion (LOW PRIORITY)
- **`url_resolve(base, rel)`** - For fixing relative URLs (MEDIUM PRIORITY)

### Nice to Have (Enhancements)
- CSS selector support (currently tag-name only)
- Enhanced DOM editing capabilities in MarkEditor

The core content-scoring algorithm and DOM analysis are purely computational and map directly to Lambda's functional paradigm. **Only 2-3 new string functions are needed** to complete the port.

**Estimated effort**: 2-3 weeks for complete port with testing
**Risk level**: Very Low - almost all infrastructure already exists
