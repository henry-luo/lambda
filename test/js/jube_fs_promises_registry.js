const fs = require('fs');
const promises = require('fs/promises');
const path = 'temp/jube-fs-promises-registry.txt';

promises.writeFile(path, 'promise').then(() => promises.appendFile(path, ' registry')).then(() => promises.readFile(path)).then((data) => {
  console.log(data === 'promise registry');
  fs.unlinkSync(path);
});
