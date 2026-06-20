const crypto = require('crypto');

const info = crypto.getCipherInfo('aes-128-cbc');
console.log('cbc name:', info.name);
console.log('cbc nid:', info.nid);
console.log('cbc block:', info.blockSize);
console.log('cbc iv:', info.ivLength);
console.log('cbc key:', info.keyLength);
console.log('cbc mode:', info.mode);
console.log('cbc nid lookup:', crypto.getCipherInfo(info.nid).name);
console.log('cbc key ok:', Boolean(crypto.getCipherInfo('aes-128-cbc', { keyLength: 16 })));
console.log('cbc key bad:', crypto.getCipherInfo('aes-128-cbc', { keyLength: 12 }) === undefined);
console.log('unknown:', crypto.getCipherInfo('aes-128-ccm') === undefined);

try {
  crypto.getCipherInfo(null);
  console.log('bad arg: false');
} catch (err) {
  console.log('bad arg:', err.code);
}
