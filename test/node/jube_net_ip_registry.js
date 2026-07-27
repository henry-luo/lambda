const net = require('net');
const nodeNet = require('node:net');

console.log(net === nodeNet);
console.log(net.isIP('127.0.0.1'), net.isIP('::1'), net.isIP('not-an-ip'));
console.log(net.isIPv4('127.0.0.1'), net.isIPv6('::1'), net.isIPv6('127.0.0.1'));
console.log(net.isIP({toString: () => '127.0.0.1'}));
const normalized = net._normalizeArgs([8123, '127.0.0.1', () => {}]);
const internalNet = require('internal/net');
console.log(normalized[0].port, normalized[0].host, typeof normalized[1]);
console.log(internalNet.normalizedArgsSymbol, internalNet.kReinitializeHandle,
  normalized[internalNet.normalizedArgsSymbol]);
const dns = require('dns');
console.log(typeof dns.lookupSync, dns.lookupSync('127.0.0.1'));
console.log(net.getDefaultAutoSelectFamily(), net.getDefaultAutoSelectFamilyAttemptTimeout());
net.setDefaultAutoSelectFamily(true);
net.setDefaultAutoSelectFamilyAttemptTimeout(321);
console.log(net.getDefaultAutoSelectFamily(), net.getDefaultAutoSelectFamilyAttemptTimeout());
try {
  net.setDefaultAutoSelectFamilyAttemptTimeout(0);
} catch (error) {
  console.log(error.code);
}
