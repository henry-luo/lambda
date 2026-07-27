const fs = require('fs');
const path = 'temp/jube-fs-descriptor-callback-registry.txt';

fs.writeFileSync(path, 'descriptor');
fs.open(path, 'r', function(error, descriptor) {
  console.log(error === null);
  console.log(typeof descriptor);
  fs.close(descriptor, function(closeError) {
    console.log(closeError === null);
    fs.unlinkSync(path);
  });
});
