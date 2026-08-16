// C0.3 regression: fs errors must name the syscall that actually failed.
// Node reports the underlying call, not the JS API name — readFile/writeFile
// report open, readdir reports scandir, realpath reports lstat.
const assert = require('assert');
const fs = require('fs');

const MISSING = 'temp/fs-error-syscall-missing-file';
const MISSING_DIR = 'temp/fs-error-syscall-missing-dir';

function check(label, err, syscall, code) {
  assert.ok(err, label + ': expected an error');
  assert.strictEqual(err.code, code, label + ': code');
  assert.strictEqual(err.syscall, syscall, label + ': syscall');
}

// sync lane
try {
  fs.readFileSync(MISSING);
  assert.fail('readFileSync should throw');
} catch (e) { check('readFileSync', e, 'open', 'ENOENT'); }

try {
  fs.accessSync(MISSING);
  assert.fail('accessSync should throw');
} catch (e) { check('accessSync', e, 'access', 'ENOENT'); }

// async lane — each callback checks one operation
const pending = [];
function expect(label, syscall, code, run) {
  pending.push(label);
  run((err) => {
    check(label, err, syscall, code);
    pending.splice(pending.indexOf(label), 1);
    if (pending.length === 0) console.log('fs error syscall ok');
  });
}

expect('readFile', 'open', 'ENOENT', (cb) => fs.readFile(MISSING, cb));
expect('access', 'access', 'ENOENT', (cb) => fs.access(MISSING, cb));
expect('stat', 'stat', 'ENOENT', (cb) => fs.stat(MISSING, cb));
expect('lstat', 'lstat', 'ENOENT', (cb) => fs.lstat(MISSING, cb));
expect('open', 'open', 'ENOENT', (cb) => fs.open(MISSING, 'r', cb));
expect('unlink', 'unlink', 'ENOENT', (cb) => fs.unlink(MISSING, cb));
expect('rmdir', 'rmdir', 'ENOENT', (cb) => fs.rmdir(MISSING_DIR, cb));
expect('readdir', 'scandir', 'ENOENT', (cb) => fs.readdir(MISSING_DIR, cb));
expect('realpath', 'lstat', 'ENOENT', (cb) => fs.realpath(MISSING, cb));
expect('readlink', 'readlink', 'ENOENT', (cb) => fs.readlink(MISSING, cb));
expect('fstat', 'fstat', 'EBADF', (cb) => fs.fstat(9999, cb));
