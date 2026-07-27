const fs = require('fs');

console.log(fs.constants.F_OK);
console.log(fs.constants.O_RDONLY === 0);
console.log(typeof fs.constants.S_IFREG);
console.log(fs.constants.UV_DIRENT_FILE);
console.log(fs.constants.COPYFILE_FICLONE_FORCE);
