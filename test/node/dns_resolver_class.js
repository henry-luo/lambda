'use strict';

const dns = require('node:dns');

function isIPv4(address) {
  if (typeof address !== 'string') return false;
  const parts = address.split('.');
  if (parts.length !== 4) return false;
  for (let i = 0; i < parts.length; i++) {
    const value = Number(parts[i]);
    if (String(value) !== parts[i] || value < 0 || value > 255) return false;
  }
  return true;
}

function allIPv4(addresses) {
  if (!Array.isArray(addresses) || addresses.length === 0) return false;
  for (let i = 0; i < addresses.length; i++) {
    if (!isIPv4(addresses[i])) return false;
  }
  return true;
}

const resolver = new dns.Resolver();
console.log('resolver constructor:', typeof dns.Resolver);
console.log('resolver instanceof:', resolver instanceof dns.Resolver);
console.log('resolver resolve4 method:', typeof resolver.resolve4);
console.log('resolver getServers method:', typeof resolver.getServers);
console.log('resolver setServers method:', typeof resolver.setServers);
console.log('resolver getServers array:', Array.isArray(resolver.getServers()));

resolver.setServers(['127.0.0.1']);
const resolverServers = resolver.getServers();
resolverServers.push('8.8.8.8');
console.log('resolver setServers first:', resolver.getServers()[0]);
console.log('resolver getServers copy:', resolver.getServers().length === 1);

try {
  resolver.setServers('127.0.0.1');
} catch (err) {
  console.log('resolver setServers bad list:', err && err.code);
}

resolver.resolve4('localhost', function(err, addresses) {
  console.log('resolver resolve4 err:', err === null);
  console.log('resolver resolve4 ipv4:', allIPv4(addresses));

  const promiseResolver = new dns.promises.Resolver();
  console.log('promises resolver instanceof:', promiseResolver instanceof dns.promises.Resolver);
  console.log('promises resolver resolve4 method:', typeof promiseResolver.resolve4);
  console.log('promises resolver getServers method:', typeof promiseResolver.getServers);
  console.log('promises resolver setServers method:', typeof promiseResolver.setServers);
  promiseResolver.setServers(['127.0.0.1']);
  console.log('promises resolver setServers first:', promiseResolver.getServers()[0]);

  promiseResolver.resolve4('localhost').then(function(promiseAddresses) {
    console.log('promises resolver resolve4 ipv4:', allIPv4(promiseAddresses));
  }, function(promiseErr) {
    console.log('promises resolver rejected:', promiseErr && promiseErr.code);
  });
});
