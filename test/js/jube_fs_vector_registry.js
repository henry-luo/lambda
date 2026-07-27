const fs = require('fs');
const path = 'temp/jube-fs-vector-registry.txt';
const descriptor = fs.openSync(path, 'w+');
const writeFirst = Buffer.alloc(1);
const writeSecond = Buffer.alloc(2);
const writeThird = Buffer.alloc(1);
writeFirst[0] = 97;
writeSecond[0] = 98;
writeSecond[1] = 99;
writeThird[0] = 100;

console.log(fs.writevSync(descriptor, [writeFirst, writeSecond], 0));
const first = Buffer.alloc(1);
const second = Buffer.alloc(2);
console.log(fs.readvSync(descriptor, [first, second], 0));
console.log(first.toString() + second.toString());
fs.writev(descriptor, [writeThird], 3, function(writeError, bytesWritten, buffers) {
  console.log(writeError === null);
  console.log(bytesWritten);
  console.log(buffers[0].toString());
  fs.readv(descriptor, [first, second], 0, function(readError, bytesRead, readBuffers) {
    console.log(readError === null);
    console.log(bytesRead);
    console.log(readBuffers[0].toString() + readBuffers[1].toString());
    fs.closeSync(descriptor);
    fs.unlinkSync(path);
  });
});
