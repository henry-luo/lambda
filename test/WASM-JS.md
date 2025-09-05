# Lambda WASM JavaScript Interface

This comprehensive guide covers the complete WebAssembly (WASM) build system and JavaScript interface for the Lambda project. It includes scripts, configurations, and JavaScript examples for compiling Lambda C files to WebAssembly using WASI SDK and interfacing with the resulting WASM module in both browser and Node.js environments.

## 📋 Files Overview

### Core Build System
- **`compile-wasm.sh`** - Complete WASM compilation script with JSON configuration
- **`build_lambda_wasm_config.json`** - Configuration file for WASM build
- **`wasm-deps/include/`** - Comprehensive stub libraries for WASM compatibility

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
# Basic compilation (linking only - recommended)
./compile-wasm.sh --linking-only

# Full compilation from source
./compile-wasm.sh

# Debug build with symbols
./compile-wasm.sh --debug

# Force full rebuild
./compile-wasm.sh --force

# Use custom configuration
./compile-wasm.sh custom_config.json

# Parallel build with 4 jobs
./compile-wasm.sh --jobs=4
```

**Available compilation options:**
- `config_file` - JSON configuration file (default: build_lambda_wasm_config.json)
- `--debug, -d` - Build debug version with debug symbols
- `--force, -f` - Force rebuild all files (disable incremental compilation)
- `--jobs=N, -j N` - Number of parallel compilation jobs (default: auto-detect)
- `--linking-only, -l` - Only perform linking step (requires existing object files)
- `--clean-deps` - Clean dependency files (.d files) and exit
- `--help, -h` - Show help information

## 🔧 WASM Build System

The Lambda project includes a complete WebAssembly (WASM) build system that compiles the Lambda C codebase into a single WASM module. This enables running Lambda's input parsers, output formatters, and validators directly in web browsers and Node.js environments.

### Build Scripts

#### 1. `compile-wasm.sh` - **Complete WASM Build System**

A comprehensive compilation script with JSON configuration support:

```bash
./compile-wasm.sh [config_file] [--debug] [--force] [--jobs=N] [--linking-only]
```

**Features:**
- JSON-based configuration with `build_lambda_wasm_config.json`
- Full compilation from source or linking-only mode
- Incremental builds with dependency tracking
- Parallel compilation support
- Automatic testing with Node.js
- Clear success/failure reporting
- Comprehensive command-line options

### Build Configuration

The build system uses `build_lambda_wasm_config.json` for configuration:

```json
{
    "output": "lambda.wasm",
    "source_dirs": ["lambda/input", "lambda/format", "lambda/validator"],
    "source_files": ["lambda/lambda-wasm-main.c", ...],
    "libraries": [
        {"name": "wasm-stubs", "include": "wasm-deps/include"},
        ...
    ],
    "flags": ["fms-extensions", "O2", "DWASM_BUILD=1", ...],
    "linker_flags": ["L/opt/wasi-sdk/share/wasi-sysroot/lib/wasm32-wasi", ...]
}
```

### Dependencies

#### WASI SDK
- **Required**: WASI SDK v24.0 or later
- **Installation**: Download from [WebAssembly/wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases)
- **Default path**: `/opt/wasi-sdk`
- **Environment variable**: `WASI_SDK_PATH`

#### Stub Libraries
The build system includes comprehensive stub libraries in `wasm-deps/include/`:

- **GMP**: Mathematical functions (`gmp.h`)
- **MIR**: JIT compiler stubs (`mir.h`, `mir-gen.h`, `c2mir.h`)
- **Lexbor**: HTML parsing (`lexbor/url/url.h`)
- **zlog**: Logging functions (`zlog.h`)
- **Lambda compatibility**: Internal functions (`lambda-compat.h`)
- **System compatibility**: POSIX/system functions (`wasm-compat.h`)

### Generated Output

#### `lambda.wasm`
- **Size**: ~294KB optimized
- **Target**: `wasm32-wasi`
- **Exports**: 7 main functions + memory management

#### Exported Functions

| Function | Description |
|----------|-------------|
| `wasm_lambda_version()` | Returns version string |
| `wasm_lambda_init()` | Initialize Lambda runtime |
| `wasm_lambda_process_string()` | Simple string processing |
| `wasm_lambda_runtime_new()` | Create runtime instance |
| `wasm_lambda_runtime_free()` | Cleanup runtime instance |
| `wasm_lambda_run_code()` | Execute Lambda code |
| `wasm_lambda_item_to_string()` | Convert results to string |

### Subsystems Included

#### Input Parsers
- **Formats**: Markdown, HTML, JSON, YAML, XML, CSV, TOML, INI, LaTeX, ReStructuredText, Textile, RTF, Email, PDF, ICS, VCF, CSS, AsciiDoc, Man pages, Wiki markup
- **Files**: All `lambda/input/*.c` files compiled

#### Output Formatters  
- **Formats**: HTML, Markdown, JSON, YAML, XML, TOML, INI, ReStructuredText
- **Files**: All `lambda/format/*.c` files compiled

#### Validators
- **Schema validation**: JSON Schema, custom validation rules
- **Error reporting**: Detailed validation error messages
- **Files**: All `lambda/validator/*.c` files compiled

### Build Process Details

#### Compilation Flags
```bash
--target=wasm32-wasi -O2 -DWASM_BUILD=1 -DCROSS_COMPILE=1 -D_POSIX_C_SOURCE=200809L -fms-extensions
```

#### Include Paths
```bash
-I. -Iinclude -Ilib -Ilambda -Iwasm-deps/include
```

#### Linker Configuration
```bash
-L/opt/wasi-sdk/share/wasi-sysroot/lib/wasm32-wasi
-lwasi-emulated-signal -lwasi-emulated-process-clocks 
-lwasi-emulated-getpid -lwasi-emulated-mman
```

#### Exports Configuration
```bash
-Wl,--export=wasm_lambda_version,--export=wasm_lambda_init,
--export=wasm_lambda_process_string,--export=wasm_lambda_runtime_new,
--export=wasm_lambda_runtime_free,--export=wasm_lambda_run_code,
--export=wasm_lambda_item_to_string,--allow-undefined
```

### Performance

- **Compilation time**: ~30 seconds (full), ~2 seconds (linking only)
- **Runtime**: Near-native performance with WASM
- **Memory usage**: Configurable, starts at ~1MB
- **File size**: 294KB compressed, suitable for web deployment

### Build System Troubleshooting

#### Common Issues

1. **WASI SDK not found**
   ```bash
   export WASI_SDK_PATH=/path/to/wasi-sdk
   ```

2. **Object files missing**
   ```bash
   ./compile-wasm.sh --force  # Full rebuild
   ```

3. **Link errors**
   ```bash
   ./compile-wasm-simple.sh  # Use known-good linking
   ```

4. **Function signature mismatches**
   - Expected warnings from stubs
   - WASM module still functional

#### Debug Mode
```bash
./compile-wasm.sh --debug  # Enable debug symbols
bash -x ./compile-wasm.sh  # Shell debugging
```

### Maintenance

#### Updating Stubs
When adding new dependencies, update stub files in `wasm-deps/include/`:

1. Add new header file with stub implementations
2. Update `build_lambda_wasm_config.json` libraries section
3. Test with `./compile-wasm-simple.sh`

#### Adding Source Files
1. Add to `source_files` or `source_dirs` in config
2. Ensure no tree-sitter/MIR dependencies
3. Test compilation and linking

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

### WASM Integration Examples

#### Node.js Basic Usage
```javascript
const fs = require('fs');
const wasmBuffer = fs.readFileSync('lambda.wasm');
const wasmModule = await WebAssembly.instantiate(wasmBuffer);

// Initialize Lambda
const initResult = wasmModule.instance.exports.wasm_lambda_init();
console.log('Lambda initialized:', initResult);

// Get version
const versionPtr = wasmModule.instance.exports.wasm_lambda_version();
const version = getString(wasmModule, versionPtr);
console.log('Version:', version);
```

#### Browser Basic Usage
```javascript
fetch('lambda.wasm')
  .then(response => response.arrayBuffer())
  .then(bytes => WebAssembly.instantiate(bytes))
  .then(results => {
    const exports = results.instance.exports;
    
    // Initialize and use Lambda functions
    exports.wasm_lambda_init();
    const version = getString(exports.wasm_lambda_version());
    console.log('Lambda version:', version);
  });
```

### WASM Testing

#### Node.js Testing
```bash
node test/lambda-wasm-node.js
```

#### Browser Testing
```html
<!DOCTYPE html>
<html>
<body>
    <script src="test-wasm-simple.html"></script>
</body>
</html>
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

Lambda provides two build scripts for different use cases:

**For most users (linking only):**
```bash
./compile-wasm.sh --linking-only  # Quick linking with existing objects
```

**For development (full build):**
```bash
./compile-wasm.sh [config_file] [--debug] [--force] [--jobs=N]  # Full compilation system
```

See the **🔧 WASM Build System** section above for complete details on:
- Build script features and options
- Configuration with `build_lambda_wasm_config.json`
- Stub libraries and dependencies
- Exported functions and subsystems
- Performance characteristics
- Troubleshooting guide

#### Environment Variables
- `WASI_SDK_PATH`: Path to WASI SDK installation (default: `/opt/wasi-sdk`)

#### Quick Build Summary

The build system compiles the following Lambda components into a single WASM module:
- **Input Parsers**: All document format parsers from `lambda/input/`
- **Formatters**: All output generators from `lambda/format/`
- **Validators**: All validation logic from `lambda/validator/`
- **Core Libraries**: Essential utilities and runtime components

Output: `lambda.wasm` (~294KB optimized) with 7 exported functions ready for browser and Node.js use.

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

### Build System Issues

For comprehensive build troubleshooting, see the **Build System Troubleshooting** section in the **🔧 WASM Build System** above, which covers:
- WASI SDK installation and path issues
- Object file and dependency problems
- Linking errors and solutions
- Debug mode and shell debugging
- Stub library maintenance

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

This JavaScript interface and WASM build system are part of the Lambda project and follow the same license terms.

---

*This document combines the WASM build system documentation (formerly WASM-BUILD.md) with the JavaScript interface guide to provide a complete reference for Lambda WebAssembly development.*
