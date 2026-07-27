const fs = require('fs');

const path = 'temp/jube-fs-chown.txt';
fs.writeFileSync(path, 'chown');
const owner = fs.statSync(path);
fs.chownSync(path, owner.uid, owner.gid);
console.log(true);
fs.chown(path, owner.uid, owner.gid, (error) => console.log(error === null));
fs.promises.chown(path, owner.uid, owner.gid).then(() => console.log(true));
