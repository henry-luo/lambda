// Lambda-compatible shim for Node.js test fixtures module
// Replaces ref/node/test/common/fixtures.js
'use strict';

const path = require('path');
const fs = require('fs');

const fixturesDir = path.join(__dirname, '..', 'fixtures');

function fixturesPath() {
  const args = [fixturesDir];
  for (let i = 0; i < arguments.length; i++) args.push(arguments[i]);
  return path.join.apply(path, args);
}

function fixturesFileURL() {
  const { pathToFileURL } = require('url');
  return pathToFileURL(fixturesPath.apply(this, arguments));
}

function readFixtureSync(args, enc) {
  if (Array.isArray(args))
    return fs.readFileSync(fixturesPath.apply(this, args), enc);
  return fs.readFileSync(fixturesPath(args), enc);
}

function readFixtureKey(name, enc) {
  return fs.readFileSync(fixturesPath('keys', name), enc);
}

function readFixtureKeys(enc) {
  const names = Array.prototype.slice.call(arguments, 1);
  return names.map(function(name) { return readFixtureKey(name, enc); });
}

const utf8TestText = '\u6C38\u548C\u4E5D\u5E74\uFF0C\u6B72\u5728\u7678\u4E11\uFF0C\u66AE\u6625\u4E4B\u521D\uFF0C\u6703\u65BC\u6703\u7A3D\u5C71\u9670\u4E4B\u862D\u4EAD\uFF0C\u4FEE\u7985\u4E8B\u4E5F\u3002';

module.exports = {
  fixturesDir: fixturesDir,
  path: fixturesPath,
  fileURL: fixturesFileURL,
  readSync: readFixtureSync,
  readKey: readFixtureKey,
  readKeys: readFixtureKeys,
  utf8TestText: utf8TestText,
  get utf8TestTextPath() {
    return fixturesPath('utf8_test_text.txt');
  },
};
