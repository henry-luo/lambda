// parse() option `sourcepos`: each top-level block of a markup document
// carries the source lines it spans, as cmark writes them:
// "startline:startcol-endline:endcol", 1-based, trailing blank lines excluded.

fn blocks(doc) {
  let body = [for (c in content(doc) where type(c) == element and name(c) == 'body') c][0];
  [for (c in content(body) where type(c) == element) [name(c), c.sourcepos]]
}

let source = "# Title

Two lines
of text.

  - indented list
  - second

```
code
```

[ref]: https://example.com

> quote


Last.
"

// link reference definitions claim no block, so line 13 is not reported
"markdown blocks:";
blocks(parse(source, {type: 'markdown', sourcepos: true}) ^ { null })

// off by default: the tree is unchanged
"default has no positions:";
[for (b in blocks(parse(source, 'markdown') ^ { null })) b[1]]

// nested blocks carry no positions; only the top level does
"nested blocks:";
let nested = parse("- a\n- b\n", {type: 'markdown', sourcepos: true}) ^ { null }
let top = [for (c in content([for (c in content(nested) where type(c) == element and name(c) == 'body') c][0]) where type(c) == element) c][0];
[top.sourcepos, [for (li in content(top) where type(li) == element) li.sourcepos]]

"option must be a bool:";
[parse("x", {type: 'markdown', sourcepos: 1}) ^ { ^.message }]
