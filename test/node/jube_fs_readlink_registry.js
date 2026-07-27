const fs = require('fs');

const source = 'temp/jube-fs-readlink-source.txt';
const link = 'temp/jube-fs-readlink-link.txt';
fs.writeFileSync(source, 'readlink');
if (fs.existsSync(link)) fs.unlinkSync(link);
fs.symlinkSync('jube-fs-readlink-source.txt', link);
console.log(fs.readlinkSync(link));
fs.readlink(link, (error, target) => {
  console.log(error === null);
  console.log(target);
});
