const fs = require('fs');

const path = 'temp/jube-fs-stream-read-only.txt';
fs.writeFileSync(path, 'stream');
const stream = fs.createReadStream(path);
stream.on('data', (chunk) => console.log(chunk.toString()));
stream.on('end', () => console.log(true));
