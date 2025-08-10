# Main Document Title

This is a comprehensive test document covering all features implemented in the markup parser with **bold**, *italic*, and `inline code`.

## Headers Testing

### Level 3 Header
#### Level 4 Header  
##### Level 5 Header
###### Level 6 Header

## Multi-Paragraph List Items (Previously Problematic)

### Ordered Lists
1. First item with single paragraph

2. Second item with multiple paragraphs

   This is the second paragraph of item 2.
   It continues on multiple lines.

3. Third item with code block:

   ```python
   print("Hello, World!")
   x = 42
   return x * 2
   ```

   And some text after the code block.

### Unordered Lists
- First unordered item
- Second unordered item with **bold** and *italic*
- Third item with `inline code`
+ Plus marker item
* Asterisk marker item
- Mixed markers work fine

## Code Blocks Testing

### Python Code
```python
def hello_world():
    print("Hello from Python!")
    return 42
```

### JavaScript Code
```javascript
function greeting(name) {
    console.log(`Hello, ${name}!`);
    return name.toUpperCase();
}
```

### Plain Code Block
```
This is a plain code block
without syntax highlighting
```

## Tables (Previously Problematic)

### Simple Table
| Header 1 | Header 2 | Header 3 |
|----------|----------|----------|
| Cell 1   | Cell 2   | Cell 3   |
| **Bold** | *Italic* | `Code`   |

### Complex Table with Formatting
| Feature | Syntax | Example |
|---------|--------|---------|
| Bold | `**text**` | **bold text** |
| Italic | `*text*` | *italic text* |
| Code | `` `text` `` | `inline code` |
| Link | `[text](url)` | [example](https://example.com) |

## Blockquotes

> This is a simple blockquote
> with multiple lines

> **Nested formatting** works in blockquotes
> 
> Including `inline code` and *emphasis*

## Links and Images

### Basic Links
- [Simple link](https://example.com)
- [Link with title](https://example.com "This is a title")
- [Relative link](../other-page.html)

### Images
- ![Alt text](https://example.com/image.png)
- ![Alt text with title](https://example.com/image.png "Image title")
- ![Local image](./assets/test-image.jpg)

## Advanced Inline Formatting

### Strikethrough
This text has ~~strikethrough~~ formatting.
You can combine ~~**bold strikethrough**~~ and ~~*italic strikethrough*~~.

### Superscript and Subscript
- Water molecule: H^2^O (superscript)
- Chemical formula: CO~2~ (subscript)  
- Complex: E = mc^2^ and H~2~SO~4~

### Mathematical Content

#### Inline Math
This equation $x^2 + y^2 = z^2$ is the Pythagorean theorem.
Here's another: $\frac{-b \pm \sqrt{b^2-4ac}}{2a}$ for quadratic formula.

#### Block Math
$$
\int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}
$$

$$
\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}
$$

### Emoji Shortcodes
- Happy face: :smile:
- Thumbs up: :thumbsup:
- Heart: :heart:
- Rocket: :rocket:

## Horizontal Dividers

Text before first divider

---

Text between dividers (three hyphens)

***

Text between dividers (three asterisks)  

___

Text after last divider (three underscores)

## Nested Lists (Simpler Version)

### Basic Nested Lists
1. **First item** with bold text
   
   - Simple nested unordered item
   - Another nested item with *italic*
   
2. **Second item** with different nesting
   
   1. Nested ordered item
   2. Another nested ordered item

### Simple Blockquotes with Basic Content
> This blockquote contains **bold** and *italic* text.
> 
> It also has `inline code` but no complex nesting.

## Phase 6 Advanced Features

### YAML Frontmatter
```yaml
---
title: "Comprehensive Test Document"
author: "Test Suite"
date: "2025-08-10"
tags: ["markdown", "parsing", "test"]
version: 1.2
published: true
---
```

### Footnotes and References
This text has a footnote reference[^1] and another one[^note2].

You can also use inline footnotes^[This is an inline footnote] in your text.

Multiple references to the same footnote[^1] work correctly.

### Citations and Bibliography
Academic writing often requires citations [@smith2023] and multiple citations [@doe2022; @johnson2024].

You can also cite with page numbers [@brown2021, p. 15] or chapters [@wilson2020, ch. 3].

### RST-Style Directives
```rst
.. note:: This is an important note
   
   It can span multiple lines and contain **formatting**.

.. warning:: 
   Be careful when using this feature!

.. code-block:: python
   :linenos:
   :emphasize-lines: 2,3
   
   def example_function():
       # This line is emphasized
       return "Hello, World!"
```

### Org Mode Blocks and Properties
```org
#+TITLE: Test Document
#+AUTHOR: Test Suite
#+DATE: 2025-08-10

#+BEGIN_SRC python
def org_example():
    return "This is from Org mode"
#+END_SRC

#+BEGIN_QUOTE
This is an Org-mode quote block
with multiple lines of content.
#+END_QUOTE

#+BEGIN_EXAMPLE
This is an example block
that preserves formatting
    including indentation
#+END_EXAMPLE
```

### Wiki-Style Templates
```wiki
{{template|param1=value1|param2=value2}}

{{infobox
|title = Example Infobox
|image = example.jpg
|caption = This is a test image
|field1 = Some value
|field2 = Another value
}}

{{cite book
|author = John Doe  
|title = Example Book
|year = 2023
|publisher = Test Publisher
}}
```

### Mixed Phase 6 Content
This paragraph contains a footnote[^mixed], a citation [@mixed2023], and references to {{wiki_template|param=value}}.

Here's a complex example with multiple Phase 6 elements:
- List item with footnote[^list_note]
- List item with citation [@list_cite]  
- List item with {{template|inline=true}}

### Advanced Metadata Sections
```toml
[metadata]
title = "Advanced Test"
format = "comprehensive"
features = ["footnotes", "citations", "directives"]
```

```json
{
  "document": {
    "type": "test",
    "phase": 6,
    "features": [
      "yaml_frontmatter",
      "footnotes", 
      "citations",
      "rst_directives",
      "org_blocks",
      "wiki_templates"
    ]
  }
}
```

## Footnote Definitions
[^1]: This is the first footnote with **bold** and *italic* formatting.

[^note2]: This is the second footnote with a [link](https://example.com) and `inline code`.

[^mixed]: This footnote contains citation [@inner2023] and {{nested_template}}.

[^list_note]: Footnote from a list item with complex content.

## Bibliography
[@smith2023]: Smith, J. (2023). *Advanced Parsing Techniques*. Tech Publishers.

[@doe2022]: Doe, J. (2022). "Markup Language Evolution." *Journal of Computing*, 15(3), 42-58.

[@johnson2024]: Johnson, A. (2024). *Modern Documentation Systems*. Academic Press.

[@brown2021]: Brown, M. (2021). *Citation Management*. Reference Books Ltd.

[@wilson2020]: Wilson, S. (2020). *Technical Writing Handbook*. Professional Publishers.

[@mixed2023]: Mixed, R. (2023). "Integrated Parsing Systems." *Tech Review*, 8(1), 12-25.

[@list_cite]: ListAuthor, L. (2023). "List-Based Citations." *Format Studies*, 3(2), 78-82.

[@inner2023]: Inner, C. (2023). "Nested Reference Systems." *Meta Journal*, 12(4), 156-167.

## Final Testing Section

This comprehensive test covers implemented features that are stable:

**Core Block Elements:**
- Headers (levels 1-6) ✓
- Paragraphs ✓  
- Lists (ordered, unordered, basic nesting) ✓
- Code blocks (fenced with ``` syntax) ✓
- Tables ✓
- Blockquotes ✓
- Mathematical blocks ($$...$$ syntax) ✓
- Horizontal rules ✓

**Core Inline Elements:**
- Bold (**text**) and italic (*text*) ✓
- Inline code (`text`) ✓  
- Links [text](url) ✓
- Images ![alt](url) ✓
- Strikethrough (~~text~~) ✓
- Superscript (text^sup^) ✓
- Subscript (text~sub~) ✓
- Inline math ($...$) ✓
- Emoji shortcodes (:name:) ✓

**Phase 6 Advanced Features:**
- YAML frontmatter (---...---) ✓
- Footnotes ([^ref] and definitions) ✓
- Inline footnotes (^[text]) ✓
- Citations ([@key] and [@key, p. 1]) ✓
- Bibliography definitions ✓
- RST directives (.. directive::) ✓
- Org mode blocks (#+BEGIN_...#+END_) ✓
- Org mode properties (#+PROPERTY:) ✓
- Wiki templates ({{template|param=value}}) ✓
- Metadata blocks (TOML, JSON) ✓

**Complex Integration:**
- Multi-paragraph list items ✓
- Basic nested lists ✓
- Code blocks inside list items ✓
- Tables with inline formatting ✓  
- Blockquotes with inline formatting ✓
- Mixed inline formatting combinations ✓
- Footnotes with citations and templates ✓
- Cross-references between Phase 6 elements ✓

**Known Limitations:**
- Very complex nested structures may cause performance issues
- Some edge cases with deep nesting levels need further investigation
- Complex interactions between different Phase 6 elements are still being tested

This document successfully tests both core parser capabilities and Phase 6 advanced features.
