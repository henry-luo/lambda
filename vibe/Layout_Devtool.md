# Layout DevTool - Electron App Development Plan

## Overview

An Electron-based development tool for testing and debugging Lambda HTML/CSS layout engine. The app provides a visual interface for running layout tests, comparing results against browser references, and analyzing layout differences.

## Architecture Analysis

### Existing Infrastructure

#### Test Organization (`./test/layout/`)
- **Test Data**: `data/` directory with categorized HTML test files
  - Categories: `baseline/`, `basic/`, `box/`, `flex/`, `grid/`, `position/`, `table/`, `text_flow/`, `page/`, `medium/`
  - 270+ HTML test files (`.html` and `.htm`)
  - Excluded: `css2.1/` (not tested)

- **Reference Data**: `reference/` directory with browser-rendered layout data
  - Mirrors test data structure (same categories)
  - JSON files containing layout tree, element bounds, computed styles
  - Generated via Puppeteer (`tools/extract_browser_references.js`)

- **Test Reports**: `reports/` directory with extraction summaries

#### Testing Infrastructure

**Node.js Test Scripts**:
1. `test_radiant_layout.js` - Main test runner
   - Supports both Radiant (Lexbor) and Lambda CSS engines
   - Compares layout output against browser references
   - Hierarchical comparison: elements + text nodes + computed styles
   - Configurable tolerances (5px default, 100% element/text match)
   - Outputs: pass/fail status, detailed diffs, match percentages

2. `compare_css.js` - CSS property comparison
   - Compares resolved CSS properties between Lambda and browser
   - Property normalization (colors, fonts, units)
   - Default value handling

3. `tools/extract_browser_references.js` - Reference generation
   - Uses Puppeteer to render HTML in headless Chrome
   - Extracts layout bounds, computed styles, element hierarchy
   - Consistent viewport (1200x800)

**Makefile Targets**:
- `make layout test=<name>` - Run Lambda CSS test
- `make test-layout test=<name>` - Run Radiant test
- `make layout suite=<category>` - Run category tests
- `make layout pattern=<pattern>` - Pattern matching
- `make capture-layout` - Generate browser references

**Lambda CLI Commands**:
```bash
./lambda.exe layout <file.html> [--width 1200] [--height 800]
```
- Outputs: `/tmp/view_tree.json` (layout tree with bounds)
- Includes: element hierarchy, layout boxes, text nodes, computed styles

#### Data Formats

**Browser Reference JSON** (`reference/<category>/<test>.json`):
```json
{
  "browser_info": { "name": "Chrome", "version": "..." },
  "layout_tree": {
    "tag": "html",
    "selector": "html",
    "layout": { "x": 0, "y": 0, "width": 1200, "height": 800 },
    "computed": { "display": "block", "fontSize": "16px", ... },
    "children": [...]
  }
}
```

**Lambda Output JSON** (`/tmp/view_tree.json`):
```json
{
  "layout_tree": {
    "tag": "html",
    "type": "element",
    "layout": { "x": 0, "y": 0, "width": 1200, "height": 800 },
    "computed": { ... },
    "css_properties": { ... },
    "children": [
      { "type": "element", "tag": "body", ... },
      { "type": "text", "content": "...", "layout": {...} }
    ]
  }
}
```

**Test Results Format**:
```json
{
  "testName": "baseline_801_display_block",
  "elementComparison": {
    "total": 15,
    "matched": 14,
    "failed": 1,
    "passRate": 93.3
  },
  "textComparison": {
    "total": 8,
    "matched": 7,
    "passRate": 87.5
  },
  "differences": [
    {
      "type": "layout_difference",
      "path": "root/0/1",
      "tag": "div",
      "maxDifference": 12.5,
      "differences": [...]
    }
  ]
}
```

## Application Design

### Technology Stack

**Core**:
- Electron (v28+)
- Node.js (v18+)
- HTML/CSS/JavaScript (ES6+)

**UI Framework**:
- React 18 (component-based UI with hooks)

**UI Libraries**:
- Split.js - resizable panels
- xterm.js - terminal emulator

**Utilities**:
- child_process (Node.js built-in) - spawn Lambda CLI
- fs/fs.promises (Node.js built-in) - file operations
- path (Node.js built-in) - path handling

### UI Layout

```
┌─────────────────────────────────────────────────────────────────┐
│ Menu Bar: File | Test | View | Tools | Help                     │
├─────────────┬───────────────────────────────────────────────────┤
│             │ ┌─────────────────────┬─────────────────────────┐ │
│   Test      │ │   Browser View      │   Lambda View          │ │
│   Tree      │ │                     │                        │ │
│   (Left)    │ │   (iframe or img)   │   (rendered PNG)       │ │
│             │ │                     │                        │ │
│   ├─baseline│ │                     │                        │ │
│   │ ├─801   │ │                     │                        │ │
│   │ ├─802   │ │                     │                        │ │
│   ├─basic   │ │                     │                        │ │
│   ├─flex    │ │                     │                        │ │
│   └─...     │ └─────────────────────┴─────────────────────────┘ │
│             │                                                    │
│             ├────────────────────────────────────────────────────┤
│             │ Terminal Output                                   │
│             │ > Running: make layout test=baseline_801...       │
│             │ ✅ PASS: 14/15 elements (93.3%)                   │
│             │                                                    │
└─────────────┴────────────────────────────────────────────────────┘
```

### Component Architecture

#### 1. Main Window (`main.js`)
```javascript
const { app, BrowserWindow, ipcMain } = require('electron');

class LayoutDevTool {
  constructor() {
    this.mainWindow = null;
    this.testProcess = null;
  }

  createWindow() {
    this.mainWindow = new BrowserWindow({
      width: 1600,
      height: 1000,
      webPreferences: {
        nodeIntegration: false,
        contextIsolation: true,
        preload: path.join(__dirname, 'preload.js')
      }
    });

    this.mainWindow.loadFile('src/index.html');
  }

  setupIPC() {
    // Handle test execution requests
    ipcMain.handle('run-test', this.runTest.bind(this));
    ipcMain.handle('load-test-tree', this.loadTestTree.bind(this));
    ipcMain.handle('load-reference', this.loadReference.bind(this));
    // ... more handlers
  }

  async runTest(event, testPath, options) {
    // Spawn Lambda CLI or make command
    // Stream output to renderer
    // Return results
  }
}
```

#### 2. Test Tree Panel (`src/components/TestTree.jsx`)
```javascript
import React, { useState, useEffect } from 'react';

function TestTree({ onTestSelect }) {
  const [categories, setCategories] = useState([]);
  const [expandedCategories, setExpandedCategories] = useState(new Set());
  const [selectedTest, setSelectedTest] = useState(null);
  const [testStatus, setTestStatus] = useState(new Map());

  useEffect(() => {
    loadTests();
  }, []);

  async function loadTests() {
    // Request test tree from main process
    const data = await window.electronAPI.loadTestTree();
    setCategories(data);
  }

  function toggleCategory(category) {
    setExpandedCategories(prev => {
      const next = new Set(prev);
      if (next.has(category)) next.delete(category);
      else next.add(category);
      return next;
    });
  }

  function handleTestSelect(category, testFile) {
    const test = { category, testFile };
    setSelectedTest(test);
    onTestSelect?.(test);
  }

  function getStatusIcon(category, testFile) {
    const key = `${category}/${testFile}`;
    const status = testStatus.get(key);
    return status === 'pass' ? '✅' : status === 'fail' ? '❌' :
           status === 'running' ? '⏳' : '⚪';
  }

  return (
    <div className="test-tree">
      {categories.map(cat => (
        <div key={cat.name} className="category">
          <div className="category-header" onClick={() => toggleCategory(cat.name)}>
            <span>{expandedCategories.has(cat.name) ? '▼' : '▶'}</span>
            <span>{cat.name}</span>
            <span>({cat.tests.length})</span>
          </div>
          {expandedCategories.has(cat.name) && (
            <div className="test-list">
              {cat.tests.map(test => (
                <div key={test} className="test-item"
                     onClick={() => handleTestSelect(cat.name, test)}>
                  <span>{getStatusIcon(cat.name, test)}</span>
                  <span>{test}</span>
                </div>
              ))}
            </div>
          )}
        </div>
      ))}
    </div>
  );
}

export default TestTree;
```

#### 3. Comparison Panel (`src/components/ComparisonPanel.jsx`)
```javascript
import React, { useState, useEffect, useRef } from 'react';

function ComparisonPanel({ test }) {
  const [browserView, setBrowserView] = useState(null);
  const [lambdaView, setLambdaView] = useState(null);
  const [splitPos, setSplitPos] = useState(50);
  const isDragging = useRef(false);

  useEffect(() => {
    if (test) loadViews(test);
  }, [test]);

  async function loadViews(test) {
    const testPath = `./test/layout/data/${test.category}/${test.testFile}`;
    setBrowserView(testPath);

    // Request Lambda render from main process
    const outputPath = await window.electronAPI.renderLambdaView(testPath);
    setLambdaView(outputPath);
  }

  function handleMouseDown(e) {
    isDragging.current = true;
    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);
  }

  function handleMouseMove(e) {
    if (!isDragging.current) return;
    const container = e.currentTarget.parentElement;
    const rect = container.getBoundingClientRect();
    const pos = ((e.clientX - rect.left) / rect.width) * 100;
    setSplitPos(Math.max(20, Math.min(80, pos)));
  }

  function handleMouseUp() {
    isDragging.current = false;
    document.removeEventListener('mousemove', handleMouseMove);
    document.removeEventListener('mouseup', handleMouseUp);
  }

  return (
    <div className="comparison-panel">
      <div className="panel left-panel" style={{ width: `${splitPos}%` }}>
        <div className="panel-header">Browser View</div>
        {browserView && <iframe src={browserView} title="Browser" />}
      </div>
      <div className="gutter" onMouseDown={handleMouseDown} />
      <div className="panel right-panel" style={{ width: `${100 - splitPos}%` }}>
        <div className="panel-header">Lambda View</div>
        {lambdaView && <img src={lambdaView} alt="Lambda" />}
      </div>
    </div>
  );
}

export default ComparisonPanel;
```

#### 4. Terminal Panel (`src/components/TerminalPanel.jsx`)
```javascript
import React, { useEffect, useRef, useImperativeHandle, forwardRef } from 'react';
import { Terminal } from 'xterm';
import { FitAddon } from 'xterm-addon-fit';
import 'xterm/css/xterm.css';

const TerminalPanel = forwardRef((props, ref) => {
  const terminalRef = useRef(null);
  const xtermRef = useRef(null);
  const fitAddonRef = useRef(null);

  useEffect(() => {
    if (!terminalRef.current || xtermRef.current) return;

    const terminal = new Terminal({
      rows: 15,
      cols: 120,
      theme: { background: '#1e1e1e', foreground: '#d4d4d4' },
      fontSize: 12,
      fontFamily: 'Monaco, Consolas, monospace'
    });

    const fitAddon = new FitAddon();
    terminal.loadAddon(fitAddon);
    terminal.open(terminalRef.current);
    fitAddon.fit();

    terminal.writeln('Layout DevTool Terminal');
    terminal.writeln('Ready.');
    terminal.writeln('');

    xtermRef.current = terminal;
    fitAddonRef.current = fitAddon;

    const handleResize = () => fitAddon.fit();
    window.addEventListener('resize', handleResize);

    return () => {
      window.removeEventListener('resize', handleResize);
      terminal.dispose();
    };
  }, []);

  useImperativeHandle(ref, () => ({
    write: (text) => xtermRef.current?.write(text),
    writeln: (text) => xtermRef.current?.writeln(text),
    clear: () => xtermRef.current?.clear()
  }));

  return (
    <div className="terminal-panel">
      <div className="terminal-header">
        <span>Terminal</span>
        <button onClick={() => xtermRef.current?.clear()}>Clear</button>
      </div>
      <div ref={terminalRef} className="terminal-content" />
    </div>
  );
});

export default TerminalPanel;
```

#### 5. Test Runner (`src/services/TestRunner.js`)
```javascript
const { spawn } = require('child_process');

class TestRunner {
  constructor() {
    this.lambdaExe = './lambda.exe';
    this.makeCommand = 'make';
    this.currentProcess = null;
  }

  async runSingleTest(category, testFile, options = {}) {
    const testPath = path.join('./test/layout/data', category, testFile);
    const testName = path.basename(testFile, path.extname(testFile));

    // Option 1: Direct Lambda CLI
    if (options.engine === 'lambda-direct') {
      return this.runLambdaDirect(testPath);
    }

    // Option 2: Via Makefile
    if (options.engine === 'make') {
      return this.runViaMake(testName, category);
    }

    // Option 3: Via Node.js test script
    return this.runViaNodeScript(testName, category);
  }

  async runLambdaDirect(htmlPath) {
    // ./lambda.exe layout <file> --width 1200 --height 800
    const args = ['layout', htmlPath, '--width', '1200', '--height', '800'];

    return new Promise((resolve, reject) => {
      this.currentProcess = spawn(this.lambdaExe, args);

      let stdout = '';
      let stderr = '';

      this.currentProcess.stdout.on('data', (data) => {
        stdout += data.toString();
        this.emit('output', data.toString());
      });

      this.currentProcess.stderr.on('data', (data) => {
        stderr += data.toString();
        this.emit('error', data.toString());
      });

      this.currentProcess.on('close', async (code) => {
        if (code === 0) {
          // Load and parse /tmp/view_tree.json
          const result = await this.parseLayoutOutput();
          resolve(result);
        } else {
          reject(new Error(`Lambda failed: ${stderr}`));
        }
      });
    });
  }

  async runViaMake(testName, category) {
    // make layout test=<testName>
    const command = `make layout test=${testName}`;

    return new Promise((resolve, reject) => {
      this.currentProcess = spawn('make', ['layout', `test=${testName}`], {
        cwd: process.cwd()
      });

      // Similar process handling...
    });
  }

  async runViaNodeScript(testName, category) {
    // node test/layout/test_radiant_layout.js -e lambda-css -t <test> -v
    const args = [
      'test/layout/test_radiant_layout.js',
      '--engine', 'lambda-css',
      '--radiant-exe', './lambda.exe',
      '--test', `${testName}.html`,
      '-v'
    ];

    return new Promise((resolve, reject) => {
      this.currentProcess = spawn('node', args);
      // ... handle output
    });
  }

  async parseLayoutOutput() {
    // Read /tmp/view_tree.json
    const outputPath = '/tmp/view_tree.json';
    const content = await fs.readFile(outputPath, 'utf8');
    return JSON.parse(content);
  }

  async loadBrowserReference(testName, category) {
    // Read test/layout/reference/<category>/<testName>.json
    const refPath = path.join(
      './test/layout/reference',
      category,
      `${testName}.json`
    );
    const content = await fs.readFile(refPath, 'utf8');
    return JSON.parse(content);
  }

  async compareResults(lambdaOutput, browserReference) {
    // Implement comparison logic
    // Or call existing compare_css.js

    // Calculate differences
    const comparison = {
      elementMatch: this.compareElements(lambdaOutput, browserReference),
      textMatch: this.compareTextNodes(lambdaOutput, browserReference),
      cssMatch: this.compareCSS(lambdaOutput, browserReference)
    };

    return comparison;
  }

  stop() {
    if (this.currentProcess) {
      this.currentProcess.kill();
      this.currentProcess = null;
    }
  }
}
```

#### 6. Results Viewer (`src/components/ResultsViewer.js`)
```javascript
class ResultsViewer {
  constructor() {
    this.currentResults = null;
    this.diffMode = 'side-by-side'; // or 'overlay'
  }

  displayResults(results) {
    this.currentResults = results;

    // Summary section
    this.renderSummary(results);

    // Differences list
    this.renderDifferences(results.differences);

    // Visual comparison
    this.highlightDifferences();
  }

  renderSummary(results) {
    const summary = `
      <div class="summary">
        <div class="stat">
          <span class="label">Elements:</span>
          <span class="value">${results.elementComparison.matched}/${results.elementComparison.total}</span>
          <span class="percentage">${results.elementComparison.passRate.toFixed(1)}%</span>
        </div>
        <div class="stat">
          <span class="label">Text Nodes:</span>
          <span class="value">${results.textComparison.matched}/${results.textComparison.total}</span>
          <span class="percentage">${results.textComparison.passRate.toFixed(1)}%</span>
        </div>
      </div>
    `;
    // ... render to DOM
  }

  renderDifferences(differences) {
    const list = differences.map(diff => {
      switch (diff.type) {
        case 'layout_difference':
          return this.renderLayoutDiff(diff);
        case 'text_mismatch':
          return this.renderTextDiff(diff);
        case 'tag_mismatch':
          return this.renderTagDiff(diff);
        default:
          return this.renderGenericDiff(diff);
      }
    }).join('');

    // ... render to DOM
  }

  highlightDifferences() {
    // Overlay difference markers on comparison panels
    // Draw bounding boxes on divergent elements

    if (this.diffMode === 'overlay') {
      this.drawOverlay();
    }
  }

  drawOverlay() {
    // Create canvas overlay
    // Draw red boxes around failed elements
    // Draw green boxes around passed elements

    const canvas = document.createElement('canvas');
    canvas.width = 1200;
    canvas.height = 800;
    const ctx = canvas.getContext('2d');

    this.currentResults.differences.forEach(diff => {
      if (diff.type === 'layout_difference') {
        const { x, y, width, height } = diff.browser;
        ctx.strokeStyle = 'red';
        ctx.lineWidth = 2;
        ctx.strokeRect(x, y, width, height);

        // Show difference value
        ctx.fillStyle = 'red';
        ctx.font = '12px monospace';
        ctx.fillText(
          `Δ${diff.maxDifference.toFixed(1)}px`,
          x + 2, y - 2
        );
      }
    });
  }
}
```

### File Structure

```
./utils/layout-devtool/
├── package.json
├── main.js                    # Electron main process
├── preload.js                 # Context bridge
├── src/
│   ├── index.html            # Main window HTML
│   ├── App.jsx               # Root React component
│   ├── styles/
│   │   ├── main.css
│   │   ├── tree.css
│   │   ├── comparison.css
│   │   └── terminal.css
│   ├── components/
│   │   ├── TestTree.jsx
│   │   ├── ComparisonPanel.jsx
│   │   ├── TerminalPanel.jsx
│   │   ├── ResultsViewer.jsx
│   │   └── MenuBar.jsx
│   ├── services/
│   │   ├── TestRunner.js
│   │   └── ConfigManager.js
│   └── utils/
│       ├── diff.js
│       └── formatter.js
├── assets/
│   ├── icons/
│   └── images/
└── README.md
```

### Data Flow

```
User Interaction
    │
    ├─> Select Test (TestTree)
    │       │
    │       ├─> Load Test Info
    │       └─> Update UI State
    │
    ├─> Run Test (TestRunner)
    │       │
    │       ├─> Execute Lambda CLI
    │       ├─> Stream Output -> Terminal
    │       ├─> Parse Results
    │       └─> Load Reference
    │
    ├─> Compare Results
    │       │
    │       ├─> Element Comparison
    │       ├─> Text Comparison
    │       └─> CSS Comparison
    │
    └─> Display Results (ResultsViewer)
            │
            ├─> Render Browser View
            ├─> Render Lambda View
            ├─> Show Differences
            └─> Highlight Issues
```

## Implementation Phases

### Phase 1: Foundation ✅ COMPLETED
**Goal**: Basic Electron app with test tree and terminal

**Tasks**:
1. ✅ Initialize Electron project
   - Setup package.json with minimal dependencies (Electron, React, xterm.js)
   - Configure Electron main/renderer processes with security (context isolation)
   - Setup preload script with IPC via contextBridge

2. ✅ Implement Test Tree Panel
   - Scan ./test/layout/data directory (excludes .DS_Store and css2.1)
   - Build hierarchical tree structure with 11 categories
   - Render collapsible categories (baseline expanded by default)
   - Handle test selection with visual feedback

3. ✅ Implement Terminal Panel
   - Integrate xterm.js with FitAddon
   - Fixed 120 columns to prevent text wrapping
   - Dynamic row sizing with ResizeObserver
   - Support ANSI colors for formatted output

4. ✅ Basic IPC Communication
   - main <-> renderer message passing via electronAPI
   - Error handling with console logging
   - State management with React hooks

**Deliverable**: ✅ App opens, shows test tree, can run commands in terminal

### Phase 2: Test Execution ✅ COMPLETED
**Goal**: Run tests and display output

**Tasks**:
1. ✅ Implement TestRunner Service
   - Execute Lambda CLI via `make layout test=<name>` commands
   - Parse /tmp/view_tree.json output
   - Handle errors gracefully with try-catch
   - Support process termination

2. ✅ Load Browser References
   - Read reference JSON files from test/layout/reference/
   - Error handling for missing references
   - IPC handler: `load-reference`

3. ✅ Terminal Integration
   - Stream test output to terminal in real-time
   - Show test status (running/pass/fail) with colored output
   - Display formatted test results (emojis, ANSI colors)
   - Proper handling of stdout/stderr streams

4. ✅ Status Indicators
   - Test tree status icons (⚪ not run, ⏳ running, ✅ pass, ❌ fail)
   - Run Test button with disabled state during execution
   - Terminal shows test duration and results

**Deliverable**: ✅ Can run tests via UI, see output in terminal, basic status

**Implementation Notes**:
- Fixed path resolution: `../..` from layout-devtool to project root
- Added `webSecurity: false` to allow loading local HTML files in iframe
- Terminal columns fixed at 120 for proper formatting
- Added ResizeObserver for dynamic terminal height adjustment
- Search functionality in test tree

### Phase 3: Comparison View ✅ COMPLETED
**Goal**: Side-by-side visual comparison

**Tasks**:
1. ✅ Implement Comparison Panel
   - Custom split view with resizable gutter (no Split.js dependency)
   - Drag-to-resize functionality with mouse tracking
   - Left panel: Browser view (20-80% range)
   - Right panel: Lambda view

2. ✅ Browser View Rendering
   - Use iframe to load and render HTML directly with `file://` protocol
   - Provides live, interactive browser rendering
   - Fast and simple implementation
   - Supports native scrolling and interaction
   - Path format: `file:///Users/.../test/layout/data/<category>/<test>.html`

3. ✅ Lambda View Rendering
   - Placeholder shown: "Run test to see Lambda render"
   - Ready for PNG image display when `./lambda.exe render` command is available
   - IPC handler registered: `render-lambda-view`
   - Fallback: empty state with instructions

4. ⏸️ Sync Scrolling (Deferred)
   - Not implemented yet
   - Zoom controls - future enhancement
   - Pan support - future enhancement

**Deliverable**: ✅ Visual side-by-side comparison of browser vs Lambda (browser view functional, Lambda view ready for render command)

**Implementation Notes**:
- Custom CSS flexbox-based resizing instead of Split.js
- Both left panel (test tree) and terminal panel are resizable
- Resize handles show blue highlight on hover
- Terminal adapts rows dynamically to container height
- Left sidebar resizable: 200-600px
- Terminal height resizable: 150-600px
- Fixed terminal text wrapping with 120-column fixed width

**Known Limitations**:
- Lambda render command not yet available in CLI (right panel shows placeholder)
- Sync scrolling between panels not implemented
- No overlay/diff visualization yet

### Phase 4: Results Analysis 🚧 IN PROGRESS
**Goal**: Detailed difference analysis

**Tasks**:
1. Results Viewer Component
   - Parse comparison results
   - Display summary statistics
   - Show element-by-element differences
   - CSS property differences

2. Difference Visualization
   - Overlay mode: highlight differences on image
   - Draw bounding boxes
   - Show dimension deltas
   - Color-code by severity

3. Interactive Exploration
   - Click element to see details
   - Filter differences by type
   - Sort by severity/location
   - Jump to difference in tree

4. Export/Report
   - Save results to JSON
   - Generate HTML report
   - Copy diff to clipboard
   - Screenshot comparison

**Deliverable**: Detailed analysis UI with interactive difference exploration

### Phase 5: Polish & Features ⏳ PENDING
**Goal**: Production-ready tool

**Tasks**:
1. Batch Testing
   - Run all tests in category
   - Run all tests in suite
   - Parallel execution support
   - Progress tracking

2. File Watching
   - Monitor test HTML files
   - Auto-rerun on change
   - Optional: monitor Lambda binary

3. Configuration
   - Tolerance settings
   - Engine selection (Lambda CSS vs Radiant)
   - Viewport size
   - Theme (dark/light mode)

4. Keyboard Shortcuts
   - Run test: Cmd/Ctrl+R
   - Stop test: Cmd/Ctrl+.
   - Next test: Cmd/Ctrl+Down
   - Previous test: Cmd/Ctrl+Up
   - Toggle comparison: Cmd/Ctrl+T

5. Search & Filter
   - Search tests by name
   - Filter by status (passed/failed/not run)
   - Filter by category
   - Recent tests history

6. UI Polish
   - Loading states
   - Error messages
   - Tooltips
   - Responsive layout
   - Animations

**Deliverable**: Fully-featured, polished development tool

## Technical Considerations

### Performance
- **Lazy Loading**: Load test results on-demand, not upfront
- **Virtual Scrolling**: For large test lists (use react-window or similar)
- **Caching**: Cache browser references, test results
- **Worker Threads**: Heavy parsing in background threads
- **Debouncing**: File watching, resize events

### Error Handling
- **Graceful Degradation**: Missing references, failed tests
- **Retry Logic**: Network issues, process crashes
- **User Feedback**: Clear error messages, recovery suggestions
- **Logging**: Debug logs for troubleshooting

### Cross-Platform
- **Path Handling**: Use path.join(), normalize separators
- **Process Spawning**: Shell escaping, environment variables
- **File Permissions**: Check executability of Lambda CLI
- **Native Modules**: Avoid if possible, or provide fallbacks

### Integration
- **Makefile Integration**: Support existing `make layout` commands
- **Script Reuse**: Leverage existing Node.js test scripts
- **Data Format**: Compatible with existing JSON schemas
- **Output Location**: Use /tmp/view_tree.json as Lambda does

## Testing Strategy

### Unit Tests
- TestTree: directory scanning, filtering
- TestRunner: command execution, output parsing
- ComparisonPanel: diff calculation, rendering

### Integration Tests
- End-to-end: select test -> run -> compare -> display
- IPC: main <-> renderer communication
- File system: read/write operations

### Manual Testing
- UI responsiveness
- Visual accuracy
- Error scenarios
- Performance with 270+ tests

## Documentation

### User Guide
- Installation instructions
- Basic usage walkthrough
- Keyboard shortcuts reference
- Configuration options
- Troubleshooting

### Developer Guide
- Architecture overview
- Component documentation
- IPC protocol
- Adding new features
- Build and deployment

## Future Enhancements

### Advanced Features
1. **Test Recording**: Record browser interactions, replay in Lambda
2. **Visual Regression**: Detect visual changes over time
3. **Performance Profiling**: Layout performance metrics
4. **Diff Viewer**: Monaco-based side-by-side code diff
5. **Network Inspector**: CSS/resource loading analysis
6. **Accessibility Checks**: A11y validation
7. **Mobile Viewport**: Test responsive layouts
8. **CSS Coverage**: Unused CSS detection
9. **Animation Playback**: Frame-by-frame comparison
10. **Export to CI**: Generate JUnit XML for CI/CD

### Integration Opportunities
1. **VS Code Extension**: Open test from editor
2. **Browser DevTools**: Inspect reference in browser
3. **Git Integration**: Blame for test failures
4. **Slack/Discord**: Test failure notifications
5. **Database**: Store historical test results

## Dependencies

### Core Dependencies (Minimal)
```json
{
  "dependencies": {
    "electron": "^28.0.0",
    "react": "^18.2.0",
    "react-dom": "^18.2.0",
    "xterm": "^5.3.0",
    "xterm-addon-fit": "^0.8.0"
  },
  "devDependencies": {
    "electron-builder": "^24.9.1",
    "vite": "^5.0.0",
    "@vitejs/plugin-react": "^4.2.0"
  }
}
```

**Note**: Split.js and chokidar are optional - we can implement basic resizing with CSS flexbox and file watching with Node.js fs.watch()

### UI Framework
```json
{
  "dependencies": {
    "react": "^18.2.0",
    "react-dom": "^18.2.0"
  },
  "devDependencies": {
    "@types/react": "^18.2.0",
    "@types/react-dom": "^18.2.0"
  }
}
```

### Build Tools
- electron-builder: Package for macOS/Linux/Windows
- vite: Fast dev server and bundling

## Deployment

### Development
```bash
# From project root
make layout-devtool

# Or directly
cd utils/layout-devtool
npm install
npm run electron:dev  # Start Vite dev server + Electron
```

### Makefile Integration
Added `make layout-devtool` target in project root Makefile:
```makefile
layout-devtool:
	@echo "🚀 Launching Layout DevTool..."
	@if [ -d "utils/layout-devtool" ]; then \
		cd utils/layout-devtool && npm run electron:dev; \
	else \
		echo "❌ Error: Layout DevTool not found at utils/layout-devtool"; \
		exit 1; \
	fi
```

### Production Build
```bash
npm run build:mac     # macOS .app
npm run build:linux   # Linux AppImage
npm run build:win     # Windows .exe
```

### Distribution
- macOS: .dmg installer
- Linux: .AppImage or .deb
- Windows: .exe installer or portable zip

## Success Metrics

### Usability
- ✅ Can run any test in < 3 clicks (select test → click Run Test)
- ✅ Test results appear in < 2 seconds (local test)
- ⏸️ Differences are immediately visible (browser view works, Lambda render pending)
- ✅ Clear indication of pass/fail status (terminal output with colors/emojis)

### Coverage
- ✅ Supports all 270+ existing tests (11 categories loaded)
- ✅ Works with all test categories (baseline, basic, box, flex, grid, etc.)
- ⏸️ Handles both Lambda CSS and Radiant engines (Lambda only for now)
- ✅ Compatible with existing Makefile workflow (`make layout test=<name>`)

### Reliability
- ✅ No crashes during normal operation
- ✅ Graceful handling of missing references (error logging)
- ✅ Recovers from failed test executions (try-catch blocks)
- ⏸️ Preserves state across app restarts (not implemented)

## Timeline Estimate

- **Phase 1 (Foundation)**: ✅ Completed (1 day)
- **Phase 2 (Test Execution)**: ✅ Completed (1 day)
- **Phase 3 (Comparison View)**: ✅ Completed (1 day)
- **Phase 4 (Results Analysis)**: 🚧 In Progress
- **Phase 5 (Polish)**: ⏳ Pending

**Actual Progress**: 3 days for Phases 1-3 MVP
**Remaining**: Phases 4-5 for production-ready version

## Current Status (November 27, 2025)

### What's Working
1. ✅ **Test Tree**: 11 categories, 270+ tests, search functionality
2. ✅ **Test Execution**: Run tests via `make layout`, see real-time output
3. ✅ **Terminal**: xterm.js with proper formatting (120 cols, dynamic rows)
4. ✅ **Browser View**: iframe displaying HTML test files
5. ✅ **Resizable Panels**: Left sidebar (200-600px), terminal height (150-600px)
6. ✅ **Make Integration**: `make layout-devtool` launches app from project root

### What's Pending
1. ⏸️ **Lambda Render**: Waiting for `./lambda.exe render` command implementation
2. ⏸️ **Diff Visualization**: Overlay mode, bounding boxes, difference highlights
3. ⏸️ **Results Analysis**: Detailed comparison statistics, interactive exploration
4. ⏸️ **Batch Testing**: Run multiple tests, parallel execution
5. ⏸️ **File Watching**: Auto-rerun on file changes
6. ⏸️ **Test History**: Persist results across sessions

### Known Issues
1. ✅ Fixed: xterm dimensions error (added ResizeObserver, fixed columns)
2. ✅ Fixed: Terminal text wrapping (set fixed 120 columns)
3. ✅ Fixed: File protocol error (added `webSecurity: false`)
4. ✅ Fixed: Path resolution (corrected to `../..` from layout-devtool)
5. ✅ Fixed: Development mode detection (added cross-env, NODE_ENV check)

## Conclusion

This Electron app will significantly improve the Lambda layout testing workflow by providing:
1. **Visual feedback**: See exactly how Lambda renders vs. browser
2. **Faster iteration**: Run tests without command line
3. **Better debugging**: Interactive exploration of differences
4. **Batch testing**: Run multiple tests efficiently
5. **Historical tracking**: See test trends over time

The design leverages existing infrastructure (test scripts, Makefile, JSON formats) while adding a modern GUI layer that streamlines the development and testing process.
