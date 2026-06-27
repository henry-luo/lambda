'use strict';

const dns = require('node:dns');

function printThrow(label, fn) {
  try {
    fn();
    console.log(label + ': no throw');
  } catch (err) {
    console.log(label + ':', err && err.code ? err.code : err.name);
  }
}

const resolver = new dns.Resolver();
const promiseResolver = new dns.promises.Resolver();

console.log('resolver setLocalAddress method:', typeof resolver.setLocalAddress);
console.log('promises resolver setLocalAddress method:',
            typeof promiseResolver.setLocalAddress);

console.log('resolver ipv4:', resolver.setLocalAddress('127.0.0.1') === undefined);
console.log('resolver ipv6:', resolver.setLocalAddress('::1') === undefined);
console.log('resolver both:', resolver.setLocalAddress('127.0.0.1', '::1') === undefined);
console.log('promises resolver both:',
            promiseResolver.setLocalAddress('127.0.0.1', '::1') === undefined);

printThrow('resolver duplicate ipv4', function() {
  resolver.setLocalAddress('127.0.0.1', '127.0.0.1');
});
printThrow('resolver duplicate ipv6', function() {
  resolver.setLocalAddress('::1', '::1');
});
printThrow('resolver bad address', function() {
  resolver.setLocalAddress('bad');
});
printThrow('resolver bad ipv4 type', function() {
  resolver.setLocalAddress(123);
});
printThrow('resolver bad ipv6 type', function() {
  resolver.setLocalAddress('127.0.0.1', 42);
});
printThrow('resolver missing', function() {
  resolver.setLocalAddress();
});
printThrow('promises resolver missing', function() {
  promiseResolver.setLocalAddress();
});
