const fs = require('fs');
const promises = require('fs/promises');
const path = 'temp/jube-fs-chmod-registry.txt';

fs.writeFileSync(path, 'chmod');
fs.chmodSync(path, 0o600);
const descriptor = fs.openSync(path, 'r');
fs.fchmodSync(descriptor, 0o600);
fs.closeSync(descriptor);
fs.chmod(path, 0o600, function(error) {
  console.log(error === null);
  const callbackDescriptor = fs.openSync(path, 'r');
  fs.fchmod(callbackDescriptor, 0o600, function(fchmodError) {
    console.log(fchmodError === null);
    fs.closeSync(callbackDescriptor);
    promises.chmod(path, 0o600).then(function() {
      console.log('changed');
      fs.unlinkSync(path);
    });
  });
});
