/**
 * Lambda WASM Module Loader and Interface
 * 
 * This module provides a JavaScript interface to the Lambda WASM module
 * for parsing and processing various document formats.
 */

class LambdaWASM {
    constructor() {
        this.module = null;
        this.memory = null;
        this.exports = null;
        this.isInitialized = false;
    }

    /**
     * Load the WASM module from a file or URL
     * @param {string|ArrayBuffer} wasmSource - Path to WASM file or ArrayBuffer
     * @returns {Promise<void>}
     */
    async loadModule(wasmSource) {
        try {
            let wasmBytes;
            
            if (typeof wasmSource === 'string') {
                // Node.js environment
                if (typeof require !== 'undefined') {
                    const fs = require('fs');
                    wasmBytes = fs.readFileSync(wasmSource);
                }
                // Browser environment
                else {
                    const response = await fetch(wasmSource);
                    wasmBytes = await response.arrayBuffer();
                }
            } else {
                wasmBytes = wasmSource;
            }

            // WASI imports for the module
            const wasiImports = this.createWASIImports();
            
            // Instantiate the WASM module
            const wasmModule = await WebAssembly.instantiate(wasmBytes, {
                wasi_snapshot_preview1: wasiImports,
                env: {
                    // Additional environment imports if needed
                    memory: new WebAssembly.Memory({ initial: 256, maximum: 512 })
                }
            });

            this.module = wasmModule.instance;
            this.exports = wasmModule.instance.exports;
            this.memory = this.exports.memory || wasiImports.memory;
            this.isInitialized = true;

            console.log('Lambda WASM module loaded successfully');
            console.log('Available exports:', Object.keys(this.exports));
            
        } catch (error) {
            console.error('Failed to load Lambda WASM module:', error);
            throw error;
        }
    }

    /**
     * Create minimal WASI imports required by the WASM module
     * @returns {Object} WASI import object
     */
    createWASIImports() {
        return {
            // File descriptor operations
            fd_write: (fd, iovs, iovs_len, nwritten) => {
                // Simple stdout/stderr implementation
                if (fd === 1 || fd === 2) { // stdout or stderr
                    let text = '';
                    const view = new DataView(this.memory.buffer);
                    
                    for (let i = 0; i < iovs_len; i++) {
                        const ptr = view.getUint32(iovs + i * 8, true);
                        const len = view.getUint32(iovs + i * 8 + 4, true);
                        const bytes = new Uint8Array(this.memory.buffer, ptr, len);
                        text += new TextDecoder().decode(bytes);
                    }
                    
                    if (fd === 1) console.log(text);
                    else console.error(text);
                    
                    // Write the number of bytes written
                    view.setUint32(nwritten, text.length, true);
                    return 0; // Success
                }
                return 8; // EBADF
            },

            fd_read: () => 52, // ENOSYS - not implemented
            fd_seek: () => 52, // ENOSYS - not implemented
            fd_close: () => 0, // Success - no-op
            
            // Process operations
            proc_exit: (code) => {
                console.log(`WASM process exited with code: ${code}`);
            },

            // Environment operations
            environ_sizes_get: (environ_count, environ_buf_size) => {
                const view = new DataView(this.memory.buffer);
                view.setUint32(environ_count, 0, true);
                view.setUint32(environ_buf_size, 0, true);
                return 0;
            },

            environ_get: () => 0, // No environment variables

            // Clock operations
            clock_time_get: (id, precision, time) => {
                const now = BigInt(Date.now()) * 1000000n; // Convert to nanoseconds
                const view = new DataView(this.memory.buffer);
                view.setBigUint64(time, now, true);
                return 0;
            },

            // Random number generation
            random_get: (buf, buf_len) => {
                const bytes = new Uint8Array(this.memory.buffer, buf, buf_len);
                crypto.getRandomValues(bytes);
                return 0;
            },

            // Memory allocation (if needed)
            memory: new WebAssembly.Memory({ initial: 256, maximum: 512 })
        };
    }

    /**
     * Allocate memory in the WASM module
     * @param {number} size - Size in bytes
     * @returns {number} Pointer to allocated memory
     */
    allocateMemory(size) {
        if (!this.isInitialized) {
            throw new Error('WASM module not initialized');
        }
        
        // If the module has a malloc export, use it
        if (this.exports.malloc) {
            return this.exports.malloc(size);
        }
        
        // Simple memory allocation fallback
        // This is a basic implementation - real usage might need a proper allocator
        if (!this.memoryOffset) {
            this.memoryOffset = 1024; // Start after first 1KB
        }
        
        const ptr = this.memoryOffset;
        this.memoryOffset += size;
        return ptr;
    }

    /**
     * Free allocated memory
     * @param {number} ptr - Pointer to memory to free
     */
    freeMemory(ptr) {
        if (this.exports.free) {
            this.exports.free(ptr);
        }
        // Otherwise, no-op for simple allocator
    }

    /**
     * Write a string to WASM memory
     * @param {string} str - String to write
     * @returns {number} Pointer to the string in WASM memory
     */
    writeString(str) {
        const encoder = new TextEncoder();
        const bytes = encoder.encode(str + '\0'); // Null-terminated
        const ptr = this.allocateMemory(bytes.length);
        
        const memory = new Uint8Array(this.memory.buffer);
        memory.set(bytes, ptr);
        
        return ptr;
    }

    /**
     * Read a string from WASM memory
     * @param {number} ptr - Pointer to string in WASM memory
     * @param {number} maxLength - Maximum length to read
     * @returns {string} The string
     */
    readString(ptr, maxLength = 1024) {
        const memory = new Uint8Array(this.memory.buffer);
        const bytes = [];
        
        for (let i = 0; i < maxLength; i++) {
            const byte = memory[ptr + i];
            if (byte === 0) break; // Null terminator
            bytes.push(byte);
        }
        
        return new TextDecoder().decode(new Uint8Array(bytes));
    }

    /**
     * Parse input text using the Lambda WASM module
     * @param {string} inputText - Text to parse
     * @param {string} format - Input format (e.g., 'markdown', 'json', 'html')
     * @returns {Promise<string>} Parsed result
     */
    async parseInput(inputText, format = 'markdown') {
        if (!this.isInitialized) {
            throw new Error('WASM module not initialized. Call loadModule() first.');
        }

        try {
            // Write input text to WASM memory
            const inputPtr = this.writeString(inputText);
            const formatPtr = this.writeString(format);
            
            // Allocate memory for output
            const outputSize = Math.max(inputText.length * 2, 4096);
            const outputPtr = this.allocateMemory(outputSize);

            // Call the appropriate parser function
            let result = 0;
            
            // Try different parser function names that might be exported
            const possibleFunctions = [
                'parse_input',
                'lambda_parse',
                'process_input',
                'parse_document',
                'input_parser'
            ];

            for (const funcName of possibleFunctions) {
                if (this.exports[funcName]) {
                    console.log(`Calling WASM function: ${funcName}`);
                    result = this.exports[funcName](inputPtr, formatPtr, outputPtr, outputSize);
                    break;
                }
            }

            if (result === 0) {
                // Success - read the output
                const output = this.readString(outputPtr);
                
                // Clean up memory
                this.freeMemory(inputPtr);
                this.freeMemory(formatPtr);
                this.freeMemory(outputPtr);
                
                return output;
            } else {
                throw new Error(`Parsing failed with code: ${result}`);
            }

        } catch (error) {
            console.error('Error in parseInput:', error);
            throw error;
        }
    }

    /**
     * Format output using the Lambda WASM module
     * @param {string} content - Content to format
     * @param {string} outputFormat - Output format (e.g., 'html', 'json', 'xml')
     * @returns {Promise<string>} Formatted result
     */
    async formatOutput(content, outputFormat = 'html') {
        if (!this.isInitialized) {
            throw new Error('WASM module not initialized. Call loadModule() first.');
        }

        try {
            const contentPtr = this.writeString(content);
            const formatPtr = this.writeString(outputFormat);
            
            const outputSize = Math.max(content.length * 2, 4096);
            const outputPtr = this.allocateMemory(outputSize);

            // Try different formatter function names
            const possibleFunctions = [
                'format_output',
                'lambda_format',
                'process_output',
                'format_document'
            ];

            let result = 0;
            for (const funcName of possibleFunctions) {
                if (this.exports[funcName]) {
                    console.log(`Calling WASM function: ${funcName}`);
                    result = this.exports[funcName](contentPtr, formatPtr, outputPtr, outputSize);
                    break;
                }
            }

            if (result === 0) {
                const output = this.readString(outputPtr);
                
                this.freeMemory(contentPtr);
                this.freeMemory(formatPtr);
                this.freeMemory(outputPtr);
                
                return output;
            } else {
                throw new Error(`Formatting failed with code: ${result}`);
            }

        } catch (error) {
            console.error('Error in formatOutput:', error);
            throw error;
        }
    }

    /**
     * Get information about the loaded WASM module
     * @returns {Object} Module information
     */
    getModuleInfo() {
        if (!this.isInitialized) {
            return { initialized: false };
        }

        return {
            initialized: true,
            memoryPages: this.memory ? this.memory.buffer.byteLength / 65536 : 0,
            exports: Object.keys(this.exports),
            availableFunctions: Object.keys(this.exports).filter(key => 
                typeof this.exports[key] === 'function'
            )
        };
    }
}

// Example usage
async function example() {
    const lambda = new LambdaWASM();
    
    try {
        // Load the WASM module
        await lambda.loadModule('./lambda.wasm');
        
        console.log('Module info:', lambda.getModuleInfo());
        
        // Example: Parse markdown input
        const markdownInput = `
# Hello Lambda WASM

This is a **test** document with:
- Lists
- *Emphasis*
- And more!

## Code Example
\`\`\`javascript
console.log("Hello from WASM!");
\`\`\`
        `;
        
        console.log('Input text:', markdownInput);
        
        // Parse the input
        const parsed = await lambda.parseInput(markdownInput, 'markdown');
        console.log('Parsed result:', parsed);
        
        // Format to HTML
        const htmlOutput = await lambda.formatOutput(parsed, 'html');
        console.log('HTML output:', htmlOutput);
        
    } catch (error) {
        console.error('Example failed:', error);
    }
}

// Export for different environments
if (typeof module !== 'undefined' && module.exports) {
    // Node.js
    module.exports = { LambdaWASM, example };
} else if (typeof window !== 'undefined') {
    // Browser
    window.LambdaWASM = LambdaWASM;
    window.lambdaExample = example;
}

// Auto-run example if this file is executed directly
if (typeof require !== 'undefined' && require.main === module) {
    example().catch(console.error);
}
