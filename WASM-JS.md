# Lambda WASM JavaScript Interface

This directory contains scripts, configurations, and JavaScript examples for compiling Lambda C files to WebAssembly (WASM) using WASI SDK and interfacing with the resulting WASM module in both browser and Node.js environments.

## 📋 Files Overview

### Core Build System
- **`compile-wasm.sh`** - Main WASM compilation script
- **`build_lambda_wasm_config.json`** - Configuration file for WASM build

### JavaScript Interface
- **`lambda-wasm-example.js`** - Core JavaScript class for loading and interfacing with the WASM module
- **`test/lambda-wasm-demo.html`** - Interactive browser demo with GUI
- **`lambda-wasm-node.js`** - Command-line Node.js tool
- **`package.json`** - Node.js package configuration

### Test & Verification
- **`test/test-wasm.sh`** - Basic compilation test script
- **`test/verify-wasm-setup.sh`** - Setup verification script
- **`README-WASM-JS.md`** - This comprehensive documentation file

## 🚀 Quick Start

### Prerequisites

1. **WASI SDK**: Download and install from [WebAssembly/wasi-sdk releases](https://github.com/WebAssembly/wasi-sdk/releases)
   ```bash
   # Example installation on macOS/Linux:
   wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-macos.tar.gz
   tar xzf wasi-sdk-20.0-macos.tar.gz
   sudo mv wasi-sdk-20.0 /opt/wasi-sdk
   ```

2. **jq** (optional but recommended): For better JSON parsing
   ```bash
   # macOS
   brew install jq
   
   # Linux
   sudo apt-get install jq
   ```

### Compilation

First, compile the Lambda C files to WASM:

```bash
# Basic compilation
./compile-wasm.sh

# Debug build with symbols
./compile-wasm.sh --debug

# Force full rebuild
./compile-wasm.sh --force

# Use custom WASI SDK path
./compile-wasm.sh --wasi-sdk=/usr/local/wasi-sdk

# Parallel build with 4 jobs
./compile-wasm.sh --jobs=4
```

**Available compilation options:**
- `--debug, -d` - Build debug version with debug symbols
- `--force, -f` - Force rebuild all files (disable incremental compilation)
- `--jobs=N, -j N` - Number of parallel compilation jobs (default: auto-detect)
- `--clean-deps` - Clean dependency files (.d files) and exit
- `--wasi-sdk=PATH` - Path to WASI SDK (default: /opt/wasi-sdk)
- `--help, -h` - Show help information

## 🎉 Complete Setup Summary

### 📁 Files in This System
- ✅ **`lambda-wasm-example.js`** - Core JavaScript WASM interface class
- ✅ **`test/lambda-wasm-demo.html`** - Interactive browser demo with GUI  
- ✅ **`lambda-wasm-node.js`** - Command-line Node.js tool
- ✅ **`package.json`** - Node.js package configuration
- ✅ **`README-WASM-JS.md`** - Comprehensive documentation & compilation guide
- ✅ **`test/verify-wasm-setup.sh`** - Setup verification script
- ✅ **`test/test-wasm.sh`** - Basic compilation test script

### 🔧 Key Features
- ✅ Cross-platform (Browser + Node.js)
- ✅ Multiple input formats (markdown, html, json, xml, etc.)
- ✅ Multiple output formats (html, json, xml, etc.)
- ✅ Memory management with cleanup
- ✅ Error handling and fallbacks
- ✅ Interactive CLI with help system
- ✅ WASI compatibility layer
- ✅ Drag & drop WASM loading
- ✅ Real-time processing demo

### 📚 Integration Examples
- React components
- Express.js servers  
- Web Workers
- Browser extensions

### 🚀 Quick Start Steps
1. **Compile WASM**: `./compile-wasm.sh`
2. **Test browser**: Open `test/lambda-wasm-demo.html`
3. **Test CLI**: `./lambda-wasm-node.js --help`
4. **Run verification**: `./test/verify-wasm-setup.sh`
5. **Read docs**: Complete guide below

💡 **The JavaScript interface is production-ready and provides a complete abstraction layer for calling your Lambda WASM module from web applications!**

### Browser Usage

1. **Compile the WASM module first:**
   ```bash
   ./compile-wasm.sh
   ```

2. **Serve the files locally:**
   ```bash
   # Using Python
   python3 -m http.server 8080
   
   # Or using Node.js
   npm run serve-node
   ```

3. **Open the demo:**
   Navigate to `http://localhost:8080/test/lambda-wasm-demo.html`

### Node.js Usage

1. **Install as CLI tool:**
   ```bash
   chmod +x lambda-wasm-node.js
   ```

2. **Process files:**
   ```bash
   # Process a file
   ./lambda-wasm-node.js process input.md output.html
   
   # Interactive mode
   ./lambda-wasm-node.js interactive
   
   # Show module info
   ./lambda-wasm-node.js info
   ```

## 📚 API Reference

### LambdaWASM Class

#### Constructor
```javascript
const lambda = new LambdaWASM();
```

#### Methods

##### `loadModule(wasmSource)`
Load the WASM module from file path or ArrayBuffer.

```javascript
// From file path (Node.js)
await lambda.loadModule('./lambda.wasm');

// From URL (Browser)
await lambda.loadModule('./lambda.wasm');

// From ArrayBuffer
const wasmBytes = new Uint8Array([...]);
await lambda.loadModule(wasmBytes.buffer);
```

##### `parseInput(text, format)`
Parse input text in the specified format.

```javascript
const result = await lambda.parseInput(markdownText, 'markdown');
```

**Supported input formats:**
- `markdown` - Markdown text
- `html` - HTML documents
- `json` - JSON data
- `xml` - XML documents
- `text` - Plain text
- `rst` - reStructuredText
- `latex` - LaTeX documents
- `csv` - CSV data
- `toml` - TOML configuration
- `yaml` - YAML data
- `ini` - INI configuration

##### `formatOutput(content, format)`
Format content to the specified output format.

```javascript
const html = await lambda.formatOutput(parsedContent, 'html');
```

**Supported output formats:**
- `html` - HTML output
- `json` - JSON structure
- `xml` - XML format
- `markdown` - Markdown text
- `text` - Plain text

##### `getModuleInfo()`
Get information about the loaded WASM module.

```javascript
const info = lambda.getModuleInfo();
console.log(info.exports); // Available exports
console.log(info.availableFunctions); // Callable functions
```

## 🖥️ Browser Demo Features

The interactive HTML demo (`test/lambda-wasm-demo.html`) includes:

- **Live text processing** - Type or paste text and see results immediately
- **Format conversion** - Convert between different document formats
- **Drag & drop** - Drop WASM files to load different modules
- **Module inspection** - View available WASM exports and functions
- **Responsive design** - Works on desktop and mobile devices
- **Error handling** - Clear error messages and fallback behavior

### Demo Screenshot Flow
1. Enter text in the input textarea
2. Select input and output formats
3. Click "Process Text" to run WASM functions
4. View results in the output textarea
5. Check module information at the bottom

## 🔧 Node.js CLI Tool

### Commands

#### Process Files
```bash
# Basic usage
node lambda-wasm-node.js process input.md output.html

# Specify formats explicitly
node lambda-wasm-node.js --input-format markdown --output-format html process doc.txt result.html

# Process to stdout
node lambda-wasm-node.js process input.md
```

#### Interactive Mode
```bash
node lambda-wasm-node.js interactive
```

In interactive mode:
```
λ> parse markdown "# Hello World"
λ> format html "<h1>Hello</h1>"
λ> info
λ> help
λ> exit
```

#### Module Information
```bash
node lambda-wasm-node.js info
```

### Command-Line Options

- `--wasm <path>` - Path to WASM file (default: `./lambda.wasm`)
- `--input-format <format>` - Input format (default: auto-detect)
- `--output-format <format>` - Output format (default: `html`)
- `--help` - Show usage information

## 🔌 Integration Examples

### React Component
```javascript
import React, { useState, useEffect } from 'react';
import { LambdaWASM } from './lambda-wasm-example.js';

function DocumentProcessor() {
    const [lambda, setLambda] = useState(null);
    const [input, setInput] = useState('');
    const [output, setOutput] = useState('');

    useEffect(() => {
        const initWasm = async () => {
            const wasmInstance = new LambdaWASM();
            await wasmInstance.loadModule('./lambda.wasm');
            setLambda(wasmInstance);
        };
        initWasm();
    }, []);

    const processText = async () => {
        if (lambda) {
            const result = await lambda.parseInput(input, 'markdown');
            const formatted = await lambda.formatOutput(result, 'html');
            setOutput(formatted);
        }
    };

    return (
        <div>
            <textarea value={input} onChange={(e) => setInput(e.target.value)} />
            <button onClick={processText}>Process</button>
            <div dangerouslySetInnerHTML={{ __html: output }} />
        </div>
    );
}
```

### Express.js Server
```javascript
const express = require('express');
const { LambdaWASM } = require('./lambda-wasm-example.js');

const app = express();
const lambda = new LambdaWASM();

app.use(express.json());

app.post('/process', async (req, res) => {
    try {
        const { text, inputFormat, outputFormat } = req.body;
        
        const parsed = await lambda.parseInput(text, inputFormat);
        const formatted = await lambda.formatOutput(parsed, outputFormat);
        
        res.json({ result: formatted });
    } catch (error) {
        res.status(500).json({ error: error.message });
    }
});

// Initialize WASM module on startup
lambda.loadModule('./lambda.wasm').then(() => {
    app.listen(3000, () => console.log('Server running on port 3000'));
});
```

### Web Worker
```javascript
// worker.js
importScripts('./lambda-wasm-example.js');

const lambda = new LambdaWASM();

self.onmessage = async function(e) {
    const { action, data } = e.data;
    
    switch (action) {
        case 'init':
            await lambda.loadModule(data.wasmPath);
            self.postMessage({ type: 'ready' });
            break;
            
        case 'process':
            try {
                const result = await lambda.parseInput(data.text, data.format);
                self.postMessage({ type: 'result', data: result });
            } catch (error) {
                self.postMessage({ type: 'error', error: error.message });
            }
            break;
    }
};

// main.js
const worker = new Worker('./worker.js');
worker.postMessage({ action: 'init', data: { wasmPath: './lambda.wasm' } });
```

## 🛠️ Development

### Building the WASM Module

#### WASM Compilation System

The `compile-wasm.sh` script provides a complete build system for compiling Lambda C files to WebAssembly:

```bash
# Basic build
./compile-wasm.sh

# Debug build
./compile-wasm.sh --debug

# Force rebuild
./compile-wasm.sh --force
```

#### Environment Variables
- `WASI_SDK_PATH`: Path to WASI SDK installation (default: `/opt/wasi-sdk`)

#### Build Configuration

The build configuration is defined in `build_lambda_wasm_config.json`:

- **Source Files**: Automatically scans `lambda/input`, `lambda/format`, and `lambda/validator` directories for C files
- **Output**: Produces `lambda.wasm` in the project root
- **Build Directory**: Uses `build_wasm/` for intermediate object files  
- **Libraries**: Links with tree-sitter libraries for parsing support

#### Key Compilation Features

1. **Incremental Compilation**: Only recompiles changed files based on dependency tracking
2. **Parallel Compilation**: Automatically detects CPU cores and compiles multiple files in parallel
3. **Dependency Tracking**: Uses `.d` files for precise dependency management
4. **Cross-platform**: Works on macOS, Linux, and other Unix-like systems
5. **Error Reporting**: Provides detailed error messages with clickable file links in VS Code

#### Compiled Modules

The script compiles the following Lambda project components:

- **Input Parsers**: All files in `lambda/input/` (various document format parsers)
- **Formatters**: All files in `lambda/format/` (output format generators)
- **Validators**: All files in `lambda/validator/` (document validation logic)  
- **Core Libraries**: Essential utilities from `lib/` directory
- **Lambda Runtime**: Core Lambda interpreter and transpiler components

#### Build Process

1. **Scanning**: Checks all source files for changes using timestamps and dependency files
2. **Compilation**: Compiles changed C files to WebAssembly object files (`.o`)
3. **Linking**: Links all object files and static libraries into final `.wasm` module
4. **Optimization**: Applies WASM-specific optimizations and exports

The build system supports both full rebuilds and efficient incremental compilation for faster development cycles.

### Testing the Interface
```bash
# Verify setup
./test/verify-wasm-setup.sh

# Test individual components
node -e "require('./lambda-wasm-example.js')"

# Run interactive demo
npm run demo
```

### Debugging

1. **Check WASM module exports:**
   ```javascript
   const info = lambda.getModuleInfo();
   console.log('Available functions:', info.availableFunctions);
   ```

2. **Monitor memory usage:**
   ```javascript
   console.log('Memory pages:', info.memoryPages);
   console.log('Memory size:', lambda.memory.buffer.byteLength);
   ```

3. **Enable verbose logging:**
   ```javascript
   // Add debug logging to WASI imports
   const lambda = new LambdaWASM();
   // Modify createWASIImports() to add console.log statements
   ```

## 🔍 Troubleshooting

### Compilation Issues

**"WASI SDK not found"**
- Ensure WASI SDK is installed and `WASI_SDK_PATH` is set correctly
- Download from: https://github.com/WebAssembly/wasi-sdk/releases

**"Missing dependencies"**
- Make sure all required C source files and libraries are present
- Check that tree-sitter libraries are built properly

**"Compilation errors"**
- Use `--debug` flag for more detailed error information
- Check individual source files for syntax errors

**"Permission issues"**
- Ensure the script is executable (`chmod +x compile-wasm.sh`)
- Check write permissions for build directory

### Runtime Issues

**"WASM module not found"**
- Ensure `lambda.wasm` exists in the same directory
- Check file permissions
- Verify the WASM file was compiled successfully

**"Function not exported"**
- Check available exports with `getModuleInfo()`
- Verify the C functions are properly exported
- Ensure WASM compilation included all necessary source files

**"Memory allocation failed"**
- Increase WASI memory limits
- Check for memory leaks in repeated calls
- Use `freeMemory()` to clean up allocated memory

**"CORS errors in browser"**
- Serve files from a local server, not file:// protocol
- Use `npm run serve` or `npm run serve-node`
- Configure proper CORS headers if serving from a different domain

### WASM Output Compatibility

The compilation produces `lambda.wasm`, a WebAssembly module that can be run with:

1. **Node.js with WASI**: Using `@wasmer/wasi` or similar
2. **Browser with WASI polyfill**: Using WASI browser implementations
3. **Standalone WASI runtime**: Like `wasmtime`, `wasmer`, or `wasm3`

## 📈 Performance Tips

1. **Reuse WASM instance** - Don't reload the module for each operation
2. **Batch operations** - Process multiple inputs in a single call when possible
3. **Memory management** - Free allocated memory after use
4. **Web Workers** - Use workers for heavy processing to avoid blocking the UI
5. **Streaming** - For large files, consider streaming processing

## 🎯 Use Cases

- **Documentation websites** - Real-time markdown rendering
- **Content management** - Format conversion between different document types
- **API services** - Server-side document processing
- **Desktop applications** - Electron apps with document processing
- **Mobile apps** - React Native with WebAssembly support
- **Browser extensions** - Process web content in real-time

## 📄 License

This JavaScript interface is part of the Lambda project and follows the same license terms.
