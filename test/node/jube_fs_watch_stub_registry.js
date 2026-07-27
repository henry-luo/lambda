const fs = require('fs');

const watcher = fs.watch('temp');
console.log(typeof watcher.close);
console.log(watcher.ref() === watcher);
console.log(watcher.unref() === watcher);
console.log(watcher.close() === undefined);
console.log(typeof fs.watchFile('temp', () => {}).close);
console.log(fs.unwatchFile('temp') === undefined);
