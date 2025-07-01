# Comprehensive Markdown Test Document

This document tests all common Markdown syntax elements to ensure proper parsing and rendering.

## Headers

# H1 Header
## H2 Header
### H3 Header
#### H4 Header
##### H5 Header
###### H6 Header

Alternative H1
==============

Alternative H2
--------------

## Text Formatting

**Bold text** and __also bold text__

*Italic text* and _also italic text_

***Bold and italic*** and ___also bold and italic___

~~Strikethrough text~~

`Inline code` and `more inline code`

## Line Breaks and Paragraphs

This is a paragraph with a  
hard line break (two spaces at end).

This is a new paragraph separated by a blank line.

This is another paragraph.
This line has no line break before it.

## Lists

### Unordered Lists

- Item 1
- Item 2
  - Nested item 2.1
  - Nested item 2.2
    - Double nested item 2.2.1
    - Double nested item 2.2.2
- Item 3

* Alternative bullet style
* Another item
  * Nested with asterisk

+ Plus sign bullets
+ Another plus item

### Ordered Lists

1. First item
2. Second item
   1. Nested ordered item
   2. Another nested item
      1. Double nested
      2. Another double nested
3. Third item

1. First item
1. Second item (auto-numbered)
1. Third item (auto-numbered)

### Mixed Lists

1. Ordered item
   - Unordered nested item
   - Another unordered nested item
2. Another ordered item
   1. Ordered nested item
   2. Another ordered nested item

### Task Lists

- [x] Completed task
- [ ] Incomplete task
- [x] Another completed task
- [ ] Another incomplete task

## Links

[Inline link](https://www.example.com)

[Link with title](https://www.example.com "This is a title")

[Reference link][1]

[Another reference link][reference-id]

[Case-insensitive reference link][REFERENCE-ID]

Direct URL: https://www.example.com

Email: user@example.com

<https://www.autolink.com>

<user@autolink.com>

[1]: https://www.reference1.com
[reference-id]: https://www.reference2.com "Reference with title"

## Images

![Alt text](image.jpg)

![Alt text with title](image.jpg "Image title")

![Reference image][image-ref]

[image-ref]: image-reference.jpg "Reference image title"

## Code

### Inline Code

Use `console.log()` to print to console.

Variables like `userName` and `userAge`.

### Code Blocks

```
Plain code block
No syntax highlighting
Multiple lines supported
```

```javascript
// JavaScript code block
function greetUser(name) {
    console.log(`Hello, ${name}!`);
    return true;
}

const users = ['Alice', 'Bob', 'Charlie'];
users.forEach(greetUser);
```

```python
# Python code block
def calculate_area(radius):
    """Calculate the area of a circle."""
    import math
    return math.pi * radius ** 2

# Usage example
area = calculate_area(5)
print(f"Area: {area:.2f}")
```

```html
<!-- HTML code block -->
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Sample HTML</title>
</head>
<body>
    <h1>Hello World</h1>
    <p>This is a paragraph.</p>
</body>
</html>
```

```css
/* CSS code block */
.container {
    max-width: 1200px;
    margin: 0 auto;
    padding: 20px;
}

.header {
    background-color: #333;
    color: white;
    text-align: center;
}
```

    Indented code block (4 spaces)
    function indentedCode() {
        return "This is indented";
    }

## Blockquotes

> This is a blockquote.
> It can span multiple lines.
> 
> And can contain multiple paragraphs.

> Blockquotes can be nested.
> 
> > This is a nested blockquote.
> > 
> > > And this is double nested.

> **Blockquotes** can contain *formatting*
> 
> 1. And lists
> 2. Both ordered
> 
> - And unordered
> - Lists work too
> 
> ```javascript
> // Even code blocks
> console.log("Inside blockquote");
> ```

## Tables

| Column 1 | Column 2 | Column 3 |
|----------|----------|----------|
| Row 1    | Data     | More data|
| Row 2    | Info     | More info|

| Left Aligned | Center Aligned | Right Aligned |
|:-------------|:--------------:|--------------:|
| Left         | Center         | Right         |
| Text         | Text           | Text          |

| Name | Age | City |
|------|-----|------|
| John | 25  | NYC  |
| Jane | 30  | LA   |
| Bob  | 35  | Chicago |

## Horizontal Rules

---

***

___

- - -

* * *

_ _ _

## Escaping Characters

\*This is not italic\*

\`This is not code\`

\# This is not a header

\[This is not a link\]

\\This shows a backslash

\> This is not a blockquote

## HTML in Markdown

<div>
    <p>Raw HTML is supported in Markdown.</p>
    <strong>Bold HTML tag</strong>
    <em>Italic HTML tag</em>
</div>

<details>
<summary>Collapsible section</summary>

This content is hidden by default and can be expanded.

- It can contain **Markdown** too
- Including `code` and [links](https://example.com)

</details>

<kbd>Ctrl</kbd> + <kbd>C</kbd> to copy

Text with <mark>highlighted</mark> section.

## Special Characters and Symbols

Copyright: &copy;
Trademark: &trade;
Registered: &reg;
Less than: &lt;
Greater than: &gt;
Ampersand: &amp;

Mathematical symbols: &alpha; &beta; &gamma; &sum; &prod;

## Footnotes

This text has a footnote[^1].

Another footnote reference[^note].

[^1]: This is the first footnote.
[^note]: This is a named footnote with more content.
    
    It can have multiple paragraphs.
    
    And even code blocks:
    
    ```
    footnote code example
    ```

## Abbreviations

*[HTML]: HyperText Markup Language
*[CSS]: Cascading Style Sheets
*[JS]: JavaScript

HTML and CSS are fundamental web technologies.
JS adds interactivity to web pages.

## Definition Lists

Term 1
:   Definition of term 1

Term 2
:   Definition of term 2
:   Another definition of term 2

## Line Breaks and Whitespace

This line ends with two spaces  
So this line appears below it.

This line ends with a backslash\
So this line also appears below it.


Multiple blank lines above (should be preserved as single blank line in output).

## Edge Cases

### Empty Elements

[]()

![]()

### Special Link Cases

[Link with (parentheses) in title](https://example.com)

[Link with "quotes" in title](https://example.com)

### Code with Backticks

To show `code` with backticks: `` `code` ``

To show multiple backticks: ``` `` ```

### Mixed Formatting

**Bold with *italic* inside**

*Italic with **bold** inside*

**Bold with `code` inside**

`Code with **bold** inside` (bold won't render in code)

### Long Lines

This is a very long line that should be wrapped appropriately when rendered. It contains multiple words and should demonstrate how the parser handles long continuous text without any special formatting or line breaks in the middle of the content.

### Unicode and Emojis

Unicode characters: α β γ δ ε ζ η θ

Emojis: 😀 😃 😄 😁 🚀 ⭐️ 🌟 💫

Math symbols: ∑ ∏ ∫ ∂ ∞ ≠ ≤ ≥

## Complex Nested Structures

1. **Ordered list** with formatting
   
   > Blockquote inside list item
   > 
   > ```javascript
   > // Code block inside blockquote inside list
   > console.log("Nested structure");
   > ```
   
   - Unordered list inside ordered list
     - *Italic* formatting
     - **Bold** formatting
     - `Code` formatting
   
   | Table | Inside | List |
   |-------|--------|------|
   | Cell  | Data   | Here |

2. Another **ordered item** with [link](https://example.com)

## Conclusion

This document covers most common Markdown syntax elements including:

- Headers (H1-H6, alternative syntax)
- Text formatting (bold, italic, strikethrough, code)
- Lists (ordered, unordered, nested, tasks)
- Links (inline, reference, automatic)
- Images (inline, reference)
- Code blocks (fenced, indented, with syntax highlighting)
- Blockquotes (simple, nested, with formatting)
- Tables (basic, aligned)
- Horizontal rules
- HTML elements
- Special characters and escaping
- Advanced features (footnotes, abbreviations, definition lists)
- Edge cases and complex nested structures

---

*End of Markdown document*
