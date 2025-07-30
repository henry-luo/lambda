#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

async function testLambdaWasm() {
    console.log('🚀 Testing Lambda WASM in Node.js...\n');
    
    try {
        // Read the WASM file
        const wasmPath = path.join(__dirname, 'lambda.wasm');
        const wasmBuffer = fs.readFileSync(wasmPath);
        
        console.log(`📁 WASM file size: ${wasmBuffer.length} bytes`);
        
        // Create WASI imports
        const wasiImports = {
            wasi_snapshot_preview1: {
                args_get: (argv, argv_buf) => 0,
                args_sizes_get: (argc, argv_buf_size) => 0,
                environ_get: (environ, environ_buf) => 0,
                environ_sizes_get: (environc, environ_buf_size) => 0,
                clock_res_get: (id, resolution) => 0,
                clock_time_get: (id, precision, time) => 0,
                fd_advise: (fd, offset, len, advice) => 0,
                fd_allocate: (fd, offset, len) => 0,
                fd_close: (fd) => 0,
                fd_datasync: (fd) => 0,
                fd_fdstat_get: (fd, stat) => 0,
                fd_fdstat_set_flags: (fd, flags) => 0,
                fd_fdstat_set_rights: (fd, fs_rights_base, fs_rights_inheriting) => 0,
                fd_filestat_get: (fd, filestat) => 0,
                fd_filestat_set_size: (fd, size) => 0,
                fd_filestat_set_times: (fd, atim, mtim, fst_flags) => 0,
                fd_pread: (fd, iovs, iovs_len, offset, nread) => 0,
                fd_prestat_get: (fd, prestat) => 0,
                fd_prestat_dir_name: (fd, path, path_len) => 0,
                fd_pwrite: (fd, iovs, iovs_len, offset, nwritten) => 0,
                fd_read: (fd, iovs, iovs_len, nread) => 0,
                fd_readdir: (fd, buf, buf_len, cookie, bufused) => 0,
                fd_renumber: (fd, to) => 0,
                fd_seek: (fd, offset, whence, newoffset) => 0,
                fd_sync: (fd) => 0,
                fd_tell: (fd, offset) => 0,
                fd_write: (fd, iovs, iovs_len, nwritten) => {
                    console.log('WASI fd_write called');
                    return 0;
                },
                path_create_directory: (fd, path, path_len) => 0,
                path_filestat_get: (fd, flags, path, path_len, filestat) => 0,
                path_filestat_set_times: (fd, flags, path, path_len, atim, mtim, fst_flags) => 0,
                path_link: (old_fd, old_flags, old_path, old_path_len, new_fd, new_path, new_path_len) => 0,
                path_open: (fd, dirflags, path, path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, opened_fd) => 0,
                path_readlink: (fd, path, path_len, buf, buf_len, bufused) => 0,
                path_remove_directory: (fd, path, path_len) => 0,
                path_rename: (fd, old_path, old_path_len, new_fd, new_path, new_path_len) => 0,
                path_symlink: (old_path, old_path_len, fd, new_path, new_path_len) => 0,
                path_unlink_file: (fd, path, path_len) => 0,
                poll_oneoff: (in_, out, nsubscriptions, nevents) => 0,
                proc_exit: (code) => {
                    console.log(`WASI proc_exit called with code: ${code}`);
                },
                proc_raise: (sig) => 0,
                sched_yield: () => 0,
                random_get: (buf, buf_len) => 0,
                sock_recv: (fd, ri_data, ri_data_len, ri_flags, ro_datalen, ro_flags) => 0,
                sock_send: (fd, si_data, si_data_len, si_flags, so_datalen) => 0,
                sock_shutdown: (fd, how) => 0
            },
            env: {
                // Stub functions that our WASM needs
                pool_get_str: () => 0,
                pool_get_int: () => 0,
                pool_get_double: () => 0,
                pool_add_size: () => 0,
                pool_ensure_capacity: () => 0,
                pool_add_ptr: () => 0
            }
        };
        
        // Instantiate the WASM module
        const wasmModule = await WebAssembly.instantiate(wasmBuffer, wasiImports);
        const { exports } = wasmModule.instance;
        
        console.log('✅ WASM module instantiated successfully');
        console.log(`📋 Available exports: ${Object.keys(exports).slice(0, 15).join(', ')}${Object.keys(exports).length > 15 ? '...' : ''}\n`);
        
        // Helper functions for memory management
        function writeStringToMemory(str) {
            const ptr = exports.malloc(str.length + 1);
            const memoryView = new Uint8Array(exports.memory.buffer);
            for (let i = 0; i < str.length; i++) {
                memoryView[ptr + i] = str.charCodeAt(i);
            }
            memoryView[ptr + str.length] = 0; // null terminator
            return ptr;
        }
        
        function readStringFromMemory(ptr) {
            if (!ptr) return '';
            const memoryView = new Uint8Array(exports.memory.buffer);
            let str = '';
            let i = ptr;
            while (memoryView[i] !== 0) {
                str += String.fromCharCode(memoryView[i]);
                i++;
            }
            return str;
        }
        
        // Test 1: Get version
        console.log('🔍 Testing wasm_lambda_version...');
        try {
            const versionPtr = exports.wasm_lambda_version();
            const version = readStringFromMemory(versionPtr);
            console.log(`✅ Version: ${version}\n`);
        } catch (error) {
            console.log(`❌ Failed to get version: ${error.message}\n`);
        }
        
        // Test 2: Initialize runtime
        console.log('🔍 Testing wasm_lambda_init...');
        try {
            const initResult = exports.wasm_lambda_init();
            if (initResult) {
                console.log('✅ Runtime initialized successfully\n');
            } else {
                console.log('❌ Runtime initialization failed\n');
            }
        } catch (error) {
            console.log(`❌ Runtime init error: ${error.message}\n`);
        }
        
        // Test 3: Create runtime instance
        console.log('🔍 Testing wasm_lambda_runtime_new...');
        try {
            const runtimePtr = exports.wasm_lambda_runtime_new();
            if (runtimePtr) {
                console.log('✅ Created new runtime instance');
                
                // Test 4: Process string
                console.log('🔍 Testing wasm_lambda_process_string...');
                try {
                    const testInput = "Hello from Node.js!";
                    const inputPtr = writeStringToMemory(testInput);
                    const outputPtr = exports.malloc(256);
                    
                    const resultLen = exports.wasm_lambda_process_string(inputPtr, outputPtr, 256);
                    if (resultLen > 0) {
                        const outputStr = readStringFromMemory(outputPtr);
                        console.log(`✅ String processing: "${outputStr}"`);
                    } else {
                        console.log('❌ String processing failed');
                    }
                    
                    exports.free(inputPtr);
                    exports.free(outputPtr);
                } catch (error) {
                    console.log(`❌ String processing error: ${error.message}`);
                }
                
                // Test 5: Run code
                console.log('🔍 Testing wasm_lambda_run_code...');
                try {
                    const sourceCode = "1 + 2 * 3";
                    const sourcePtr = writeStringToMemory(sourceCode);
                    const resultPtr = exports.wasm_lambda_run_code(runtimePtr, sourcePtr);
                    
                    if (resultPtr) {
                        const resultStrPtr = exports.wasm_lambda_item_to_string(resultPtr);
                        const resultValue = readStringFromMemory(resultStrPtr);
                        console.log(`✅ Code execution: "${sourceCode}" → "${resultValue}"`);
                    } else {
                        console.log(`❌ Code execution failed for: "${sourceCode}"`);
                    }
                    
                    exports.free(sourcePtr);
                } catch (error) {
                    console.log(`❌ Code execution error: ${error.message}`);
                }
                
                // Cleanup
                exports.wasm_lambda_runtime_free(runtimePtr);
                console.log('✅ Runtime cleanup completed');
            } else {
                console.log('❌ Failed to create runtime instance');
            }
        } catch (error) {
            console.log(`❌ Runtime creation error: ${error.message}`);
        }
        
        console.log('\n🎉 All tests completed!');
        
    } catch (error) {
        console.error('❌ Test failed:', error.message);
        console.error(error.stack);
    }
}

// Run the tests
testLambdaWasm().catch(console.error);
