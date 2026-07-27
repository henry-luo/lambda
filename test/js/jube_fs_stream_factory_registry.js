const fs = require('fs');

const path = 'temp/jube-fs-stream-factory.txt';
fs.writeFileSync(path, 'stream');
const stream = fs.createReadStream(path);
stream.on('data', (chunk) => console.log(chunk.toString()));
stream.on('end', () => console.log(true));
console.log(typeof fs.ReadStream);
console.log(typeof fs.WriteStream);
const output = 'temp/jube-fs-stream-factory-output.txt';
const writer = fs.createWriteStream(output);
writer.write('written');
writer.end();
writer.on('finish', () => console.log(fs.readFileSync(output, 'utf8')));
