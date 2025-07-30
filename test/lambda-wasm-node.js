#!/usr/bin/env node

/**
 * Node.js Example for Lambda WASM Module
 * 
 * This script demonstrates how to use the Lambda WASM module in a Node.js environment.
 * It can be used as a command-line tool or imported as a module.
 */

const fs = require('fs');
const path = require('path');
const { LambdaWASM } = require('./lambda-wasm-example.js');

class LambdaWASMCLI {
    constructor() {
        this.lambda = new LambdaWASM();
    }

    /**
     * Initialize the WASM module
     */
    async init(wasmPath = './lambda.wasm') {
        try {
            console.log(`🔄 Loading Lambda WASM module from: ${wasmPath}`);
            await this.lambda.loadModule(wasmPath);
            console.log('✅ Lambda WASM module loaded successfully!');
            
            const info = this.lambda.getModuleInfo();
            console.log(`📊 Module Info: ${info.exports.length} exports, ${info.availableFunctions.length} functions`);
            
            return true;
        } catch (error) {
            console.error('❌ Failed to load WASM module:', error.message);
            return false;
        }
    }

    /**
     * Process a file through the Lambda WASM module
     */
    async processFile(inputPath, outputPath, inputFormat = 'auto', outputFormat = 'html') {
        try {
            // Read input file
            const inputText = fs.readFileSync(inputPath, 'utf8');
            console.log(`📖 Read ${inputText.length} characters from ${inputPath}`);

            // Auto-detect format if needed
            if (inputFormat === 'auto') {
                inputFormat = this.detectFormat(inputPath);
                console.log(`🔍 Auto-detected format: ${inputFormat}`);
            }

            // Process the text
            console.log(`🔄 Processing ${inputFormat} → ${outputFormat}...`);
            
            const parsed = await this.lambda.parseInput(inputText, inputFormat);
            const formatted = await this.lambda.formatOutput(parsed, outputFormat);

            // Write output file
            if (outputPath) {
                fs.writeFileSync(outputPath, formatted, 'utf8');
                console.log(`✅ Output written to ${outputPath}`);
            } else {
                console.log('\n📄 Processed Output:');
                console.log('─'.repeat(50));
                console.log(formatted);
                console.log('─'.repeat(50));
            }

            return formatted;

        } catch (error) {
            console.error('❌ Processing failed:', error.message);
            
            // Show mock processing for demo
            console.log('\n🎭 Mock Processing (WASM functions not available):');
            console.log(`Input: ${inputPath} (${inputFormat})`);
            console.log(`Output: ${outputPath || 'stdout'} (${outputFormat})`);
            
            return null;
        }
    }

    /**
     * Detect file format based on extension
     */
    detectFormat(filePath) {
        const ext = path.extname(filePath).toLowerCase();
        const formatMap = {
            '.md': 'markdown',
            '.markdown': 'markdown',
            '.html': 'html',
            '.htm': 'html',
            '.json': 'json',
            '.xml': 'xml',
            '.txt': 'text',
            '.rst': 'rst',
            '.tex': 'latex',
            '.csv': 'csv',
            '.toml': 'toml',
            '.yaml': 'yaml',
            '.yml': 'yaml',
            '.ini': 'ini'
        };
        
        return formatMap[ext] || 'text';
    }

    /**
     * Interactive mode
     */
    async interactive() {
        const readline = require('readline');
        const rl = readline.createInterface({
            input: process.stdin,
            output: process.stdout
        });

        console.log('\n🎮 Interactive Lambda WASM Mode');
        console.log('Type "exit" to quit, "help" for commands\n');

        const question = (prompt) => new Promise(resolve => rl.question(prompt, resolve));

        try {
            while (true) {
                const input = await question('λ> ');
                
                if (input.trim() === 'exit') {
                    console.log('👋 Goodbye!');
                    break;
                }
                
                if (input.trim() === 'help') {
                    console.log(`
Available commands:
- parse <format> <text>     Parse text in given format
- format <format> <text>    Format text to given format  
- info                      Show module information
- exit                      Exit interactive mode
- help                      Show this help

Example: parse markdown "# Hello World"
`);
                    continue;
                }

                if (input.trim() === 'info') {
                    const info = this.lambda.getModuleInfo();
                    console.log('📊 Module Information:');
                    console.log(`   Initialized: ${info.initialized}`);
                    console.log(`   Memory Pages: ${info.memoryPages}`);
                    console.log(`   Exports: ${info.exports.length}`);
                    console.log(`   Functions: ${info.availableFunctions.join(', ')}`);
                    continue;
                }

                // Parse commands
                const parts = input.trim().split(' ');
                const command = parts[0];
                const format = parts[1];
                const text = parts.slice(2).join(' ');

                try {
                    if (command === 'parse' && format && text) {
                        const result = await this.lambda.parseInput(text, format);
                        console.log('📄 Result:', result);
                    } else if (command === 'format' && format && text) {
                        const result = await this.lambda.formatOutput(text, format);
                        console.log('📄 Result:', result);
                    } else {
                        console.log('❓ Unknown command. Type "help" for available commands.');
                    }
                } catch (error) {
                    console.log('❌ Error:', error.message);
                }
            }
        } finally {
            rl.close();
        }
    }

    /**
     * Show usage information
     */
    showUsage() {
        console.log(`
🚀 Lambda WASM CLI Tool

Usage:
  node lambda-wasm-node.js [options] <command>
  
Commands:
  process <input> [output]     Process a file
  interactive                  Start interactive mode
  info                        Show module information
  
Options:
  --wasm <path>               Path to WASM file (default: ./lambda.wasm)
  --input-format <format>     Input format (auto, markdown, html, json, etc.)
  --output-format <format>    Output format (html, json, xml, etc.)
  --help                      Show this help
  
Examples:
  node lambda-wasm-node.js process input.md output.html
  node lambda-wasm-node.js --input-format markdown process doc.txt
  node lambda-wasm-node.js interactive
  
Supported Formats:
  Input:  markdown, html, json, xml, text, rst, latex, csv, toml, yaml, ini
  Output: html, json, xml, markdown, text
`);
    }
}

// Main execution
async function main() {
    const args = process.argv.slice(2);
    const cli = new LambdaWASMCLI();
    
    // Parse arguments
    let wasmPath = './lambda.wasm';
    let inputFormat = 'auto';
    let outputFormat = 'html';
    let command = '';
    let inputFile = '';
    let outputFile = '';
    
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        
        switch (arg) {
            case '--wasm':
                wasmPath = args[++i];
                break;
            case '--input-format':
                inputFormat = args[++i];
                break;
            case '--output-format':
                outputFormat = args[++i];
                break;
            case '--help':
                cli.showUsage();
                return;
            case 'process':
                command = 'process';
                inputFile = args[++i];
                outputFile = args[i + 1]; // Optional
                if (outputFile && !outputFile.startsWith('--')) {
                    i++; // Consume output file argument  
                } else {
                    outputFile = ''; // No output file specified
                }
                break;
            case 'interactive':
                command = 'interactive';
                break;
            case 'info':
                command = 'info';
                break;
            default:
                if (!command) {
                    console.error(`❌ Unknown argument: ${arg}`);
                    cli.showUsage();
                    return;
                }
        }
    }
    
    // Check if WASM file exists
    if (!fs.existsSync(wasmPath)) {
        console.error(`❌ WASM file not found: ${wasmPath}`);
        console.log('💡 Compile the Lambda project to WASM first using: ./compile-wasm.sh');
        return;
    }
    
    // Initialize WASM module
    const initialized = await cli.init(wasmPath);
    
    // Execute command
    switch (command) {
        case 'process':
            if (!inputFile) {
                console.error('❌ Input file required for process command');
                cli.showUsage();
                return;
            }
            
            if (!fs.existsSync(inputFile)) {
                console.error(`❌ Input file not found: ${inputFile}`);
                return;
            }
            
            await cli.processFile(inputFile, outputFile, inputFormat, outputFormat);
            break;
            
        case 'interactive':
            if (initialized) {
                await cli.interactive();
            }
            break;
            
        case 'info':
            if (initialized) {
                const info = cli.lambda.getModuleInfo();
                console.log('\n📊 Detailed Module Information:');
                console.log(JSON.stringify(info, null, 2));
            }
            break;
            
        default:
            if (args.length === 0) {
                cli.showUsage();
            } else {
                console.error('❌ No command specified');
                cli.showUsage();
            }
    }
}

// Run if executed directly
if (require.main === module) {
    main().catch(error => {
        console.error('💥 Fatal error:', error);
        process.exit(1);
    });
}

// Export for use as module
module.exports = { LambdaWASMCLI };
