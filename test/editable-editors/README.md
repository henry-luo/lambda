# Radiant upstream-editor compatibility fixtures

This package is intentionally separate from `test/editor-js`, which is the
Lambda custom editor. It pins upstream CodeMirror, ProseMirror, and Editor.js
dependencies exactly and generates offline browser bundles in `build/`.

Run `npm ci && npm run build` only when refreshing the checked-in package
artifacts. Baseline targets never install packages. Each fixture publishes its
editor-owned state in `#state`; UI assertions use that value rather than only
the projected DOM.

ProseMirror runs in light DOM. Editor.js configurations omit Tools that expose
or require legacy command APIs.
