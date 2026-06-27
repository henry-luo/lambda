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

const cares = process.binding('cares_wrap');
const channel = cares.ChannelWrap.prototype;

channel.queryTxt = function(hostname) {
  return [['lambda', hostname], ['spf']];
};
channel.queryMx = function(hostname) {
  return [{ exchange: 'mail.' + hostname, priority: 10 }];
};
channel.querySrv = function(hostname) {
  return [{ name: 'srv.' + hostname, port: 443, priority: 1, weight: 5 }];
};
channel.queryCname = function(hostname) {
  return ['alias.' + hostname];
};
channel.getHostByAddr = function(hostname) {
  return ['ptr.' + hostname];
};

function runRecordShapeTests() {
  dns.resolveTxt('example.test', function(txtErr, records) {
    console.log('resolveTxt err:', txtErr === null);
    console.log('resolveTxt shape:', records[0][0], records[0][1], records[1][0]);

    dns.resolveMx('example.test', function(mxErr, mxRecords) {
      console.log('resolveMx err:', mxErr === null);
      console.log('resolveMx shape:', mxRecords[0].exchange, mxRecords[0].priority);

      dns.resolveSrv('example.test', function(srvErr, srvRecords) {
        console.log('resolveSrv err:', srvErr === null);
        console.log('resolveSrv shape:', srvRecords[0].name, srvRecords[0].port,
                    srvRecords[0].priority, srvRecords[0].weight);

        dns.resolveCname('example.test', function(cnameErr, cnameRecords) {
          console.log('resolveCname err:', cnameErr === null);
          console.log('resolveCname shape:', cnameRecords[0]);

          dns.reverse('127.0.0.1', function(reverseErr, reverseRecords) {
            console.log('reverse err:', reverseErr === null);
            console.log('reverse shape:', reverseRecords[0]);

            dns.promises.resolveTxt('promise.test').then(function(promiseTxt) {
              console.log('promises resolveTxt shape:',
                          promiseTxt[0][0], promiseTxt[0][1]);
              const resolver = new dns.Resolver();
              console.log('resolver resolveTxt method:', typeof resolver.resolveTxt);
              const promiseResolver = new dns.promises.Resolver();
              console.log('promises resolver reverse method:',
                          typeof promiseResolver.reverse);
            });
          });
        });
      });
    });
  });
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
      runRecordShapeTests();
    }, function(promiseErr) {
      console.log('promises resolve4 rejected:', promiseErr && promiseErr.code);
    });
  });
});
