# Layout DevTool

Electron-based development tool for testing and debugging Lambda HTML/CSS layout engine.

## Features

- **Test Tree**: Browse and select layout tests organized by category
- **Recent Tests**: Quick access to recently run tests (10 most recent)
- **Viewport Presets**: Test at Desktop (1200×800), Tablet (768×1024), or Mobile (375×667) sizes
- **Side-by-Side Comparison**: View browser rendering vs Lambda output
- **Integrated Terminal**: Run tests and see output in real-time
- **Keyboard Shortcuts**: Efficient workflow with common shortcuts
- **Test Results**: Detailed test execution results and analysis
- **Clean Interface**: No auto-opening dev tools (available on demand)
- **Live Inspector**: Launch a Radiant document, browse its DOM, inspect computed
  styles/geometry, pick from rendered pixels, edit inline CSS and undo edits.

## Live document inspection

From the repository root, run `make devtool`, select **Live**, enter a document
path (or use `test/devtools/document.html`), and press **Launch**. The preview
shows pixels from a separate `lambda.exe` process. Expand Elements, select a
node, change a CSS property/value and press **Apply**; **Undo** restores the
previous inline value and priority. An empty value removes the declaration.
**Pick element** selects through Radiant's hit testing. **Native window** also
opens the target's own window; selection/highlights currently live in the
DevTool preview. The comparison workspace remains under **Tests**.

The Source tab shows an immutable local-file snapshot captured at attach. It
does not yet provide source-generation tracking for imported scripts or a
source-level debugger. Connection/process output is available at the bottom;
structured runtime console events are a later milestone.

**Lambda scratch** opens a separate persistent functional Lambda REPL. Press
**Start scratch**, submit declarations/expressions with **Run Lambda** or
**Cmd/Ctrl+Enter**, and expand returned values. **Bindings** reads the current
scope without rerunning previous entries. **Collect** forces GC while inspector
handles remain rooted. **Clear results** releases all inspector handles and
clears the transcript; existing REPL bindings remain. **Stop scratch** ends only
the scratch target. Alt+Up/Down recalls entry history. This uses the existing
session semantics (S16.7.4–S16.7.6 and D8.1.1v18); it does not inherit a document's
locals. JavaScript evaluation is deferred.

Large results expand in bounded pages. Functions and unsupported live values
show an opaque preview; they are never invoked by expansion. Typed arrays show
shape metadata and expand in flattened logical order. Binary and long text use
byte offsets; text pages end on UTF-8 boundaries. The Radiant tree also pages
children and reveals the ancestor path of a selected element.

A headless client uses the identical Mark protocol:

```bash
node utils/devtool/inspect.mjs test/devtools/document.html
node utils/devtool/inspect.mjs test/devtools/document.html request.mark
node utils/devtool/inspect.mjs --scratch request.mark
node --test test/devtools/protocol.test.mjs
```

The optional request file contains a map such as `{op: 'tree'}`. The client
performs `hello`, assigns an ID and prints the reply in Mark. Native startup is
`lambda devtools <document> --pipe <inherited-fd> [--headless]`; the launcher
provides a dedicated duplex descriptor, separate from stdio. The backend reports
its supported operations through `hello`; stepping/evaluation are not advertised.

UI verification (after `npm run build` in this directory):

```bash
env -u ELECTRON_RUN_AS_NODE utils/devtool/node_modules/.bin/electron test/devtools/ui-check.cjs
```

Run that command from the repository root. It saves before/after screenshots
under `temp/devtools/`. Design and implementation status are recorded in
[`Lambda_Design_Devtools.md`](../../vibe/Lambda_Design_Devtools.md) and
[`Lambda_Impl_Devtools.md`](../../vibe/impl/Lambda_Impl_Devtools.md).

## Keyboard Shortcuts

The test shortcuts apply in the Tests workspace.

- **`Cmd+R` / `Ctrl+R`**: Run the selected test
- **`Cmd+L` / `Ctrl+L`**: Focus the log/terminal panel
- **`Cmd+K` / `Ctrl+K`**: Clear the terminal
- **`Cmd+Shift+R` / `Ctrl+Shift+R`**: Toggle recent tests menu
- **`Cmd+Option+I` / `F12`**: Open developer tools (when needed)

## Installation

From the `utils/devtool` directory:

```bash
npm install
```

## Development

Run in development mode with hot reload:

```bash
npm run electron:dev
```

This will start both the Vite dev server and Electron in development mode.

## Building

Build for production:

```bash
npm run build
npm run package
```

## Usage

1. **Select a Test**: Click on any test in the left sidebar tree
2. **Choose Viewport** (optional): Select Desktop, Tablet, or Mobile preset from toolbar
3. **Run Test**: Click "▶ Run Test" button or press `Cmd+R` / `Ctrl+R`
4. **View Results**:
   - Browser view shows the HTML rendered in iframe
   - Lambda view shows rendered output from layout engine
   - Terminal shows test execution output
5. **Compare**: Visually compare browser vs Lambda rendering
6. **Quick Re-run**: Use Recent menu (`Cmd+Shift+R`) to access recently run tests

## Project Structure

```
layout-devtool/
├── main.js              # Electron main process
├── preload.js           # IPC bridge
├── index.html           # Entry HTML
├── vite.config.js       # Vite configuration
├── src/
│   ├── main.jsx         # React entry point
│   ├── App.jsx          # Root component
│   ├── components/      # React components
│   │   ├── TestTree.jsx
│   │   ├── ComparisonPanel.jsx
│   │   ├── TerminalPanel.jsx
│   │   └── ResultsViewer.jsx
│   └── styles/          # CSS files
│       ├── main.css
│       ├── tree.css
│       ├── comparison.css
│       └── terminal.css
└── package.json
```

## Requirements

- Node.js 18+
- Electron 28+
- Lambda project must be built (`./lambda.exe` must exist)
- Test data in `../../test/layout/data/`
- Reference data in `../../test/layout/reference/`

## Known Limitations

- File watching not yet implemented
- Batch testing not yet available

## Development Roadmap

- [x] Phase 1: Foundation (test tree, terminal)
- [x] Phase 2: Test execution
- [x] Phase 3: Comparison view
- [ ] Phase 4: Results analysis
- [ ] Phase 5: Polish & features

## Contributing

This tool is part of the Lambda project. See main project documentation for contribution guidelines.

## License

MIT
