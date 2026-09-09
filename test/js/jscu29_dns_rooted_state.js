// JSCU29: DNS namespace and resolver caches share one precise realm root range.
const dns = require('dns');
console.log(typeof dns.lookup, typeof dns.promises.resolve,
    Array.isArray(dns.getServers()), typeof dns.Resolver);
