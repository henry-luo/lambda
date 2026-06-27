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

dns.resolve4('localhost', function(err, addresses) {
  console.log('resolve4 callback err:', err === null);
  console.log('resolve4 callback array:', Array.isArray(addresses));
  console.log('resolve4 callback ipv4:', allIPv4(addresses));

  dns.resolve('localhost', 'A', function(resolveErr, resolveAddresses) {
    console.log('resolve default err:', resolveErr === null);
    console.log('resolve default array:', Array.isArray(resolveAddresses));
    console.log('resolve default ipv4:', allIPv4(resolveAddresses));

    dns.promises.resolve4('localhost').then(function(promiseAddresses) {
      console.log('promises resolve4 array:', Array.isArray(promiseAddresses));
      console.log('promises resolve4 ipv4:', allIPv4(promiseAddresses));
    }, function(promiseErr) {
      console.log('promises resolve4 rejected:', promiseErr && promiseErr.code);
    });
  });
});
