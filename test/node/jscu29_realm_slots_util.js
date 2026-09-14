const assert = require('assert');
const util = require('util');

const first = util;
gc();
assert.strictEqual(require('util'), first);
assert.strictEqual(util.format('%s:%d', 'slot', 29), 'slot:29');
assert.strictEqual(typeof util.inspect, 'function');

const child_process = require('child_process');
const child_process_first = child_process;
gc();
assert.strictEqual(require('child_process'), child_process_first);
assert.strictEqual(typeof child_process.exec, 'function');

const buffer = require('buffer');
const buffer_first = buffer;
const BufferConstructor = buffer.Buffer;
const BufferPrototype = BufferConstructor.prototype;
gc();
assert.strictEqual(require('buffer'), buffer_first);
assert.strictEqual(buffer.Buffer, BufferConstructor);
assert.strictEqual(BufferConstructor.prototype, BufferPrototype);
assert.strictEqual(BufferConstructor.from('slots').toString(), 'slots');

const tls = require('tls');
const tls_first = tls;
const bundledCertificates = tls.getCACertificates('bundled');
gc();
assert.strictEqual(require('tls'), tls_first);
assert.strictEqual(tls.getCACertificates('bundled'), bundledCertificates);
assert.strictEqual(typeof tls.TLSSocket, 'function');

const readline = require('readline');
const readline_first = readline;
const readline_promises = require('readline/promises');
gc();
assert.strictEqual(require('readline'), readline_first);
assert.strictEqual(require('readline/promises'), readline_promises);
assert.strictEqual(typeof readline.createInterface, 'function');

const http = require('http');
const http_first = http;
const HttpServer = http.Server;
const IncomingMessage = http.IncomingMessage;
gc();
assert.strictEqual(require('http'), http_first);
assert.strictEqual(http.Server, HttpServer);
assert.strictEqual(http.IncomingMessage, IncomingMessage);
assert.strictEqual(typeof http.ServerResponse, 'function');

const net = require('net');
const net_first = net;
const NetSocket = net.Socket;
const NetServer = net.Server;
gc();
assert.strictEqual(require('net'), net_first);
assert.strictEqual(net.Socket, NetSocket);
assert.strictEqual(net.Server, NetServer);
assert.strictEqual(typeof net.createConnection, 'function');

const fs = require('fs');
const fs_first = fs;
const fs_promises = fs.promises;
gc();
assert.strictEqual(require('fs'), fs_first);
assert.strictEqual(fs.promises, fs_promises);
assert.strictEqual(typeof fs.statSync, 'function');

const https = require('https');
const https_first = https;
gc();
assert.strictEqual(require('https'), https_first);
assert.strictEqual(typeof https.Agent, 'function');
console.log('realm slots util ok');
