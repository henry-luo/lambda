const fs = require('fs');

fs.readFile('test/node/path_basic.js', (err, data) => {
  console.log(err === null);
  console.log(data.length > 0);
  fs.readFile('temp/jube-fs-missing-file', (missingErr, missingData) => {
    console.log(missingErr !== null);
    console.log(missingData === null);
  });
});
