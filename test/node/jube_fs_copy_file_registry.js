const fs = require('fs');
const promises = require('fs/promises');
const source = 'temp/jube-fs-copy-file-source.txt';
const first = 'temp/jube-fs-copy-file-first.txt';
const second = 'temp/jube-fs-copy-file-second.txt';
const third = 'temp/jube-fs-copy-file-third.txt';

fs.writeFileSync(source, 'copy');
fs.copyFileSync(source, first);
console.log(fs.readFileSync(first, 'utf8'));
fs.copyFile(source, second, function(error) {
  console.log(error === null);
  promises.copyFile(second, third).then(function() {
    console.log(fs.readFileSync(third, 'utf8'));
    fs.unlinkSync(source);
    fs.unlinkSync(first);
    fs.unlinkSync(second);
    fs.unlinkSync(third);
  });
});
