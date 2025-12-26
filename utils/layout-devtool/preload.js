const { contextBridge, ipcRenderer } = require('electron');

// Expose protected methods that allow the renderer process to use
// the ipcRenderer without exposing the entire object
contextBridge.exposeInMainWorld('electronAPI', {
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

  // View tree file
  readViewTreeFile: () => ipcRenderer.invoke('read-view-tree-file'),

  // HTML source file
  readHtmlSource: (testPath) => ipcRenderer.invoke('read-html-source', testPath),

  // HTML tree file
  readHtmlTreeFile: () => ipcRenderer.invoke('read-html-tree-file'),

  // Recent tests
  getRecentTests: () => ipcRenderer.invoke('get-recent-tests'),
  addRecentTest: (testInfo) => ipcRenderer.invoke('add-recent-test', testInfo),

  // Terminal output listener
  onTerminalOutput: (callback) => {
    ipcRenderer.on('terminal-output', (event, data) => callback(data));
  },

  // Remove terminal output listener
  removeTerminalOutputListener: () => {
    ipcRenderer.removeAllListeners('terminal-output');
  }
});
