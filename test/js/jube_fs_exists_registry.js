const fs = require('fs');
const util = require('util');

fs.writeFileSync('temp/jube-fs-exists.txt', 'exists');
fs.exists('temp/jube-fs-exists.txt', (exists) => console.log(exists));
util.promisify(fs.exists)('temp/jube-fs-exists.txt').then((exists) => console.log(exists));
