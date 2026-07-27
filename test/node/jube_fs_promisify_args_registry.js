const fs = require('fs');
const util = require('util');

const path = 'temp/jube-fs-promisify-args.txt';
fs.writeFileSync(path, 'arg');
const descriptor = fs.openSync(path, 'r');
const buffer = Buffer.alloc(3);
util.promisify(fs.read)(descriptor, buffer, 0, 3, 0).then((result) => {
  console.log(result.bytesRead);
  console.log(result.buffer === buffer);
  fs.closeSync(descriptor);
}, (error) => console.log(error.code || error.message));
