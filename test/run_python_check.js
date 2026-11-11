#!/usr/bin/env node
const path = require('path');
const { spawnSync } = require('child_process');

// Python checks write the same report directly; preserve their exit status.
function run_python_check(script, args = process.argv.slice(2)) {
    const result = spawnSync(process.env.PYTHON || 'python3',
        [path.resolve(__dirname, script), ...args], {
            cwd: path.resolve(__dirname, '..'), stdio: 'inherit',
            env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' },
        });
    if (result.error) console.error(result.error.message);
    process.exit(result.status === null ? 1 : result.status);
}

module.exports = { run_python_check };
