const domain = require('domain');
const nodeDomain = require('node:domain');

console.log(domain === nodeDomain, domain.default === domain);
console.log(typeof domain.create, typeof domain.createDomain, typeof domain.Domain);
console.log(typeof domain.create());
