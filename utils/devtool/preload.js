const { contextBridge, ipcRenderer } = require('electron');

function inspectorBridge(channel) {
  const subscribe = (event, callback) => {
    const listener = (_event, value) => callback(value);
    ipcRenderer.on(`${channel}-${event}`, listener);
    return () => ipcRenderer.removeListener(`${channel}-${event}`, listener);
  };
  return {
    start: (document, headless) => ipcRenderer.invoke(`${channel}-start`, document, headless),
    send: text => ipcRenderer.invoke(`${channel}-send`, text),
    stop: () => ipcRenderer.invoke(`${channel}-stop`),
    onMessage: callback => subscribe('message', callback),
    onClose: callback => subscribe('closed', callback),
    onOutput: callback => subscribe('output', callback)
  };
}
const documentInspector = inspectorBridge('devtools');

// Expose protected methods that allow the renderer process to use
// the ipcRenderer without exposing the entire object
contextBridge.exposeInMainWorld('electronAPI', {
  devtoolsStart: documentInspector.start,
  devtoolsSend: documentInspector.send,
  devtoolsStop: documentInspector.stop,
  onDevtoolsMessage: documentInspector.onMessage,
  onDevtoolsClose: documentInspector.onClose,
  onDevtoolsOutput: documentInspector.onOutput,
  scratch: inspectorBridge('devtools-scratch'),
  // Test tree operations
  loadTestTree: () => ipcRenderer.invoke('load-test-tree'),

  // Test execution
  runTest: (testPath, options) => ipcRenderer.invoke('run-test', testPath, options),

  // Reference data
  loadReference: (testName, category) => ipcRenderer.invoke('load-reference', testName, category),

  // Lambda render with viewport dimensions
  renderLambdaView: (testPath, viewportWidth, viewportHeight) =>
    ipcRenderer.invoke('render-lambda-view', testPath, viewportWidth, viewportHeight),

  // Measure page content height
  measurePageHeight: (testPath, viewportWidth) =>
    ipcRenderer.invoke('measure-page-height', testPath, viewportWidth),

  // Log file
  readLogFile: () => ipcRenderer.invoke('read-log-file'),
  readLogLines: (options) => ipcRenderer.invoke('read-log-lines', options),

  // View tree file
  readViewTreeFile: () => ipcRenderer.invoke('read-view-tree-file'),

  // HTML source file
  readHtmlSource: (testPath) => ipcRenderer.invoke('read-html-source', testPath),

  // HTML tree file
  readHtmlTreeFile: () => ipcRenderer.invoke('read-html-tree-file'),

  // Get project root path
  getProjectRoot: () => ipcRenderer.invoke('get-project-root'),

  // Struct census
  loadStructCensus: () => ipcRenderer.invoke('load-struct-census'),
  regenStructCensus: (full) => ipcRenderer.invoke('regen-struct-census', full),

  // Recent tests
  getRecentTests: () => ipcRenderer.invoke('get-recent-tests'),
  addRecentTest: (testInfo) => ipcRenderer.invoke('add-recent-test', testInfo),

  // Render test APIs
  loadRenderTests: () => ipcRenderer.invoke('load-render-tests'),
  runRenderTest: (testName, renderDir, outputFormat) =>
    ipcRenderer.invoke('run-render-test', testName, renderDir, outputFormat),
  getRenderTestImages: (testName, renderDir, outputFormat) =>
    ipcRenderer.invoke('get-render-test-images', testName, renderDir, outputFormat),

  // PDF render test APIs
  loadPdfRenderTests: () => ipcRenderer.invoke('load-pdf-render-tests'),
  runPdfRenderTest: (testName, renderDir) => ipcRenderer.invoke('run-pdf-render-test', testName, renderDir),

  // Terminal output listener
  onTerminalOutput: (callback) => {
    ipcRenderer.on('terminal-output', (event, data) => callback(data));
  },

  // Remove terminal output listener
  removeTerminalOutputListener: () => {
    ipcRenderer.removeAllListeners('terminal-output');
  }
});
