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

#### Simple Inline Math Tests
Test basic inline math: $x + y = z$ and $a = b$.

More complex: $x^2 + y^2 = z^2$ is the Pythagorean theorem.
Here's another: $\frac{-b \pm \sqrt{b^2-4ac}}{2a}$ for quadratic formula.

Simple inline math: $E = mc^2$ and complex: $\int_0^1 x dx = \frac{1}{2}$.

##### Advanced Inline Math Tests
Limit notation: $\lim_{x \to 0} \frac{\sin x}{x} = 1$ and derivatives: $\frac{d}{dx}[x^n] = nx^{n-1}$.

Summation and product: $\sum_{i=1}^n i = \frac{n(n+1)}{2}$ and $\prod_{i=1}^n i = n!$.

Greek letters in context: The golden ratio $\phi = \frac{1 + \sqrt{5}}{2}$ satisfies $\phi^2 = \phi + 1$.

Set membership: $x \in \mathbb{R}$, subset relation: $\mathbb{N} \subset \mathbb{Z} \subset \mathbb{Q} \subset \mathbb{R}$.

Complex analysis: $|z|^2 = z \bar{z}$ where $z = a + bi \in \mathbb{C}$.

Vector notation: $\mathbf{v} \cdot \mathbf{w} = |\mathbf{v}||\mathbf{w}|\cos\theta$.

Probability: $P(A \cap B) = P(A)P(B|A)$ for dependent events.

#### Simple Block Math Tests
Basic equation:
$$
x + y = z
$$

Square root:
$$
\sqrt{x^2 + y^2} = z
$$

Fraction:
$$
\frac{a}{b} = c
$$

#### Standard Block Math
$$
\int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}
$$

$$
\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}
$$

#### Complex Math (May Have Issues)
<!-- These are the problematic ones but won't hang -->
Aligned equations:
$$
\begin{aligned}
f(x) &= ax^2 + bx + c \\
f'(x) &= 2ax + b \\
f''(x) &= 2a
\end{aligned}
$$

Matrix example:
$$
\begin{pmatrix}
a & b \\
c & d
\end{pmatrix}
\begin{pmatrix}
x \\
y
\end{pmatrix}
=
\begin{pmatrix}
ax + by \\
cx + dy
\end{pmatrix}
$$

#### Advanced Mathematical Expressions

Inline math with subscripts: $x_1 + x_2 = \sum_{i=1}^{2} x_i$

##### Big Operators and Limits
$$\lim_{n \to \infty} \sum_{k=1}^{n} \frac{1}{k^2} = \frac{\pi^2}{6}$$

$$\prod_{p \text{ prime}} \left(1 - \frac{1}{p^2}\right) = \frac{6}{\pi^2}$$

$$\int_0^\infty \frac{\sin x}{x} dx = \frac{\pi}{2}$$

##### Complex Fractions and Nested Structures
$$\frac{1}{\sqrt{2\pi}} \int_{-\infty}^{\infty} e^{-\frac{x^2}{2}} dx = 1$$

$$\cfrac{1}{1+\cfrac{1}{1+\cfrac{1}{1+\cdots}}} = \frac{\sqrt{5}-1}{2}$$

##### Calculus and Analysis
$$\frac{\partial^2 u}{\partial t^2} = c^2 \nabla^2 u$$

$$\oint_C \mathbf{F} \cdot d\mathbf{r} = \iint_D \left(\frac{\partial Q}{\partial x} - \frac{\partial P}{\partial y}\right) dx\,dy$$

##### Set Theory and Logic
$$A \cup B = \{x : x \in A \lor x \in B\}$$

$$\forall \epsilon > 0, \exists \delta > 0 : |x - a| < \delta \Rightarrow |f(x) - f(a)| < \epsilon$$

##### Number Theory
$$\zeta(s) = \sum_{n=1}^{\infty} \frac{1}{n^s} = \prod_{p \text{ prime}} \frac{1}{1-p^{-s}}$$

$$\binom{n}{k} = \frac{n!}{k!(n-k)!}$$

##### Greek Letters and Special Symbols
$$\alpha\beta\gamma\delta\epsilon\zeta\eta\theta\iota\kappa\lambda\mu$$

$$\Gamma\Delta\Theta\Lambda\Xi\Pi\Sigma\Upsilon\Phi\Psi\Omega$$

$$\aleph_0 < \aleph_1 < \aleph_2$$

##### Complex Numbers and Trigonometry
$$e^{i\pi} + 1 = 0$$

$$\sin^2 x + \cos^2 x = 1$$

$$\tan(a + b) = \frac{\tan a + \tan b}{1 - \tan a \tan b}$$

##### Linear Algebra
$$\det(A) = \sum_{\sigma \in S_n} \text{sgn}(\sigma) \prod_{i=1}^n a_{i,\sigma(i)}$$

$$\mathbf{v} \times \mathbf{w} = \begin{vmatrix} \mathbf{i} & \mathbf{j} & \mathbf{k} \\ v_1 & v_2 & v_3 \\ w_1 & w_2 & w_3 \end{vmatrix}$$

##### Probability and Statistics
$$P(A|B) = \frac{P(B|A)P(A)}{P(B)}$$

$$\mathbb{E}[X] = \int_{-\infty}^{\infty} x f(x) dx$$

##### Physics Equations
$$E = mc^2$$

$$F = G\frac{m_1 m_2}{r^2}$$

$$\nabla \times \mathbf{E} = -\frac{\partial \mathbf{B}}{\partial t}$$

##### Stress Testing with Complex Nesting
$$\sum_{n=0}^{\infty} \frac{(-1)^n}{(2n)!} x^{2n} = \cos x$$

$$\int_0^1 \int_0^1 \frac{x^2 + y^2}{x^2 + y^2 + z^2} \, dx \, dy$$

$$\left\{ \begin{array}{ll}
x + y = 1 \\
x - y = 2
\end{array} \right.$$

### Emoji Shortcodes
- Happy face: :smile:
- Thumbs up: :thumbsup:
- Heart: :heart:
- Rocket: :rocket:

### Extended Emoji Support
Emoji support: :rocket: :heart: :100: :thumbsup: :fire: :bug: :bulb:

## Horizontal Dividers

Text before first divider

---

Text between dividers (three hyphens)

***

Text between dividers (three asterisks)  

___

Text after last divider (three underscores)

## Nested Lists (Simpler Version)

### Simple Nested Lists

Complex nested list structure:
- Top level item 1
  - Nested item 1.1
  - Nested item 1.2
    - Deep nested 1.2.1
    - Deep nested 1.2.2
  - Nested item 1.3
- Top level item 2
  - Another nested item 2.1
  - Another nested item 2.2

Mixed ordered and unordered nesting:
1. First numbered item
   - Unnumbered sub-item
   - Another sub-item
     1. Back to numbered
     2. Another numbered
   - More sub-items
2. Second numbered item
   - More unnumbered items

### Complex Lists with Code Blocks

Lists containing code:
- First item
  
  ```python
  def example():
      return "code in list"
  ```

- Second item with inline `code` and block code:
  
  ```javascript
  console.log("nested code block");
  ```

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

## Advanced Features Testing

### Footnotes and Citations

#### Basic Footnotes
This is a sentence with a footnote[^1]. Here's another footnote reference[^2].

Academic papers often use footnotes for additional information[^note1]. Multiple footnotes can appear in the same paragraph[^a][^b][^c].

#### Complex Footnotes
This footnote contains **bold text**[^bold-note]. This one has `inline code`[^code-note].

Here's a footnote with a [link](https://example.com)[^link-note].

#### Citations (Various Formats)
According to Smith (2023)[^smith2023], the findings were significant.

The research shows promising results [@doe2022; @johnson2021].

Multiple citations can be grouped [@citation1; @citation2; @citation3].

As noted in previous work [1], [2], [3], the methodology is well-established.

#### Citation with Page Numbers
The theory was first proposed [@einstein1905, pp. 123-145].

See [@newton1687, chapter 3] for detailed analysis.

According to [@darwin1859, p. 42], natural selection is the key mechanism.

#### Footnote Definitions
[^1]: This is the first footnote definition.

[^2]: This footnote contains multiple paragraphs.

    It can span several lines and include formatting like **bold** and *italic*.
    
    Even `code` and [links](https://example.com) work in footnotes.

[^note1]: Academic footnotes often contain detailed references and additional context that would disrupt the flow of the main text.

[^a]: Short footnote A.

[^b]: Short footnote B with *emphasis*.

[^c]: Short footnote C with `code`.

[^bold-note]: This footnote definition contains **bold formatting** and shows that inline formatting works within footnote definitions.

[^code-note]: This footnote shows `inline code` within the definition.

[^link-note]: Footnotes can contain [external links](https://example.com) and [internal links](#advanced-features-testing).

#### Bibliography-Style References
References:

[1] Smith, J. (2023). "Advanced Markup Processing". *Journal of Documentation*, 15(3), 45-67.

[2] Doe, A., & Johnson, B. (2022). "Parsing Complex Structures". *Technical Review*, 8(2), 123-140.

[3] Wilson, C. (2021). "Modern Text Processing". *Computing Today*, 12(4), 78-92.

### Advanced Citation Formats

#### Multi-Author Citations
The collaborative study [@smith2023; @doe2022; @wilson2021] demonstrates consistent findings.

#### In-Text Citations
Smith et al. (2023) found that parsing complexity increases with nesting depth [@smith2023].

The results (Johnson & Doe, 2022) support this conclusion [@johnson2022].

#### Citation with Prefixes and Suffixes
See especially [@smith2023, pp. 45-67; also @doe2022, chapter 3].

Compare [@jones2020, pp. 12-15] with [@brown2021, figure 2].

#### Locator-Specific Citations
The formula appears in [@mathematics2023, equation 4.7].

Detailed proofs can be found in [@proofs2022, appendix B, theorem 3].

**Known Limitations:**
- Very complex nested structures (list items containing blockquotes with nested lists) may cause infinite loops
- Some edge cases with deep nesting levels need further investigation
- Footnote and citation parsing may have implementation-specific behavior

---

### Final Comprehensive Test

Final paragraph with **all ~~combined~~ features**: *italic*, `code`, [links](http://example.com), images ![test](img.jpg), H~2~O, E=mc^2^, math $\pi r^2$, and emojis :smile: :rocket:!

Final paragraph after horizontal rule demonstrates all inline formatting features working together.

This document successfully tests the core parser capabilities including footnotes and citations without triggering infinite loops.
