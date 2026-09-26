// lambda.edit Markdown adapter (vibe/radiant/Radiant_Design_Edit_Mode.md §5, §8):
// import onto the editor model, export back through the Markdown formatter,
// and the round-trip gate the loader and Save both apply.
import md: lambda.edit.markdown
import lambda.edit.model
import lambda.editor.mod_doc

let source = "---
title: Adapter sample
tags: [a, b]
---

# Heading with *emphasis*

Text with **bold**, `code`, ~~struck~~, x^2^, a [link](https://example.com \"Example\"),
an ![image](pic.png), inline <span class=\"k\">HTML</span>, and $e^x$ math.
Line with a hard break\\
after it.

1. first
2. second

5. five
6. six

- [x] done
- [ ] open

- loose one

- loose two

> A quote
>
> - with a list

```python
print(\"hi\")
```

| left | right |
|:-----|------:|
| a | b |

<div class=\"note\">kept as is</div>

$$
x = y
$$
"

let loaded = md.import_text(source)
let written = md.export_text(loaded.doc, loaded.envelope)
let again = md.import_text(written)

"envelope keeps the front matter:"; [loaded.envelope.front_matter]
"blocks:"; [for (b in loaded.doc.content) b.tag]
"heading content:"; loaded.doc.content[0].content
"ordered list (one loose list: the blank line continues it):"; loaded.doc.content[2].attrs
"task items:"; [for (li in loaded.doc.content[3].content) attr_get(li, 'checked')]
"code block:"; [attr_get(loaded.doc.content[5], 'language'), doc_text(loaded.doc.content[5])]
"gate on the loaded model passes:"; md.check_roundtrip(loaded.doc, loaded.envelope) == null
"written:"; [written]
"written again is unchanged:"; md.export_text(again.doc, again.envelope) == written

// the gate names the first block a save would change: an underline mark has
// no Markdown spelling, so exporting it would drop the formatting
let underlined = node('doc', [node('p', [text_marked("under", [{name: 'u', value: null}])])])
"gate on unrepresentable formatting:"; [md.check_roundtrip(underlined, loaded.envelope)]

// content the adapter cannot keep refuses to open, with a diagnostic
"footnote refused:"; [md.import_text("Text[^1].\n\n[^1]: note\n") ^ { ^.message }]
"declined commands:"; md.unsupported_input_types
