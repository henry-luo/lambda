const crypto = require('crypto');

for (const name of ['argon2', 'argon2Sync']) {
  try {
    crypto[name]();
    console.log(name + ': false');
  } catch (err) {
    console.log(name + ':', err.code);
  }
}
