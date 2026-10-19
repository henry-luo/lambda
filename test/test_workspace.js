'use strict';

const fs = require('fs');
const path = require('path');

// legacy harnesses write fixed relative paths; isolate scratch without copying corpora.
function createWorkspace(root, prefix) {
    fs.mkdirSync(path.join(root, 'temp'), { recursive: true });
    const workspace = fs.mkdtempSync(path.join(root, 'temp', prefix));
    fs.mkdirSync(path.join(workspace, 'temp'));
    for (const name of ['test', 'lmd', 'ref', 'modules', 'assets', 'utils', 'log.conf', 'build_lambda_config.json', 'lambda.exe']) {
        const source = path.resolve(root, name === 'lambda.exe'
            ? process.env.RADIANT_TEST_EXECUTABLE || name : name);
        if (!fs.existsSync(source)) continue;
        const destination = path.join(workspace, name);
        if (fs.statSync(source).isDirectory()) fs.symlinkSync(source, destination, 'junction');
        // file symlinks require extra privileges on Windows.
        else if (process.platform === 'win32') fs.copyFileSync(source, destination);
        else fs.symlinkSync(source, destination, 'file');
    }
    return workspace;
}

module.exports = { createWorkspace };
