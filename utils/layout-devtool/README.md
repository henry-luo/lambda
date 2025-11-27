# Layout DevTool

Electron-based development tool for testing and debugging Lambda HTML/CSS layout engine.

## Features

- **Test Tree**: Browse and select layout tests organized by category
- **Side-by-Side Comparison**: View browser rendering vs Lambda output
- **Integrated Terminal**: Run tests and see output in real-time
- **Test Results**: Detailed test execution results and analysis

## Installation

From the `utils/layout-devtool` directory:

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
2. **Run Test**: Click the "Run Test" button in the toolbar
3. **View Results**:
   - Browser view shows the HTML rendered in iframe
   - Lambda view will show rendered output (when render command is available)
   - Terminal shows test execution output
4. **Compare**: Visually compare browser vs Lambda rendering

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

## Keyboard Shortcuts

(Coming in Phase 5)

- `Cmd/Ctrl+R` - Run selected test
- `Cmd/Ctrl+.` - Stop running test
- `Cmd/Ctrl+Up/Down` - Navigate tests

## Known Limitations

- Lambda render command not yet implemented (shows placeholder)
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
