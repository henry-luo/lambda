#!/usr/bin/env node
// The shared runner launches Node scripts; the checker writes its report directly.
const path = require('path');
const { spawnSync } = require('child_process');
const root = path.resolve(__dirname, '..');
const result = spawnSync('python3', [path.join(__dirname, 'script_format_roundtrip.py'),
    ...process.argv.slice(2)], { cwd: root, stdio: 'inherit' });
if (result.error) console.error(result.error.message);
process.exit(result.status === null ? 1 : result.status);
