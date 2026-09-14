# `@lambda/editor-js` — JS reference implementation of the Lambda/Radiant rich editor

This sub-project is the **reference implementation** of the Stage-4 rich-document-with-inline-drawings editor described in [`vibe/Radiant_Editor_Stage4_Drawings.md`](../../vibe/Radiant_Editor_Stage4_Drawings.md). It is a parallel deliverable to `lambda/package/editor/`: same architecture, same module boundaries, same fixture-driven test corpus — written in TypeScript on top of React 19 + Vite + Vitest.

## Why it exists

1. **Verify the Stage-4 design end-to-end** before committing the Lambda implementation. A working JS impl forces every gap to surface.
2. **Develop and validate the test suite** against a known-good editor — ports from Slate (~1,018 fixtures) and ProseMirror (~618 cases) get tuned where editors legitimately diverge.
3. **Serve as the oracle for the Lambda port** — fixtures are language-agnostic (HTML + JSON), so the Lambda runner can replay them and compare results to the JS reference.

## Layout

```
test/editor-js/
├── src/
│   ├── model/          # data layer — pure TS, port-ready for Lambda
│   │   ├── doc.ts              # nodes, text leaves, marks
│   │   ├── source-pos.ts       # SourcePath, SourcePos, SourceSelection (path-based)
│   │   ├── step.ts             # 7 step kinds with apply/invert/map
│   │   ├── transaction.ts      # transaction builder
│   │   ├── history.ts          # undo/redo with selection restoration
│   │   ├── mark-editor.ts      # immutable-mode Mark editor
│   │   └── schema.ts           # schema validator
│   ├── schemas/        # markdown + drawing + combined doc schemas
│   ├── commands/       # text + drawing commands (PM-style: (state, dispatch?) → bool)
│   ├── drawing/        # geom, router, snap, tools, hit-test, selection-handles
│   ├── input/          # input-intent mapper + DOM bridge
│   ├── clipboard/      # text/html/svg paste + drawing clipboard
│   ├── decorations.ts  # non-document overlays
│   ├── editor.ts       # public Editor session API
│   └── view/           # React 19 rendering layer
│       ├── EditorView.tsx
│       ├── flow/       # text-block templates (Doc, Paragraph, Heading, List, …)
│       ├── drawing/    # canvas templates (Drawing, Layer, Shape, Connector, TextFrame, …)
│       └── hooks/
├── test/
│   ├── helpers/        # fixture-runner, tagged-doc parser, int↔path adapter, event-driver
│   ├── tier_a_slate/         # ported Slate fixtures (~1,018 cases)
│   ├── tier_b_prosemirror/   # ported PM cases (~618)
│   ├── tier_c_wpt/           # selective WPT platform tests
│   └── tier_0_drawing/       # native Stage-4 drawing cases (~150)
└── demo/               # dev sandbox — load a sample doc, type, draw, sanity-check
```

## Fixture format (the canonical truth)

Fixtures are **HTML files with custom elements**, plus a JSON event script. The same three files run against the JS reference now and the Lambda implementation later — no translation.

```
test/tier_a_slate/transforms/insertText/selection/block-middle/
├── input.html       <doc><p>w<cursor></cursor>ord</p></doc>
├── events.json      [{"type": "transform", "name": "insertText", "args": ["a"]}]
├── output.html      <doc><p>wa<cursor></cursor>ord</p></doc>
└── NOTES.md         (present only when our expected output diverges from Slate's)
```

| Element family | Purpose |
|---|---|
| Document blocks (`<doc>`, `<p>`, `<heading>`, `<list>`, `<blockquote>`, `<code-block>`, `<image>`, `<table>`, …) | Source-doc structure |
| Marks (`<strong>`, `<em>`, `<u>`, `<code>`, `<link>`) | Inline formatting |
| Selection markers (`<cursor></cursor>`, `<anchor></anchor>`, `<focus></focus>`) | Slate-style — carry selection state; stripped at load time. These are the **only** position markers used in fixtures. |
| Drawing block (`<drawing id=… width=… height=…>`) | Stage-4 inline drawing surface |
| Drawing children (`<layer>`, `<shape kind=…>`, `<connector>`, `<group>`, `<text-frame>`, `<label>`) | Per Stage-4 §3 |

**Parsing rule:** all custom elements use explicit open + close tags (`<cursor></cursor>`, not `<cursor/>`), to match HTML5 parsing semantics deterministically across `parse5`, browsers, and the Lambda HTML parser later.

## Running

```bash
cd test/editor-js
npm install
npm test              # vitest run (all tiers)
npm run test:watch    # vitest watch mode
npm run dev           # vite dev server with demo
npm run typecheck     # tsc --noEmit
```

## Relationship to the Lambda port

The Lambda implementation (`lambda/package/editor/`) is **deferred until this JS reference is green against the full corpus**. The two implementations will share fixtures via `test/lambda/editing/` re-using `test/editor-js/test/**/*.html` + `*.json`. Any divergence between JS and Lambda outputs is a Lambda bug; any divergence between JS and Slate/PM is documented in the relevant `NOTES.md`.

## Status

| Task | Status |
|---|---|
| #4 Bootstrap (project skeleton) | in progress |
| #5 Model layer (doc, source-pos, step, transaction, history) | blocked on #4 |
| #6 Schema + commands + input intent | blocked on #5 |
| #7 React view layer (text editing) | blocked on #5 |
| #8 Drawing layer (geom, tools, router, snap, drawing commands) | blocked on #6, #7 |
| #3 Test suite development | blocked on #8 |
| #9 Tune expected outputs vs Slate/PM | alongside #3 |
| #2 Port to Lambda | blocked on #3 |

Track via `TaskList`.
