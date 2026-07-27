const fs = require('fs');
const path = 'temp/jube-fs-async-write-work.txt';

fs.writeFile(path, 'jube async write', (writeErr) => {
  console.log(writeErr === null);
  fs.readFile(path, 'utf8', (readErr, data) => {
    console.log(readErr === null);
    console.log(data === 'jube async write');
    fs.unlinkSync(path);
  });
});
