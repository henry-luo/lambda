const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
const fs = require('fs').promises;
const { spawn } = require('child_process');

class LayoutDevTool {
  constructor() {
    this.mainWindow = null;
    this.projectRoot = path.resolve(__dirname, '../..');
    this.testDataDir = path.join(this.projectRoot, 'test/layout/data');
    this.referenceDir = path.join(this.projectRoot, 'test/layout/reference');
    this.lambdaExe = path.join(this.projectRoot, 'lambda.exe');

    console.log('LayoutDevTool initialized:');
    console.log('  __dirname:', __dirname);
    console.log('  projectRoot:', this.projectRoot);
    console.log('  testDataDir:', this.testDataDir);
  }

  createWindow() {
    this.mainWindow = new BrowserWindow({
      width: 1600,
      height: 1000,
      webPreferences: {
        nodeIntegration: false,
        contextIsolation: true,
        preload: path.join(__dirname, 'preload.js'),
        webSecurity: false  // Allow loading local files for test viewing
      },
      title: 'Layout DevTool'
    });

    // Load from Vite dev server in development, or from built files in production
    const isDevelopment = process.env.NODE_ENV === 'development' || process.argv.includes('--dev');

    if (isDevelopment) {
      this.mainWindow.loadURL('http://localhost:5173');
      this.mainWindow.webContents.openDevTools();
    } else {
      this.mainWindow.loadFile(path.join(__dirname, 'dist/index.html'));
    }
  }

  setupIPC() {
    // Load test tree
    ipcMain.handle('load-test-tree', async () => {
      return await this.loadTestTree();
    });

    // Run test
    ipcMain.handle('run-test', async (event, testPath, options) => {
      return await this.runTest(testPath, options);
    });

    // Load reference data
    ipcMain.handle('load-reference', async (event, testName, category) => {
      return await this.loadReference(testName, category);
    });

    // Render Lambda view (future: when render command is available)
    ipcMain.handle('render-lambda-view', async (event, testPath) => {
      // For now, return null (render command not yet implemented)
      return null;
    });

    // Stream process output
    ipcMain.on('process-output', (event, data) => {
      if (this.mainWindow) {
        this.mainWindow.webContents.send('terminal-output', data);
      }
    });
  }

  async loadTestTree() {
    try {
      console.log('loadTestTree called');
      console.log('testDataDir:', this.testDataDir);
      const categories = await fs.readdir(this.testDataDir);
      console.log('Found categories:', categories);
      const tree = [];

      for (const category of categories) {
        // Skip hidden files and css2.1
        if (category.startsWith('.') || category === 'css2.1') {
          continue;
        }

        const categoryPath = path.join(this.testDataDir, category);
        const stat = await fs.stat(categoryPath);

        if (stat.isDirectory()) {
          const files = await fs.readdir(categoryPath);
          const tests = files.filter(f =>
            (f.endsWith('.html') || f.endsWith('.htm')) && !f.startsWith('.')
          );

          tree.push({
            name: category,
            tests: tests.sort()
          });
        }
      }

      const result = tree.sort((a, b) => a.name.localeCompare(b.name));
      console.log('Returning tree:', result);
      return result;
    } catch (error) {
      console.error('Failed to load test tree:', error);
      return [];
    }
  }

  async runTest(testPath, options = {}) {
    return new Promise((resolve, reject) => {
      const testName = path.basename(testPath, path.extname(testPath));

      // Run via make layout command
      const args = ['layout', `test=${testName}`];
      const process = spawn('make', args, {
        cwd: this.projectRoot,
        shell: true
      });

      let stdout = '';
      let stderr = '';

      process.stdout.on('data', (data) => {
        const text = data.toString();
        stdout += text;
        this.mainWindow?.webContents.send('terminal-output', text);
      });

      process.stderr.on('data', (data) => {
        const text = data.toString();
        stderr += text;
        this.mainWindow?.webContents.send('terminal-output', text);
      });

      process.on('close', async (code) => {
        try {
          // Load the output and reference
          const outputPath = '/tmp/view_tree.json';
          let lambdaOutput = null;

          try {
            const outputContent = await fs.readFile(outputPath, 'utf8');
            lambdaOutput = JSON.parse(outputContent);
          } catch (e) {
            console.error('Failed to load Lambda output:', e);
          }

          resolve({
            exitCode: code,
            stdout,
            stderr,
            lambdaOutput
          });
        } catch (error) {
          reject(error);
        }
      });

      process.on('error', (error) => {
        reject(error);
      });
    });
  }

  async loadReference(testName, category) {
    try {
      const refPath = path.join(this.referenceDir, category, `${testName}.json`);
      const content = await fs.readFile(refPath, 'utf8');
      return JSON.parse(content);
    } catch (error) {
      console.error('Failed to load reference:', error);
      return null;
    }
  }

  initialize() {
    app.whenReady().then(() => {
      this.setupIPC();  // Setup IPC handlers BEFORE creating window
      this.createWindow();

      app.on('activate', () => {
        if (BrowserWindow.getAllWindows().length === 0) {
          this.createWindow();
        }
      });
    });

    app.on('window-all-closed', () => {
      if (process.platform !== 'darwin') {
        app.quit();
      }
    });
  }
}

// Initialize the application
const layoutDevTool = new LayoutDevTool();
layoutDevTool.initialize();
