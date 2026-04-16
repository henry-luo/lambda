// crypto basic tests — randomBytes, randomUUID, randomInt, getHashes, timingSafeEqual
var crypto = require('node:crypto');
var Buffer = require('buffer');

// randomBytes returns a buffer of correct size
var rb = crypto.randomBytes(16);
console.log('randomBytes length:', rb.length);
console.log('randomBytes is buffer:', Buffer.isBuffer(rb));

// randomBytes different each time
var rb2 = crypto.randomBytes(16);
console.log('randomBytes unique:', rb.toString('hex') !== rb2.toString('hex'));

// randomUUID returns a string of correct format (8-4-4-4-12)
var uuid = crypto.randomUUID();
console.log('randomUUID length:', uuid.length);
console.log('randomUUID has dashes:', uuid.indexOf('-') > 0);

// two UUIDs should be different
var uuid2 = crypto.randomUUID();
console.log('randomUUID unique:', uuid !== uuid2);

// randomInt with min, max
var ri = crypto.randomInt(0, 100);
console.log('randomInt in range:', ri >= 0 && ri < 100);

// randomInt with small range
var ri2 = crypto.randomInt(0, 10);
console.log('randomInt small range:', ri2 >= 0 && ri2 < 10);

// getHashes returns array including sha256
var hashes = crypto.getHashes();
console.log('getHashes is array:', Array.isArray(hashes));
console.log('getHashes has sha256:', hashes.indexOf('sha256') >= 0);
console.log('getHashes has sha512:', hashes.indexOf('sha512') >= 0);

// timingSafeEqual - equal buffers
var buf1 = Buffer.from('abcdef');
var buf2 = Buffer.from('abcdef');
console.log('timingSafeEqual same:', crypto.timingSafeEqual(buf1, buf2));

// timingSafeEqual - different buffers
var buf3 = Buffer.from('abcdef');
var buf4 = Buffer.from('ghijkl');
console.log('timingSafeEqual diff:', crypto.timingSafeEqual(buf3, buf4));

// createHash with different algorithms
var h256 = crypto.createHash('sha256');
h256.update('test');
var d256 = h256.digest('hex');
console.log('sha256 length:', d256.length);

var h512 = crypto.createHash('sha512');
h512.update('test');
var d512 = h512.digest('hex');
console.log('sha512 length:', d512.length);

// createHash chaining update calls
var hChain = crypto.createHash('sha256');
hChain.update('hello');
hChain.update(' world');
var dChain = hChain.digest('hex');
console.log('hash chain:', dChain === crypto.createHash('sha256').update('hello world').digest('hex'));

// createHmac
var hmac = crypto.createHmac('sha256', 'mykey');
hmac.update('data');
var hmacHex = hmac.digest('hex');
console.log('hmac length:', hmacHex.length);

// createHmac deterministic
var hmac2 = crypto.createHmac('sha256', 'mykey');
hmac2.update('data');
var hmacHex2 = hmac2.digest('hex');
console.log('hmac deterministic:', hmacHex === hmacHex2);
