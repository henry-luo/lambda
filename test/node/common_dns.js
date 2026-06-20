'use strict';

const dnstools = require('../../lambda/js/test_shim/dns');

const packet = dnstools.writeDNSPacket({
  id: 0x1234,
  questions: [
    { domain: 'example.com', type: 'A' },
  ],
  answers: [
    { domain: 'example.com', type: 'A', ttl: 60, address: '127.0.0.1' },
    { domain: 'example.com', type: 'TXT', ttl: 60, entries: ['lambda'] },
    {
      domain: '_svc.example.com',
      type: 'SRV',
      ttl: 30,
      priority: 1,
      weight: 2,
      port: 443,
      name: 'target.example.com',
    },
  ],
  authorityAnswers: [],
  additionalRecords: [],
});

const parsed = dnstools.parseDNSPacket(packet);
console.log('dns id:', parsed.id);
console.log('dns question:', parsed.questions[0].domain, parsed.questions[0].type);
console.log('dns answer a:', parsed.answers[0].address);
console.log('dns answer txt:', parsed.answers[1].entries[0]);
console.log('dns answer srv:', parsed.answers[2].priority, parsed.answers[2].port,
            parsed.answers[2].name);

dnstools.errorLookupMock()('something.invalid', {}, function(err) {
  console.log('dns error:', err.code, err.syscall, err.hostname);
});

const lookup = dnstools.createMockedLookup('127.0.0.1', '::1');
lookup('example.com', { all: true }, function(err, addresses) {
  console.log('dns all:', addresses.length, addresses[0].family, addresses[1].family);
});
lookup('example.com', {}, function(err, address, family) {
  console.log('dns one:', address, family);
});
