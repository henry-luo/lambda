---
title: "Lambda Validator Markdown Test"
author: "Test Author"
date: 2024-07-28T10:30:00Z
tags: ["testing", "validation", "markdown", "lambda"]
description: "Comprehensive Markdown document for testing Lambda validator"
version: 1.0
---

# Lambda Validator Markdown Test

This is a comprehensive Markdown document designed to test all aspects of the Lambda validator with Markdown input.

## Introduction

The Lambda validator supports multiple input formats, including **Markdown**. This document contains various Markdown elements to ensure comprehensive testing coverage.

### Features Tested

- [x] Headers (H1-H6)
- [x] Paragraphs with *emphasis* and **strong** text
- [x] Lists (ordered and unordered)
- [x] Links and images
- [x] Code blocks and inline code
- [x] Tables
- [x] Blockquotes
- [x] Horizontal rules

## Content Examples

### Text Formatting

This paragraph demonstrates various **bold text**, *italic text*, ***bold and italic***, `inline code`, and ~~strikethrough~~ formatting options.

You can also use <u>underlined text</u> and <mark>highlighted text</mark> using HTML tags within Markdown.

### Links and References

Here are some example links:
- [External link](https://example.com "Example Website")
- [Internal link](#code-examples "Jump to code section")
- [Email link](mailto:test@example.com)
- [Reference-style link][ref-link]

[ref-link]: https://github.com/example/repo "Reference Link"

### Images

![Test Image](https://example.com/test-image.jpg "A test image for validation")

![Local Image](./assets/local-image.png "Local image example")

### Lists

#### Unordered List
- First item
- Second item with **bold text**
- Third item
  - Nested item 1
  - Nested item 2
    - Deeply nested item
- Fourth item with `code`

#### Ordered List
1. First numbered item
2. Second numbered item
3. Third numbered item
   1. Nested numbered item
   2. Another nested item
4. Fourth numbered item

#### Task List
- [x] Completed task
- [ ] Incomplete task
- [x] Another completed task
- [ ] Task with [link](https://example.com)

### Code Examples

Here's some `inline code` within a sentence.

#### JavaScript Code Block
```javascript
// Example JavaScript code
function validateSchema(schema, data) {
    try {
        const result = schema.validate(data);
        return {
            valid: result.valid,
            errors: result.errors || []
        };
    } catch (error) {
        return {
            valid: false,
            errors: [error.message]
        };
    }
}

// Usage example
const schema = new Schema(schemaDefinition);
const validation = validateSchema(schema, testData);
console.log('Validation result:', validation);
```

#### Python Code Block
```python
# Example Python code
import json
from typing import Dict, List, Any

def validate_document(schema: Dict[str, Any], document: Dict[str, Any]) -> bool:
    """
    Validate a document against a schema
    """
    try:
        # Validation logic here
        for field, rules in schema.items():
            if field not in document:
                if rules.get('required', False):
                    return False
            else:
                value = document[field]
                if not validate_field(value, rules):
                    return False
        return True
    except Exception as e:
        print(f"Validation error: {e}")
        return False

# Test the validation
result = validate_document(test_schema, test_document)
print(f"Document is valid: {result}")
```

#### Plain Code Block
```
This is a plain code block without syntax highlighting.
It can contain any kind of text content.

Configuration example:
server.port = 8080
server.host = localhost
database.url = jdbc:postgresql://localhost/testdb
```

### Tables

| Feature | Status | Priority | Notes |
|---------|--------|----------|-------|
| HTML Input | ✅ Complete | High | Fully implemented |
| Markdown Input | 🔄 In Progress | High | Currently testing |
| Schema Validation | ✅ Complete | Critical | Core functionality |
| Error Reporting | ⏳ Planned | Medium | Future enhancement |
| Performance | 🔄 In Progress | Low | Optimization needed |

#### Complex Table

| Type | Syntax | Example | Valid | Description |
|------|---------|---------|-------|-------------|
| String | `string` | `"hello"` | ✅ | Basic text |
| Integer | `int` | `42` | ✅ | Whole numbers |
| Float | `float` | `3.14` | ✅ | Decimal numbers |
| Boolean | `bool` | `true` | ✅ | True/false values |
| Array | `type[]` | `[1,2,3]` | ✅ | List of items |
| Optional | `type?` | `string?` | ✅ | May be null |

### Blockquotes

> This is a simple blockquote demonstrating quoted content.
> It can span multiple lines and contain **formatting**.

> #### Quote with Header
> This blockquote contains a header and multiple paragraphs.
> 
> It demonstrates more complex quote structures that might be found
> in documentation or articles.
> 
> > This is a nested blockquote within the main quote.
> > It shows how quotes can be layered.

### Horizontal Rules

Here's a horizontal rule:

---

And another style:

***

And yet another:

___

## Advanced Markdown Features

### Definition Lists

Term 1
: Definition for term 1

Term 2
: Definition for term 2
: Additional definition for term 2

Complex Term
: This definition spans multiple lines and contains **formatting**.
  It can include `code` and [links](https://example.com).

### Footnotes

This sentence has a footnote[^1]. And this one has another[^note].

[^1]: This is the first footnote.
[^note]: This is a named footnote with more content.
    
    It can even contain multiple paragraphs and code blocks:
    
    ```
    Code in footnote
    ```

### Math (if supported)

Inline math: $E = mc^2$

Block math:
$$
\int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}
$$

### HTML Embedded in Markdown

<div class="custom-container">
    <h4>HTML Section</h4>
    <p>This section uses <span style="color: red;">HTML tags</span> within Markdown.</p>
    <details>
        <summary>Click to expand</summary>
        <p>Hidden content that can be revealed.</p>
    </details>
</div>

## Conclusion

This document demonstrates the comprehensive testing capabilities of the Lambda validator with Markdown input. It covers:

1. **Basic formatting** - emphasis, strong, code
2. **Structural elements** - headers, lists, tables
3. **Media content** - links, images
4. **Code blocks** - various languages and plain text
5. **Advanced features** - footnotes, definition lists, HTML embedding

The validator should be able to parse and validate all these elements according to the defined schema.

---

*End of test document - Generated for Lambda Validator testing purposes*
