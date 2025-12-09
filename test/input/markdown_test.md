# Lambda Markdown Viewer Test

This is a comprehensive test document for the Lambda markdown viewer with GitHub-like styling.

## Headings

# H1 - Main Title
## H2 - Section Title
### H3 - Subsection
#### H4 - Minor Section
##### H5 - Small Section
###### H6 - Smallest Section

---

## Text Formatting

**Bold text** and __bold with underscores__

*Italic text* and _italic with underscores_

**_Bold and italic combined_**

~~Strikethrough text~~

`Inline code` with backticks

---

## Lists

### Unordered Lists

- First item
- Second item
  - Nested item 1
  - Nested item 2
    - Deeply nested
- Third item

### Ordered Lists

1. First item
2. Second item
   1. Nested numbered
   2. Another nested
3. Third item

### Task Lists

- [x] Completed task
- [ ] Incomplete task
- [ ] Another task

---

## Links and Images

[Link to Lambda project](https://github.com/)

[Link with title](https://github.com/ "GitHub Homepage")

![Alt text for image](https://via.placeholder.com/150)

---

## Code Blocks

### Inline Code

Use `console.log()` to print output.

### Code Block with Language

```python
def hello_world():
    print("Hello, Lambda!")
    return 42

result = hello_world()
```

```javascript
function fibonacci(n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

console.log(fibonacci(10));
```

### Generic Code Block

```
Plain text code block
without syntax highlighting
```

---

## Blockquotes

> This is a blockquote.
> It can span multiple lines.
>
> > Nested blockquote
>
> Back to first level.

---

## Tables

| Header 1 | Header 2 | Header 3 |
|----------|----------|----------|
| Cell 1   | Cell 2   | Cell 3   |
| Cell 4   | Cell 5   | Cell 6   |
| Cell 7   | Cell 8   | Cell 9   |

### Table with Alignment

| Left | Center | Right |
|:-----|:------:|------:|
| A    | B      | C     |
| 1    | 2      | 3     |

---

## Horizontal Rules

Above rule

---

Below rule

***

Another rule

___

Last rule

---

## Emphasis Combinations

*Italic*, **bold**, ***bold italic***

_Italic_, __bold__, ___bold italic___

**Bold with _nested italic_**

*Italic with **nested bold***

---

## Special Characters

Ampersand: &

Less than: <

Greater than: >

Quote: "

Apostrophe: '

---

## HTML Entities (if supported)

&copy; Copyright

&trade; Trademark

&nbsp; Non-breaking space

---

## Escaped Characters

\*Not italic\*

\[Not a link\]

\`Not code\`

---

## Math (if supported)

Inline math: $E = mc^2$

Block math:

$$
\int_{0}^{\infty} e^{-x^2} dx = \frac{\sqrt{\pi}}{2}
$$

---

## Footnotes (if supported)

Here is a sentence with a footnote[^1].

[^1]: This is the footnote content.

---

## Definition Lists (if supported)

Term 1
: Definition 1

Term 2
: Definition 2a
: Definition 2b

---

## Abbreviations (if supported)

The HTML specification is maintained by the W3C.

*[HTML]: HyperText Markup Language
*[W3C]: World Wide Web Consortium

---

## Summary

This document tests:

1. ✅ Headings (H1-H6)
2. ✅ Text formatting (bold, italic, strikethrough)
3. ✅ Lists (ordered, unordered, nested)
4. ✅ Code blocks and inline code
5. ✅ Blockquotes
6. ✅ Tables
7. ✅ Links and images
8. ✅ Horizontal rules
9. ✅ Special characters
10. ✅ Various markdown features

---

## End of Test Document

Thank you for using Lambda Markdown Viewer! 🚀
