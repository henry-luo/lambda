const fs = require('fs');
const path = 'temp/jube-fs-append-registry.txt';

fs.writeFile(path, 'first', (writeErr) => {
  console.log(writeErr === null);
  fs.appendFile(path, ':second', (appendErr) => {
    console.log(appendErr === null);
    fs.readFile(path, (readErr, data) => {
      console.log(readErr === null);
      console.log(data === 'first:second');
      fs.unlinkSync(path);
    });
  });
});
