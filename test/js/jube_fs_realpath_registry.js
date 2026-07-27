const fs = require('fs');
const promises = require('fs/promises');
const path = 'temp/jube-fs-realpath-registry.txt';

fs.writeFileSync(path, 'realpath');
const resolved = fs.realpathSync(path);
console.log(typeof resolved);
console.log(resolved === fs.realpathSync(resolved));
fs.realpath(path, function(error, callbackResolved) {
  console.log(error === null);
  console.log(callbackResolved === resolved);
  promises.realpath(path).then(function(promiseResolved) {
    console.log(promiseResolved === resolved);
    fs.unlinkSync(path);
  });
});
