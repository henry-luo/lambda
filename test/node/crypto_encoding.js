'use strict';

const assert = require('assert');
const crypto = require('crypto');

function makeCipher() {
  return crypto.createCipheriv('aes-256-cbc', Buffer.alloc(32), Buffer.alloc(16));
}

{
  const cipher = makeCipher();
  cipher.update('test', 'utf-8', 'utf-8');
  assert.throws(function() {
    cipher.update('666f6f', 'hex', 'hex');
  }, /Cannot change encoding/);
  cipher.final('utf-8');
  console.log('cipher update encoding lock:', true);
}

{
  const cipher = makeCipher();
  cipher.update('test', 'utf-8', 'utf-8');
  assert.throws(function() {
    cipher.final('hex');
  }, /Cannot change encoding/);
  cipher.final('utf-8');
  console.log('cipher final encoding lock:', true);
}

{
  const cipher = makeCipher();
  cipher.update('test', 'utf-8', 'utf-8');
  assert.throws(function() {
    cipher.final('bad2');
  }, { code: 'ERR_UNKNOWN_ENCODING', message: 'Unknown encoding: bad2' });
  cipher.final('utf-8');
  console.log('cipher final unknown encoding:', true);
}

{
  const data = Buffer.from('hash me');
  const direct = crypto.createHash('sha1').update(data).digest('hex');
  const fromBase64 = crypto.createHash('sha1')
    .update(data.toString('base64'), 'base64')
    .digest('hex');
  assert.strictEqual(fromBase64, direct);
  console.log('hash update base64:', true);
}

{
  const utf8 = crypto.createHash('sha512').update('UTF-8 text').digest('hex');
  const latin1 = crypto.createHash('sha512').update('UTF-8 text', 'latin1').digest('hex');
  assert.strictEqual(utf8, latin1);

  const utf8Wide = crypto.createHash('sha512').update('UTF-8 text \u4e2d').digest('hex');
  const latin1Wide = crypto.createHash('sha512').update('UTF-8 text \u4e2d', 'latin1').digest('hex');
  assert.notStrictEqual(utf8Wide, latin1Wide);
  console.log('hash update latin1:', true);
}

{
  const streamHash = crypto.createHash('sha256');
  streamHash.end('abc');
  const streamed = streamHash.read();
  const direct = crypto.createHash('sha256').update('abc').digest();
  assert.deepStrictEqual(streamed, direct);
  assert.strictEqual(streamHash.read(), null);
  console.log('hash end/read:', true);
}
