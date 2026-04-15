// crypto cipher, pbkdf2, scrypt tests
var crypto = require('node:crypto');
var Buffer = require('buffer');

// === AES-256-CBC encrypt/decrypt ===
var key = crypto.randomBytes(32);
var iv = crypto.randomBytes(16);
var cipher = crypto.createCipheriv('aes-256-cbc', key, iv);
var enc1 = cipher.update(Buffer.from('Hello World'));
var enc2 = cipher.final();
console.log('cbc enc1 len > 0:', enc1.length > 0);
// decrypt
var decipher = crypto.createDecipheriv('aes-256-cbc', key, iv);
var dec1 = decipher.update(enc1);
var dec2 = decipher.update(enc2);
var dec3 = decipher.final();
var decrypted = Buffer.concat([dec1, dec2, dec3]);
console.log('aes-256-cbc:', Buffer.toString(decrypted));

// === AES-256-CTR encrypt/decrypt ===
var key2 = crypto.randomBytes(32);
var iv2 = crypto.randomBytes(16);
var cipher2 = crypto.createCipheriv('aes-256-ctr', key2, iv2);
var enc2a = cipher2.update(Buffer.from('CTR mode test'));
var enc2b = cipher2.final();
var decipher2 = crypto.createDecipheriv('aes-256-ctr', key2, iv2);
var dec2a = decipher2.update(enc2a);
var dec2b = decipher2.update(enc2b);
var dec2c = decipher2.final();
var decrypted2 = Buffer.concat([dec2a, dec2b, dec2c]);
console.log('aes-256-ctr:', Buffer.toString(decrypted2));

// === AES-256-GCM encrypt/decrypt ===
var key3 = crypto.randomBytes(32);
var iv3 = crypto.randomBytes(12);
var gcmCipher = crypto.createCipheriv('aes-256-gcm', key3, iv3);
var gcmEnc1 = gcmCipher.update(Buffer.from('GCM authenticated'));
var gcmEnc2 = gcmCipher.final();
var authTag = gcmCipher.getAuthTag();
console.log('gcm authTag length:', authTag.length);

var gcmDecipher = crypto.createDecipheriv('aes-256-gcm', key3, iv3);
gcmDecipher.setAuthTag(authTag);
var gcmEncAll = Buffer.concat([gcmEnc1, gcmEnc2]);
var gcmDec1 = gcmDecipher.update(gcmEncAll);
var gcmDec2 = gcmDecipher.final();
var gcmDecrypted = Buffer.concat([gcmDec1, gcmDec2]);
console.log('aes-256-gcm:', Buffer.toString(gcmDecrypted));

// === pbkdf2Sync ===
var derived = crypto.pbkdf2Sync('password', 'salt', 100000, 32, 'sha256');
console.log('pbkdf2Sync length:', derived.length);
// verify deterministic: same inputs = same output
var derived2 = crypto.pbkdf2Sync('password', 'salt', 100000, 32, 'sha256');
console.log('pbkdf2 deterministic:', derived[0] == derived2[0]);

// === scryptSync ===
var scryptKey = crypto.scryptSync('password', 'salt', 32);
console.log('scryptSync length:', scryptKey.length);
// deterministic
var scryptKey2 = crypto.scryptSync('password', 'salt', 32);
console.log('scrypt deterministic:', scryptKey[0] == scryptKey2[0]);

// scrypt with options
var scryptKey3 = crypto.scryptSync('test', 'nacl', 64, { N: 1024, r: 8, p: 1 });
console.log('scryptSync options length:', scryptKey3.length);

// === getCiphers ===
var ciphers = crypto.getCiphers();
console.log('getCiphers includes aes-256-cbc:', ciphers.indexOf('aes-256-cbc') >= 0);
console.log('getCiphers includes aes-256-gcm:', ciphers.indexOf('aes-256-gcm') >= 0);

// === createHash (pre-existing, should work now) ===
var hash = crypto.createHash('sha256');
hash.update('hello');
var hashResult = hash.digest('hex');
console.log('createHash sha256:', hashResult == '2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824');

// === createHmac (pre-existing, should work now) ===
var hmac = crypto.createHmac('sha256', 'secret');
hmac.update('hello');
var hmacResult = hmac.digest('hex');
console.log('createHmac works:', hmacResult.length > 0);
