# Tier B — Ported ProseMirror cases

Source: `ProseMirror/prosemirror-model`, `-transform`, `-state`, `-history`, `-commands` — `it()` test cases across DOM-free files. ~618 cases total, distributed:

| Package | DOM-free cases | Includes the famous |
|---|---|---|
| prosemirror-model     | 204 (305 minus 101 in test-dom.ts which needs jsdom) | test-content, test-mark, test-node, test-replace, test-resolve, test-slice, test-diff |
| prosemirror-transform | 228 | **test-trans.ts** (169 — the workhorse), test-step (19), test-structure (38), test-replace_step (2) |
| prosemirror-state     | 35  | test-state, test-selection |
| prosemirror-history   | 28  | test-history |
| prosemirror-commands  | 133 | **test-commands.ts** — splitBlock, joinBackward, lift, wrapIn, toggleMark, …  |

## The integer↔path adapter

PM stores positions as integers into a flat token stream. We store positions as paths. Porting test-trans.ts and test-commands.ts requires translating PM's `doc.tag.a` integer back into our `SourcePos = {path, offset}`.

The adapter lives in `test/helpers/int-path-adapter.ts`. It walks the doc in PM's flat-token order, summing tokens until it reaches the integer position, then converts the surrounding context to a path + offset. Round-trip-tested in its own unit file.

## Tagged-doc builder

PM tests embed two named positions `<a>` and `<b>` in their tagged docs and pass them to transform functions as `from` / `to` integer arguments:

```js
add(doc(p("hello <a>there<b>!")), schema.mark("strong"), ...)
// internally: new Transform(doc).addMark(doc.tag.a, doc.tag.b, mark)
```

**We do NOT adopt `<a>` / `<b>` as marker elements** — `<a>` collides with the standard HTML anchor element and would make fixtures ambiguous.

Instead, since `<a>` and `<b>` in PM are functionally a `from..to` range — exactly what Slate's `<anchor>..<focus>` is — we **convert at port time**:

| PM source | Our fixture |
|---|---|
| `<p>hello <a>there<b>!</p>` | `<p>hello <anchor></anchor>there<focus></focus>!</p>` |

Events that referenced `doc.tag.a` / `doc.tag.b` as integer args become commands that read `from` / `to` from the current selection — same effect.

**For the rare PM case needing more than two positions** (a handful of replace-range tests), encode the extra positions as explicit `{path, offset}` literals in `events.json` rather than introducing more marker elements:

```json
{ "type": "command", "name": "replaceRange",
  "from": "@anchor", "to": "@focus",
  "gapFrom": {"path": [0, 0], "offset": 3},
  "gapTo":   {"path": [0, 0], "offset": 7} }
```

This keeps the marker vocabulary tiny (3 elements total) and HTML-friendly.

## Port order

| # | Source | Cases | Why this order |
|---|---|---|---|
| 1 | prosemirror-transform/test-step.ts        | 19  | Tiny + foundational |
| 2 | prosemirror-transform/test-structure.ts   | 38  | canSplit, liftTarget, findWrapping — proves schema integration |
| 3 | prosemirror-model/test-content.ts         | 62  | ContentMatch — schema test |
| 4 | prosemirror-model/test-replace.ts         | 22  | Slice algebra |
| 5 | prosemirror-model/test-resolve.ts         | 2   | (Two huge loops — high coverage despite low count) |
| 6 | prosemirror-model/test-mark.ts            | 40  | |
| 7 | prosemirror-model/test-node.ts            | 40  | |
| 8 | prosemirror-model/test-diff.ts            | 18  | |
| 9 | prosemirror-model/test-slice.ts           | 20  | |
| 10 | prosemirror-state/test-selection.ts      | 21  | |
| 11 | prosemirror-state/test-state.ts          | 14  | |
| 12 | prosemirror-history/test-history.ts      | 28  | After history module exists |
| 13 | prosemirror-transform/test-trans.ts      | 169 | The big one — after structure + content |
| 14 | prosemirror-commands/test-commands.ts    | 133 | After everything else |

## Skipped

- prosemirror-model/test-dom.ts (101) — DOM parser/serializer, not applicable
- prosemirror-transform/test-mapping.ts (10) — integer-position-mapping primitive; our path-based model has no analogous mapping object
- All of prosemirror-view (~250) — needs real browser
