const fs = require('fs');
const promises = require('fs/promises');
const prefix = 'temp/jube-fs-mkdtemp-registry-';

const syncPath = fs.mkdtempSync(prefix);
console.log(typeof syncPath);
fs.rmdirSync(syncPath);
fs.mkdtemp(prefix, function(error, callbackPath) {
  console.log(error === null);
  console.log(typeof callbackPath);
  fs.rmdirSync(callbackPath);
  promises.mkdtemp(prefix).then(function(promisePath) {
    console.log(typeof promisePath);
    fs.rmdirSync(promisePath);
  });
});
