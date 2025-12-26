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

## Keyboard Shortcuts

- **`Cmd+R` / `Ctrl+R`**: Run the selected test
- **`Cmd+L` / `Ctrl+L`**: Focus the log/terminal panel
- **`Cmd+K` / `Ctrl+K`**: Clear the terminal
- **`Cmd+Shift+R` / `Ctrl+Shift+R`**: Toggle recent tests menu
- **`Cmd+Option+I` / `F12`**: Open developer tools (when needed)

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
2. **Choose Viewport** (optional): Select Desktop, Tablet, or Mobile preset from toolbar
3. **Run Test**: Click "▶ Run Test" button or press `Cmd+R` / `Ctrl+R`
4. **View Results**:
   - Browser view shows the HTML rendered in iframe
   - Lambda view shows rendered output from layout engine
   - Terminal shows test execution output
5. **Compare**: Visually compare browser vs Lambda rendering
6. **Quick Re-run**: Use Recent menu (`Cmd+Shift+R`) to access recently run tests

## Enhanced Features

See [ENHANCEMENTS.md](ENHANCEMENTS.md) for detailed information about:
- Recent tests history
- Viewport size presets
- Keyboard shortcuts
- Clean workspace (no auto-opening dev tools)

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
