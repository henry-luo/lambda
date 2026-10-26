# DevTools verification

`document.html` covers block boxes, wrapped inline text, scrolling, fractional
transforms, flex and grid. `protocol.test.mjs` launches real headless targets and
tests Mark values, framing, lifecycle, weak handles, CSS edits/undo, hit geometry,
PNG pixels and target isolation. It runs in the Radiant baseline and uses the
existing `test/render` PNG dependency (`npm ci --prefix test/render`).

```bash
make build
node --test test/devtools/protocol.test.mjs
```

The aggregate suite uses `run.mjs` to retain the same Node test verdicts in the
shared runner's `--report <path>` JSON format.

`ui-check.cjs` exercises the actual Electron frontend, native protocol and
retained comparison workspace. Build the frontend first and run:

```bash
npm run build --prefix utils/devtool
env -u ELECTRON_RUN_AS_NODE utils/devtool/node_modules/.bin/electron test/devtools/ui-check.cjs
```

Screenshots go to `temp/devtools/`. `LAMBDA_DEVTOOLS_EXE` selects an isolated
native host for focused checks; normal invocations use the checkout's executable.
