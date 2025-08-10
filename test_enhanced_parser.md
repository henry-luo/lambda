---
title: Test Document
author: Test User
date: 2024-01-20
category: test
tags: [test, parser, emoji, yaml]
draft: false
featured: true
---

# Test Document with Enhanced Features

This document tests the enhanced new markup parser with features from the old parser.

## Emoji Testing

Let's test various emoji patterns:

- Simple emoji: 😀 😃 😄
- Text emoji: :smile: :heart: :thumbsup: :fire:
- Complex emoji: 🎉 🚀 ⭐ 💯
- Sequences: 👍🏻 👨‍💻 🏳️‍🌈

## YAML Frontmatter Testing

The frontmatter above should be parsed correctly with key-value pairs.

## Math Testing

Inline math: $x^2 + y^2 = z^2$

Display math:
$$
\int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}
$$

## Footnotes and Citations

This is a statement with a footnote[^1] and a citation[@smith2020].

[^1]: This is the footnote content.

## Complex Table

| Feature | Old Parser | New Parser |
|---------|------------|------------|
| Emoji   | ✅ Full    | ✅ Enhanced |
| YAML    | ✅ Robust  | ✅ Enhanced |
| Math    | ❌ Limited | ✅ Full     |
| Citations| ❌ No     | ✅ Yes      |

## Advanced Features

> This is a blockquote with advanced features.
> 
> - List item with emoji :rocket:
> - Math in lists: $f(x) = x^2$
> - Citations in quotes[@doe2021]

```python
# Code block with language
def test_parser():
    return "Enhanced parser works!"
```

Testing inline `code` and **bold** and *italic* text.
