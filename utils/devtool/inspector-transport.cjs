const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');
const path = require('node:path');

const MAX_BYTES = 8 * 1024 * 1024;

class InspectorTransport extends EventEmitter {
  constructor(projectRoot, document, { headless = true, executable } = {}) {
    super();
    this.child = spawn(executable || process.env.LAMBDA_DEVTOOLS_EXE || path.join(projectRoot, 'lambda.exe'),
      ['--no-log', 'devtools', document, '--pipe', '3', ...(headless ? ['--headless'] : [])],
      { cwd: projectRoot, stdio: ['ignore', 'pipe', 'pipe', 'pipe'] });
    this.channel = this.child.stdio[3];
    this.header = Buffer.alloc(4);
    this.headerSize = 0;
    this.payload = null;
    this.payloadSize = 0;
    this.ended = false;
    this.child.stdout.on('data', data => this.emit('output', data.toString()));
    this.child.stderr.on('data', data => this.emit('output', data.toString()));
    this.child.on('error', error => this.finish(error.message));
    this.child.on('exit', (code, signal) => this.finish(`target exited (${signal || code})`));
    this.channel.on('data', bytes => {
      try { this.receive(bytes); }
      catch (error) { this.finish(error.message); this.channel.destroy(); }
    });
    this.channel.on('error', error => this.finish(error.message));
    this.channel.on('end', () => this.finish('target disconnected'));
  }

  receive(bytes) {
    let offset = 0;
    while (offset < bytes.length) {
      if (!this.payload) {
        const take = Math.min(4 - this.headerSize, bytes.length - offset);
        bytes.copy(this.header, this.headerSize, offset, offset + take);
        this.headerSize += take;
        offset += take;
        if (this.headerSize !== 4) continue;
        const length = this.header.readUInt32BE();
        if (!length || length > MAX_BYTES) throw new RangeError('invalid inspector frame length');
        this.payload = Buffer.allocUnsafe(length);
        this.payloadSize = 0;
      }
      const take = Math.min(this.payload.length - this.payloadSize, bytes.length - offset);
      bytes.copy(this.payload, this.payloadSize, offset, offset + take);
      this.payloadSize += take;
      offset += take;
      if (this.payloadSize === this.payload.length) {
        const text = new TextDecoder('utf-8', { fatal: true }).decode(this.payload);
        this.payload = null;
        this.headerSize = 0;
        this.emit('message', text);
      }
    }
  }

  send(text) {
    if (this.ended) throw new Error('inspector is disconnected');
    const payload = Buffer.from(text, 'utf8');
    if (!payload.length || payload.length > MAX_BYTES || this.channel.writableLength + payload.length + 4 > MAX_BYTES)
      throw new RangeError('inspector message/queue limit exceeded');
    const header = Buffer.alloc(4);
    header.writeUInt32BE(payload.length);
    this.channel.write(Buffer.concat([header, payload]));
  }

  onMessage(callback) { this.on('message', callback); return () => this.off('message', callback); }
  onClose(callback) { this.on('closed', callback); return () => this.off('closed', callback); }
  finish(reason) {
    if (this.ended) return;
    this.ended = true;
    this.emit('closed', reason);
  }
  close() { this.channel.end(); this.finish('detached'); }
  stop() { this.close(); if (this.child.exitCode === null) this.child.kill(); }
}

module.exports = { InspectorTransport, MAX_BYTES };
