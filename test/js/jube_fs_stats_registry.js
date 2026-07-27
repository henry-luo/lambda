const fs = require('fs');
const promises = require('fs/promises');

var path = './temp/jube-fs-stats-registry.txt';
fs.writeFileSync(path, 'stats');

fs.stat(path, function(error, stats) {
    console.log(error === null);
    console.log(typeof stats.birthtimeMs);
    console.log(typeof stats.mtime.getTime);
});

var descriptor = fs.openSync(path, 'r');
fs.fstat(descriptor, { bigint: true }, function(error, stats) {
    console.log(error === null);
    console.log(typeof stats.size);
});
fs.closeSync(descriptor);

promises.lstat(path, { bigint: true }).then(function(stats) {
    console.log(typeof stats.ctimeMs);
    console.log(stats.isFile());
    fs.unlinkSync(path);
});
