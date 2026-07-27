const fs = require('fs');
const promises = require('fs/promises');
const target = 'jube-fs-symlink-source.txt';
const source = 'temp/' + target;
const syncLink = 'temp/jube-fs-symlink-sync.txt';
const callbackLink = 'temp/jube-fs-symlink-callback.txt';
const promiseLink = 'temp/jube-fs-symlink-promise.txt';

fs.writeFileSync(source, 'symlink');
fs.symlinkSync(target, syncLink);
console.log(fs.lstatSync(syncLink).isSymbolicLink());
console.log(fs.readFileSync(syncLink, 'utf8'));
fs.symlink(target, callbackLink, function(error) {
  console.log(error === null);
  promises.symlink(target, promiseLink).then(function() {
    console.log(fs.lstatSync(promiseLink).isSymbolicLink());
    fs.unlinkSync(source);
    fs.unlinkSync(syncLink);
    fs.unlinkSync(callbackLink);
    fs.unlinkSync(promiseLink);
  });
});
