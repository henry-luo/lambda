'use strict';

const fs = require('fs');
const path = require('path');
const { createHash } = require('crypto');

// a relink must not replace the executable between GTest discovery and its workers.
function snapshotExecutable(source, destination) {
    const before = fs.statSync(source);
    fs.copyFileSync(source, destination, fs.constants.COPYFILE_FICLONE);
    const digest = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    const sha256 = digest(destination);
    const after = fs.statSync(source);
    if (before.ino !== after.ino || before.size !== after.size || before.mtimeMs !== after.mtimeMs ||
        sha256 !== digest(source)) {
        throw new Error(`executable changed while taking snapshot: ${source}`);
    }
    return { source, path: destination, sha256 };
}

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

module.exports = { createWorkspace, snapshotExecutable };
