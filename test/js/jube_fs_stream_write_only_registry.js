const fs = require('fs');

const path = 'temp/jube-fs-stream-write-only.txt';
const stream = fs.createWriteStream(path);
stream.write('written');
stream.end();
stream.on('finish', () => console.log(fs.readFileSync(path, 'utf8')));
