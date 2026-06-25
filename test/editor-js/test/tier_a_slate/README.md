# Tier A — Ported Slate.js fixtures

Source: `ianstormtaylor/slate` `packages/slate/test/` — DOM-free corpus, ~1,018 fixtures across `operations/`, `interfaces/`, `normalization/`, `transforms/`. Slate's paths are 1:1 with our SourcePath model.

## Port order (smallest → largest, foundation → leaves)

| # | Subdir | Slate cases | Cumulative | Notes |
|---|---|---|---|---|
| 1 | `operations/`               | 31  | 31  | Canonical low-level steps — proves the model layer |
| 2 | `interfaces/path/`          | 117 | 148 | Pure path arithmetic — proves source-pos |
| 3 | `interfaces/point/`         | 50  | 198 | Point arithmetic |
| 4 | `interfaces/range/`         | 32  | 230 | Range arithmetic |
| 5 | `interfaces/location/`      | 25  | 255 | Location dispatch |
| 6 | `interfaces/operation/`     | 26  | 281 | Op object shape |
| 7 | `interfaces/text/`          | 23  | 304 | |
| 8 | `interfaces/element/`       | 21  | 325 | |
| 9 | `interfaces/node/`          | 52  | 377 | Tree traversal |
| 10 | `interfaces/editor/`       | 203 | 580 | Editor API surface |
| 11 | `normalization/`           | 20  | 600 | Invariants |
| 12 | `transforms/insertText/`   | 27  | 627 | First transform — proves command layer |
| 13 | `transforms/move/`         | 49  | 676 | |
| 14 | `transforms/setNodes/`     | 29  | 705 | |
| 15 | `transforms/splitNodes/`   | 33  | 738 | |
| 16 | `transforms/mergeNodes/`   | 9   | 747 | |
| 17 | `transforms/insertNodes/`  | 25  | 772 | |
| 18 | `transforms/insertFragment/` | 41 | 813 | |
| 19 | `transforms/wrapNodes/`    | 25  | 838 | |
| 20 | `transforms/unwrapNodes/`  | 26  | 864 | |
| 21 | `transforms/removeNodes/`  | 11  | 875 | |
| 22 | `transforms/liftNodes/`    | 7   | 882 | |
| 23 | `transforms/moveNodes/`    | 13  | 895 | |
| 24 | `transforms/delete/`       | 100 | 995 | Largest transform — leave for last |
| 25 | misc transforms            | ~23 | 1018 | normalization, select, deselect, unsetNodes, general, setPoint |

## Fixture format

Slate's `.tsx`:

```tsx
/** @jsx jsx */
import { Transforms } from 'slate'
import { jsx } from '../../..'
export const run = editor => { Transforms.insertText(editor, 'a') }
export const input  = (<editor><block>w<cursor />ord</block></editor>)
export const output = (<editor><block>wa<cursor />ord</block></editor>)
```

Our HTML port (`block` → `<p>` from md schema; `editor` → `<doc>`):

```
input.html
  <doc><p>w<cursor></cursor>ord</p></doc>

events.json
  [{"type": "transform", "name": "insertText", "args": ["a"]}]

output.html
  <doc><p>wa<cursor></cursor>ord</p></doc>
```

## Divergence policy

We mirror Slate's expected outputs exactly. When the Stage-4 design produces a legitimately different output (e.g., marks are nested elements in our model but mark-sets in Slate), we (a) keep our expected output, (b) write a `NOTES.md` next to the fixture explaining the design reason. A weekly `grep -r "^# Divergence"` across the tier gives the running divergence ledger.
