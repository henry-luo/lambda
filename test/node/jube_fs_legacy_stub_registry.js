const fs = require('fs');

const before = fs.statSync('temp').mtimeMs;
fs.utimesSync('temp', 0, 0);
console.log(fs.statSync('temp').mtimeMs === before);
fs.utimes('temp', 0, 0, (error) => console.log(error === null));
console.log(typeof fs.opendirSync('temp'));
