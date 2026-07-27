const fs = require('fs');
const path = 'temp/jube-fs-descriptor-io-callback-registry.txt';

fs.writeFileSync(path, 'abc');
const descriptor = fs.openSync(path, 'r+');
const buffer = Buffer.alloc(1);
fs.read(descriptor, buffer, 0, 1, 0, function(readError, bytesRead, readBuffer) {
  console.log(readError === null);
  console.log(bytesRead);
  console.log(readBuffer.toString());
  fs.write(descriptor, 'z', 0, 1, 0, function(writeError, bytesWritten, writtenData) {
    console.log(writeError === null);
    console.log(bytesWritten);
    console.log(writtenData);
    fs.closeSync(descriptor);
    fs.unlinkSync(path);
  });
});
