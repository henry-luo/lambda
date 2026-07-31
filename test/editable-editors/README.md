# Radiant upstream-editor compatibility fixtures

This package is intentionally separate from `test/editor-js`, which is the
Lambda custom editor. It pins upstream CodeMirror, ProseMirror, and Editor.js
dependencies exactly and generates offline browser bundles in `build/`.

Run `npm install && npm run build` when refreshing the local generated browser
bundles. Baseline targets never install packages. Each fixture publishes its
editor-owned state in `#state`; UI assertions use that value rather than only
the projected DOM.

ProseMirror runs in light DOM. The Editor.js fixture enables the core
`execCommand("insertHTML")` path because upstream uses it for inline paste;
other legacy formatting commands remain outside the minimal Tool set.

## Upstream-derived coverage

`ref/` contains the pinned upstream source checkouts used while refreshing this
suite. The portable browser-facing behaviors are represented as Radiant event
fixtures, rather than copying each project's private runner and unit helpers:

- CodeMirror `test/webtest-domchange.ts` and `test/webtest-composition.ts`:
  selected-range replacement and composition in a newly empty line.
- ProseMirror `test/webtest-domchange.ts` and `test/webtest-composition.ts`:
  the equivalent empty-paragraph composition and cancellation behavior.
- Editor.js `test/cypress/tests/onchange.cy.ts`: public `onChange` mutation
  events for paragraph edits and block insertion.

The corresponding `test/ui/editable-editors-*-selection-composition.json` and
`test/ui/editable-editors-editorjs-onchange.json` fixtures assert editor-owned
serialized state, not just DOM projection. They are part of `make test-editable`.

Additional ports cover CodeMirror event handlers and RTL direction, ProseMirror
external HTML paste and marked composition, and Editor.js data-empty state,
block-boundary navigation, delete merging, clipboard blocks, read-only mode,
and indexed `onChange` events. The complete file-by-file portability decision
is recorded in [UPSTREAM_TEST_AUDIT.md](UPSTREAM_TEST_AUDIT.md).
