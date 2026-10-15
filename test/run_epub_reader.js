#!/usr/bin/env node
// Adapt the persistence workflow to the shared Node test runner.
const path = require('path');
const { spawnSync } = require('child_process');
const result = spawnSync('python3', [path.join(__dirname, 'epub_reader_check.py'),
    ...process.argv.slice(2)], { cwd: path.resolve(__dirname, '..'), stdio: 'inherit' });
if (result.error) console.error(result.error.message);
process.exit(result.status === null ? 1 : result.status);
