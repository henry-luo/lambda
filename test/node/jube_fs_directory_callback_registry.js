const fs = require('fs');
const root = 'temp/jube-fs-directory-callback-registry';
const first = root + '/first.txt';
const second = root + '/second.txt';

fs.mkdir(root, function(mkdirError) {
  console.log(mkdirError === null);
  fs.writeFileSync(first, 'entry');
  fs.readdir(root, function(readdirError, entries) {
    console.log(readdirError === null);
    console.log(entries[0]);
    fs.rename(first, second, function(renameError) {
      console.log(renameError === null);
      fs.unlink(second, function(unlinkError) {
        console.log(unlinkError === null);
        fs.rmdir(root, function(rmdirError) {
          console.log(rmdirError === null);
        });
      });
    });
  });
});
