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

## Final Testing Section

This comprehensive test covers implemented features that are stable:

**Block Elements:**
- Headers (levels 1-6) ✓
- Paragraphs ✓  
- Lists (ordered, unordered, basic nesting) ✓
- Code blocks (fenced with ``` syntax) ✓
- Tables ✓
- Blockquotes ✓
- Mathematical blocks ($$...$$ syntax) ✓
- Horizontal rules ✓

**Inline Elements:**
- Bold (**text**) and italic (*text*) ✓
- Inline code (`text`) ✓  
- Links [text](url) ✓
- Images ![alt](url) ✓
- Strikethrough (~~text~~) ✓
- Superscript (text^sup^) ✓
- Subscript (text~sub~) ✓
- Inline math ($...$) ✓
- Emoji shortcodes (:name:) ✓

**Complex Features:**
- Multi-paragraph list items ✓
- Basic nested lists ✓
- Code blocks inside list items ✓
- Tables with inline formatting ✓  
- Blockquotes with inline formatting ✓
- Mixed inline formatting combinations ✓

**Known Limitations:**
- Very complex nested structures (list items containing blockquotes with nested lists) may cause infinite loops
- Some edge cases with deep nesting levels need further investigation

This document successfully tests the core parser capabilities without triggering infinite loops.
