const fs = require('fs');

const stats = fs.statfsSync('temp');
console.log(typeof stats.bsize);
console.log(typeof stats.blocks);
fs.statfs('temp', (error, callbackStats) => {
  console.log(error === null);
  console.log(typeof callbackStats.ffree);
});
