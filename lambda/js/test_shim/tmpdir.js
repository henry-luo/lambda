// Lambda-compatible shim for Node.js test tmpdir module
// Replaces ref/node/test/common/tmpdir.js
'use strict';

const fs = require('fs');
const path = require('path');

const testRoot = process.env.NODE_TEST_DIR ?
  fs.realpathSync(process.env.NODE_TEST_DIR) : path.resolve(__dirname, '..');

const tmpdirName = '.tmp.' +
  (process.env.TEST_SERIAL_ID || process.env.TEST_THREAD_ID || '0');
let tmpPath = path.join(testRoot, tmpdirName);

let firstRefresh = true;

function refresh(useSpawn) {
  try {
    fs.rmSync(tmpPath, { maxRetries: 3, recursive: true, force: true });
  } catch (e) {
    // ignore errors during cleanup
  }
  fs.mkdirSync(tmpPath, { recursive: true });

  if (firstRefresh) {
    firstRefresh = false;
    process.on('exit', function() {
      try {
        fs.rmSync(tmpPath, { maxRetries: 3, recursive: true, force: true });
      } catch (e) {
        // ignore cleanup errors on exit
      }
    });
  }
}

function resolve() {
  const args = [tmpPath];
  for (let i = 0; i < arguments.length; i++) args.push(arguments[i]);
  return path.resolve.apply(path, args);
}

function hasEnoughSpace(size) {
  // Always return true — Lambda doesn't have statfsSync
  return true;
}

function fileURL() {
  const { pathToFileURL } = require('url');
  const fullPath = path.resolve.apply(path, [tmpPath + path.sep].concat(Array.from(arguments)));
  return pathToFileURL(fullPath);
}

module.exports = {
  fileURL: fileURL,
  get path() { return tmpPath; },
  set path(val) { tmpPath = val; },
  refresh: refresh,
  resolve: resolve,
  hasEnoughSpace: hasEnoughSpace,
};
