const { app, BrowserWindow, ipcMain, protocol } = require('electron');
const path = require('path');
const fs = require('fs').promises;
const fsSync = require('fs');
const { spawn } = require('child_process');

class LayoutDevTool {
  constructor() {
    this.mainWindow = null;
    this.projectRoot = path.resolve(__dirname, '../..');
    this.testDataDir = path.join(this.projectRoot, 'test/layout/data');
    this.referenceDir = path.join(this.projectRoot, 'test/layout/reference');
    this.lambdaExe = path.join(this.projectRoot, 'lambda.exe');
    this.recentTests = []; // Track recent test history

    console.log('LayoutDevTool initialized:');
    console.log('  __dirname:', __dirname);
    console.log('  projectRoot:', this.projectRoot);
    console.log('  testDataDir:', this.testDataDir);
  }

  registerProtocol() {
    // Register a custom protocol to serve local test files
    // This allows the renderer to access local files without cross-origin issues
    protocol.registerFileProtocol('testfile', (request, callback) => {
      // URL format: testfile://path/to/file
      const filePath = decodeURIComponent(request.url.replace('testfile://', ''));
      callback({ path: filePath });
    });
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
      // Dev tools can be opened with Cmd+Option+I or F12
      // No auto-open to keep workspace clean
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

    // Render Lambda view - runs ./lambda.exe render <testPath> -o output.png
    ipcMain.handle('render-lambda-view', async (event, testPath, viewportWidth, viewportHeight) => {
      return await this.renderLambdaView(testPath, viewportWidth, viewportHeight);
    });

    // Measure page content height using a hidden BrowserWindow
    ipcMain.handle('measure-page-height', async (event, testPath, viewportWidth) => {
      return await this.measurePageHeight(testPath, viewportWidth);
    });

    // Read log file
    ipcMain.handle('read-log-file', async () => {
      return await this.readLogFile();
    });

    // Read view tree file
    ipcMain.handle('read-view-tree-file', async () => {
      return await this.readViewTreeFile();
    });

    // Read HTML source file
    ipcMain.handle('read-html-source', async (event, testPath) => {
      return await this.readHtmlSource(testPath);
    });

    // Read HTML tree file
    ipcMain.handle('read-html-tree-file', async () => {
      return await this.readHtmlTreeFile();
    });

    // Get project root path
    ipcMain.handle('get-project-root', async () => {
      return this.projectRoot;
    });

    // Get recent tests
    ipcMain.handle('get-recent-tests', async () => {
      return this.recentTests;
    });

    // Add test to recent history
    ipcMain.handle('add-recent-test', async (event, testInfo) => {
      // Remove duplicate if exists
      this.recentTests = this.recentTests.filter(
        t => !(t.category === testInfo.category && t.testFile === testInfo.testFile)
      );
      // Add to front, limit to 10 most recent
      this.recentTests.unshift(testInfo);
      if (this.recentTests.length > 10) {
        this.recentTests = this.recentTests.slice(0, 10);
      }
      return this.recentTests;
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

  async readLogFile() {
    try {
      const logPath = path.join(this.projectRoot, 'log.txt');
      const content = await fs.readFile(logPath, 'utf8');
      return content;
    } catch (error) {
      console.error('Failed to read log file:', error);
      return '';
    }
  }

  async readViewTreeFile() {
    try {
      const viewTreePath = path.join(this.projectRoot, 'view_tree.txt');
      const content = await fs.readFile(viewTreePath, 'utf8');
      return content;
    } catch (error) {
      console.error('Failed to read view_tree.txt:', error);
      return '';
    }
  }

  async readHtmlTreeFile() {
    try {
      const htmlTreePath = path.join(this.projectRoot, 'html_tree.txt');
      const content = await fs.readFile(htmlTreePath, 'utf8');
      return content;
    } catch (error) {
      console.error('Failed to read html_tree.txt:', error);
      return '';
    }
  }

  async readHtmlSource(testPath) {
    try {
      const absolutePath = path.join(this.projectRoot, testPath);
      console.log('Reading HTML source:', absolutePath);
      const content = await fs.readFile(absolutePath, 'utf8');
      return content;
    } catch (error) {
      console.error('Failed to read HTML source:', error);
      throw new Error(`Failed to read file: ${testPath}`);
    }
  }

  // Measure page content height using a hidden BrowserWindow
  async measurePageHeight(testPath, viewportWidth) {
    return new Promise((resolve, reject) => {
      const absolutePath = path.join(this.projectRoot, testPath);

      // Create a hidden window to load and measure the page
      const measureWindow = new BrowserWindow({
        width: viewportWidth,
        height: 600,
        show: false,
        webPreferences: {
          nodeIntegration: false,
          contextIsolation: true
        }
      });

      measureWindow.loadFile(absolutePath);

      measureWindow.webContents.on('did-finish-load', async () => {
        try {
          // Execute JavaScript in the page context to get content height
          const height = await measureWindow.webContents.executeJavaScript(`
            Math.max(
              document.body.scrollHeight,
              document.body.offsetHeight,
              document.documentElement.scrollHeight,
              document.documentElement.offsetHeight
            )
          `);

          console.log('Measured page height:', height, 'for', testPath);
          measureWindow.close();
          resolve(height);
        } catch (e) {
          console.error('Failed to measure page height:', e);
          measureWindow.close();
          resolve(600); // fallback
        }
      });

      measureWindow.webContents.on('did-fail-load', (event, errorCode, errorDescription) => {
        console.error('Failed to load page for measurement:', errorDescription);
        measureWindow.close();
        resolve(600); // fallback
      });

      // Timeout after 5 seconds
      setTimeout(() => {
        if (!measureWindow.isDestroyed()) {
          measureWindow.close();
          resolve(600);
        }
      }, 5000);
    });
  }

  async renderLambdaView(testPath, viewportWidth = 1200, viewportHeight = 800) {
    return new Promise(async (resolve, reject) => {
      // Output path with timestamp to avoid caching
      const outputDir = path.join(this.projectRoot, 'test_output');
      const outputPath = path.join(outputDir, 'lambda_render.png');

      // Ensure output directory exists
      try {
        await fs.mkdir(outputDir, { recursive: true });
      } catch (error) {
        console.error('Failed to create output directory:', error);
        reject(error);
        return;
      }

      // Construct the absolute path to the test file
      const absoluteTestPath = path.join(this.projectRoot, testPath);

      // Use provided viewport dimensions or defaults
      const width = viewportWidth > 0 ? viewportWidth : 1200;
      const height = viewportHeight > 0 ? viewportHeight : 800;

      console.log('Rendering Lambda view:');
      console.log('  testPath:', testPath);
      console.log('  absoluteTestPath:', absoluteTestPath);
      console.log('  outputPath:', outputPath);
      console.log('  viewport:', width, 'x', height);

      // Detect pixel ratio for HiDPI displays (Retina on macOS)
      // Electron's screen module can provide devicePixelRatio
      const { screen } = require('electron');
      const primaryDisplay = screen.getPrimaryDisplay();
      const pixelRatio = primaryDisplay.scaleFactor || 1;
      console.log('  pixelRatio:', pixelRatio);

      // Run: ./lambda.exe render <testPath> -o output.png -vw <width> -vh <height> --pixel-ratio <ratio>
      const args = ['render', absoluteTestPath, '-o', outputPath, '-vw', String(width), '-vh', String(height), '--pixel-ratio', String(pixelRatio)];
      const renderProcess = spawn(this.lambdaExe, args, {
        cwd: this.projectRoot
      });

      let stdout = '';
      let stderr = '';

      renderProcess.stdout.on('data', (data) => {
        const text = data.toString();
        stdout += text;
        this.mainWindow?.webContents.send('terminal-output', text);
      });

      renderProcess.stderr.on('data', (data) => {
        const text = data.toString();
        stderr += text;
        this.mainWindow?.webContents.send('terminal-output', text);
      });

      renderProcess.on('close', async (code) => {
        console.log('Render process exited with code:', code);
        if (code === 0) {
          // Check if output file exists
          try {
            await fs.access(outputPath);
            // Return the file path with a cache-busting timestamp and the pixel ratio
            const timestamp = Date.now();
            resolve({
              path: `file://${outputPath}?t=${timestamp}`,
              pixelRatio: pixelRatio
            });
          } catch (e) {
            console.error('Output file not found:', outputPath);
            reject(new Error('Render output file not created'));
          }
        } else {
          reject(new Error(`Render failed with exit code ${code}: ${stderr}`));
        }
      });

      renderProcess.on('error', (error) => {
        console.error('Render process error:', error);
        reject(error);
      });
    });
  }

  initialize() {
    app.whenReady().then(() => {
      this.registerProtocol();  // Register custom protocol for local file access
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
