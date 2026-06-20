const { getOpenSSLSecLevel } = require('internal/crypto/util');

const secLevel = getOpenSSLSecLevel();
console.log('secLevel type:', typeof secLevel);
console.log('secLevel range:', secLevel >= 0 && secLevel <= 5);
