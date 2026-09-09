// JSCU29: DNS namespace and resolver caches use named shared realm slots.
const assert = require('assert');
const dns = require('dns');
const first = dns;
const promises = dns.promises;
const Resolver = dns.Resolver;
const resolver = new Resolver();
gc();
assert.strictEqual(require('dns'), first);
assert.strictEqual(dns.promises, promises);
assert.strictEqual(dns.Resolver, Resolver);
assert.strictEqual(Object.getPrototypeOf(resolver), Resolver.prototype);
console.log(typeof dns.lookup, typeof dns.promises.resolve,
    Array.isArray(dns.getServers()), typeof dns.Resolver);
