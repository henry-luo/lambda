const fs = require('fs');
const promises = require('fs/promises');
const path = 'temp/jube-fs-truncate-registry.txt';

fs.writeFileSync(path, 'abcdef');
fs.truncateSync(path, 5);
console.log(fs.readFileSync(path, 'utf8'));
fs.truncate(path, 3, function(error) {
  console.log(error === null);
  promises.truncate(path, 1).then(function() {
    console.log(fs.readFileSync(path, 'utf8'));
    fs.unlinkSync(path);
  });
});
