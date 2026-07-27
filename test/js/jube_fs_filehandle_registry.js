const fs = require('fs');
const promises = require('fs/promises');
const path = 'temp/jube-fs-filehandle-registry.txt';

fs.writeFileSync(path, 'handle');
console.log(typeof promises.open);
promises.open(path, 'r').then((handle) => {
  console.log(typeof handle.fd);
  return handle.readFile().then((data) => {
    console.log(data);
    return handle.read(Buffer.alloc(3), 0, 3, 0).then((result) => {
      console.log(result.bytesRead);
      console.log(result.buffer.toString());
      return handle.close();
    });
  });
}).then(() => {
  fs.unlinkSync(path);
  console.log('closed');
});
