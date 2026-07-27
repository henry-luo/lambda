const fs = require('fs');
const source = 'temp/jube-fs-link-source.txt';
const syncLink = 'temp/jube-fs-link-sync.txt';
const callbackLink = 'temp/jube-fs-link-callback.txt';

fs.writeFileSync(source, 'link');
fs.linkSync(source, syncLink);
console.log(fs.readFileSync(syncLink, 'utf8'));
fs.link(source, callbackLink, function(error) {
  console.log(error === null);
  console.log(fs.readFileSync(callbackLink, 'utf8'));
  fs.unlinkSync(source);
  fs.unlinkSync(syncLink);
  fs.unlinkSync(callbackLink);
});
