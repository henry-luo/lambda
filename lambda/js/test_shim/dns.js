'use strict';

// Lambda-compatible subset of Node's common/dns test helper.

const assert = require('assert');
const { isIP } = require('net');

const types = {
  A: 1,
  AAAA: 28,
  NS: 2,
  CNAME: 5,
  SOA: 6,
  PTR: 12,
  MX: 15,
  TXT: 16,
  SRV: 33,
  ANY: 255,
  CAA: 257,
};

const classes = {
  IN: 1,
};

function readDomainFromPacket(buffer, offset) {
  assert.ok(offset < buffer.length);
  const length = buffer[offset];

  if (length === 0) {
    return { nread: 1, domain: '' };
  }

  if ((length & 0xC0) === 0) {
    const start = offset + 1;
    const chunk = buffer.toString('ascii', start, start + length);
    const rest = readDomainFromPacket(buffer, start + length);
    return {
      nread: 1 + length + rest.nread,
      domain: rest.domain ? chunk + '.' + rest.domain : chunk,
    };
  }

  assert.strictEqual(length & 0xC0, 0xC0);
  const pointeeOffset = buffer.readUInt16BE(offset) & ~0xC000;
  return {
    nread: 2,
    domain: readDomainFromPacket(buffer, pointeeOffset).domain,
  };
}

function typeName(typeValue) {
  for (const name in types) {
    if (types[name] === typeValue) return name;
  }
  return undefined;
}

function parseRecord(buffer, offset, domain, typeValue, cls) {
  const rr = {
    domain,
    cls,
    type: typeName(typeValue),
    ttl: buffer.readInt32BE(offset),
  };
  const dataLength = buffer.readUInt16BE(offset + 4);
  offset += 6;
  const dataOffset = offset;

  switch (typeValue) {
    case types.A:
      assert.strictEqual(dataLength, 4);
      rr.address = buffer[offset] + '.' + buffer[offset + 1] + '.' +
                   buffer[offset + 2] + '.' + buffer[offset + 3];
      break;
    case types.AAAA:
      assert.strictEqual(dataLength, 16);
      rr.address = buffer.toString('hex', offset, offset + 16)
                         .replace(/(.{4}(?!$))/g, '$1:');
      break;
    case types.TXT: {
      let position = offset;
      rr.entries = [];
      while (position < dataOffset + dataLength) {
        const txtLength = buffer[position];
        rr.entries.push(buffer.toString('utf8',
                                        position + 1,
                                        position + 1 + txtLength));
        position += 1 + txtLength;
      }
      assert.strictEqual(position, dataOffset + dataLength);
      break;
    }
    case types.MX: {
      rr.priority = buffer.readUInt16BE(offset);
      const result = readDomainFromPacket(buffer, offset + 2);
      rr.exchange = result.domain;
      assert.strictEqual(2 + result.nread, dataLength);
      break;
    }
    case types.NS:
    case types.CNAME:
    case types.PTR: {
      const result = readDomainFromPacket(buffer, offset);
      rr.value = result.domain;
      assert.strictEqual(result.nread, dataLength);
      break;
    }
    case types.SOA: {
      const mname = readDomainFromPacket(buffer, offset);
      const rname = readDomainFromPacket(buffer, offset + mname.nread);
      const trailerOffset = offset + mname.nread + rname.nread;
      rr.nsname = mname.domain;
      rr.hostmaster = rname.domain;
      rr.serial = buffer.readUInt32BE(trailerOffset);
      rr.refresh = buffer.readUInt32BE(trailerOffset + 4);
      rr.retry = buffer.readUInt32BE(trailerOffset + 8);
      rr.expire = buffer.readUInt32BE(trailerOffset + 12);
      rr.minttl = buffer.readUInt32BE(trailerOffset + 16);
      assert.strictEqual(trailerOffset + 20, dataOffset + dataLength);
      break;
    }
    case types.SRV: {
      rr.priority = buffer.readUInt16BE(offset);
      rr.weight = buffer.readUInt16BE(offset + 2);
      rr.port = buffer.readUInt16BE(offset + 4);
      const target = readDomainFromPacket(buffer, offset + 6);
      rr.name = target.domain;
      assert.strictEqual(6 + target.nread, dataLength);
      break;
    }
    case types.CAA: {
      rr.critical = !!buffer[offset];
      const tagLength = buffer[offset + 1];
      const tag = buffer.toString('ascii', offset + 2, offset + 2 + tagLength);
      const value = buffer.toString('utf8',
                                    offset + 2 + tagLength,
                                    dataOffset + dataLength);
      rr[tag] = value;
      break;
    }
    default:
      throw new Error('Unknown RR type ' + rr.type);
  }

  return { nread: 6 + dataLength, record: rr };
}

function parseDNSPacket(buffer) {
  assert.ok(buffer.length >= 12);

  const parsed = {
    id: buffer.readUInt16BE(0),
    flags: buffer.readUInt16BE(2),
  };

  const counts = [
    ['questions', buffer.readUInt16BE(4)],
    ['answers', buffer.readUInt16BE(6)],
    ['authorityAnswers', buffer.readUInt16BE(8)],
    ['additionalRecords', buffer.readUInt16BE(10)],
  ];

  let offset = 12;
  for (const section of counts) {
    const sectionName = section[0];
    const count = section[1];
    parsed[sectionName] = [];

    for (let i = 0; i < count; i++) {
      const domainPart = readDomainFromPacket(buffer, offset);
      offset += domainPart.nread;

      const typeValue = buffer.readUInt16BE(offset);
      const cls = buffer.readUInt16BE(offset + 2);
      offset += 4;

      if (sectionName === 'questions') {
        parsed[sectionName].push({
          domain: domainPart.domain,
          cls,
          type: typeName(typeValue),
        });
      } else {
        const parsedRecord = parseRecord(buffer,
                                         offset,
                                         domainPart.domain,
                                         typeValue,
                                         cls);
        offset += parsedRecord.nread;
        parsed[sectionName].push(parsedRecord.record);
      }

      assert.ok(offset <= buffer.length);
    }
  }

  assert.strictEqual(offset, buffer.length);
  return parsed;
}

function writeUInt16(value) {
  const buffer = Buffer.alloc(2);
  buffer.writeUInt16BE(value || 0, 0);
  return buffer;
}

function writeUInt32(value) {
  const buffer = Buffer.alloc(4);
  buffer.writeUInt32BE(value || 0, 0);
  return buffer;
}

function writeInt32(value) {
  const buffer = Buffer.alloc(4);
  buffer.writeInt32BE(value || 0, 0);
  return buffer;
}

function writeIPv6(ip) {
  const parts = ip.replace(/^:|:$/g, '').split(':');
  const buffer = Buffer.alloc(16);
  let offset = 0;

  for (const part of parts) {
    if (part === '') {
      offset += 16 - 2 * (parts.length - 1);
    } else {
      buffer.writeUInt16BE(parseInt(part, 16), offset);
      offset += 2;
    }
  }

  return buffer;
}

function writeDomainName(domain) {
  const buffers = [];
  const labels = domain ? domain.split('.') : [];
  for (const label of labels) {
    assert(label.length < 64);
    buffers.push(Buffer.from([label.length]));
    buffers.push(Buffer.from(label, 'ascii'));
  }
  buffers.push(Buffer.alloc(1));
  return Buffer.concat(buffers);
}

function writeTxtEntry(text) {
  const data = Buffer.from(text);
  assert(data.length < 256);
  return Buffer.concat([Buffer.from([data.length]), data]);
}

function recordPayload(rr) {
  switch (rr.type) {
    case 'A':
      return Buffer.from(rr.address.split('.').map(function(part) {
        return Number(part);
      }));
    case 'AAAA':
      return writeIPv6(rr.address);
    case 'TXT':
      return Buffer.concat(rr.entries.map(writeTxtEntry));
    case 'MX':
      return Buffer.concat([
        writeUInt16(rr.priority),
        writeDomainName(rr.exchange),
      ]);
    case 'NS':
    case 'CNAME':
    case 'PTR':
      return writeDomainName(rr.value);
    case 'SOA':
      return Buffer.concat([
        writeDomainName(rr.nsname),
        writeDomainName(rr.hostmaster),
        writeUInt32(rr.serial),
        writeUInt32(rr.refresh),
        writeUInt32(rr.retry),
        writeUInt32(rr.expire),
        writeUInt32(rr.minttl),
      ]);
    case 'CAA': {
      const tag = Buffer.from('issue', 'ascii');
      const value = Buffer.from(rr.issue || rr.value || '');
      return Buffer.concat([
        Buffer.from([rr.critical ? 1 : 0, tag.length]),
        tag,
        value,
      ]);
    }
    case 'SRV':
      return Buffer.concat([
        writeUInt16(rr.priority),
        writeUInt16(rr.weight),
        writeUInt16(rr.port),
        writeDomainName(rr.name),
      ]);
    default:
      throw new Error('Unknown RR type ' + rr.type);
  }
}

function writeRecord(rr) {
  assert(types[rr.type]);
  const payload = recordPayload(rr);
  return Buffer.concat([
    writeDomainName(rr.domain),
    writeUInt16(types[rr.type]),
    writeUInt16(rr.cls === undefined ? classes.IN : rr.cls),
    writeInt32(rr.ttl),
    writeUInt16(payload.length),
    payload,
  ]);
}

function writeQuestion(q) {
  assert(types[q.type]);
  return Buffer.concat([
    writeDomainName(q.domain),
    writeUInt16(types[q.type]),
    writeUInt16(q.cls === undefined ? classes.IN : q.cls),
  ]);
}

function writeDNSPacket(parsed) {
  const questions = parsed.questions || [];
  const answers = parsed.answers || [];
  const authorityAnswers = parsed.authorityAnswers || [];
  const additionalRecords = parsed.additionalRecords || [];
  const buffers = [
    writeUInt16(parsed.id),
    writeUInt16(parsed.flags === undefined ? 0x8180 : parsed.flags),
    writeUInt16(questions.length),
    writeUInt16(answers.length),
    writeUInt16(authorityAnswers.length),
    writeUInt16(additionalRecords.length),
  ];

  for (const question of questions) buffers.push(writeQuestion(question));
  for (const rr of answers) buffers.push(writeRecord(rr));
  for (const rr of authorityAnswers) buffers.push(writeRecord(rr));
  for (const rr of additionalRecords) buffers.push(writeRecord(rr));

  return Buffer.concat(buffers);
}

const mockedErrorCode = 'ENOTFOUND';
const mockedSysCall = 'getaddrinfo';

function errorLookupMock(code, syscall) {
  code = code || mockedErrorCode;
  syscall = syscall || mockedSysCall;
  return function lookupWithError(hostname, dnsopts, cb) {
    const err = new Error(syscall + ' ' + code + ' ' + hostname);
    err.code = code;
    err.errno = code;
    err.syscall = syscall;
    err.hostname = hostname;
    cb(err);
  };
}

function createMockedLookup() {
  const addresses = Array.prototype.slice.call(arguments).map(function(address) {
    return { address: address, family: isIP(address) };
  });

  return function lookup(hostname, options, cb) {
    if (options && options.all === true) {
      process.nextTick(function() {
        cb(null, addresses);
      });
      return;
    }

    process.nextTick(function() {
      cb(null, addresses[0].address, addresses[0].family);
    });
  };
}

module.exports = {
  types,
  classes,
  writeDNSPacket,
  parseDNSPacket,
  errorLookupMock,
  mockedErrorCode,
  mockedSysCall,
  createMockedLookup,
};
