'use strict';

const crypto = require('crypto');

const dh = crypto.createDiffieHellman(1024);
const prime = dh.getPrime('buffer');

console.log('dh instanceof:', dh instanceof crypto.DiffieHellman);
console.log('dh prime bytes:', prime.length >= 128);
console.log('dh generator hex:', dh.getGenerator('hex'));

const dhCtor = crypto.DiffieHellman(prime, 'buffer');
console.log('dh ctor instanceof:', dhCtor instanceof crypto.DiffieHellman);

const group = crypto.createDiffieHellmanGroup('modp5');
console.log('dh group instanceof:', group instanceof crypto.DiffieHellmanGroup);
console.log('dh group prime bytes:', group.getPrime().length >= 192);

const groupCtor = crypto.DiffieHellmanGroup('modp5');
console.log('dh group ctor instanceof:', groupCtor instanceof crypto.DiffieHellmanGroup);

const ecdh = crypto.createECDH('prime256v1');
console.log('ecdh instanceof:', ecdh instanceof crypto.ECDH);
console.log('ecdh ctor instanceof:', crypto.ECDH('prime256v1') instanceof crypto.ECDH);
console.log('curves has secp384r1:', crypto.getCurves().indexOf('secp384r1') >= 0);
