const fs = require('fs');
const promises = require('fs/promises');
const root = 'temp/jube-fs-rm-registry';
const nested = root + '/nested';
const file = nested + '/entry.txt';

fs.mkdirSync(nested, { recursive: true });
fs.writeFileSync(file, 'rm');
fs.rmSync(root, { recursive: true });
console.log(fs.existsSync(root));

fs.mkdirSync(nested, { recursive: true });
fs.writeFileSync(file, 'rm');
fs.rm(root, { recursive: true }, function(error) {
  console.log(error === null);
  fs.mkdirSync(nested, { recursive: true });
  fs.writeFileSync(file, 'rm');
  promises.rm(root, { recursive: true }).then(function() {
    console.log(fs.existsSync(root));
  });
});
