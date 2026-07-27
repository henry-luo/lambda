const fs = require('fs');
const promises = require('fs/promises');
const root = 'temp/jube-fs-directory-promises-registry';
const first = root + '/first.txt';
const second = root + '/second.txt';

promises.mkdir(root)
  .then(() => promises.writeFile(first, 'entry'))
  .then(() => promises.readdir(root))
  .then((entries) => {
    console.log(entries[0]);
    return promises.rename(first, second);
  })
  .then(() => promises.unlink(second))
  .then(() => {
    fs.rmdirSync(root);
    console.log('closed');
  });
