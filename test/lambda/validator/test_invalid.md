# Invalid Markdown Test

This markdown has various syntax errors:

## Unclosed Code Block
```javascript
function test() {
    console.log("This code block is never closed");

## Invalid List Structure
- Item 1
    - Nested without proper indentation
  - Wrong indentation
- Item 2
    1. Mixed list types
    - More mixing

## Broken Links
[Link with no URL]
[Another broken link](
[Incomplete reference link][missing-ref]

## Invalid Table
| Header 1 | Header 2
| Cell 1 | Cell 2 | Extra cell |
| Missing cell |

## Unclosed HTML
<div>
    <p>Unclosed paragraph
    <span>Multiple unclosed tags
</div>

*Unclosed emphasis text

**Unclosed strong text
