const fs = require('fs');
const promises = require('fs/promises');
const path = 'temp/jube-fs-access-registry.txt';

fs.writeFileSync(path, 'access');
fs.accessSync(path, 0);
fs.access(path, 0, function(error) {
  console.log(error === null);
  promises.access(path, 0).then(function() {
    console.log('granted');
    fs.unlinkSync(path);
  });
});
