import { decodeMark, encodeMark, symbol } from './mark.mjs';

export class InspectorClient {
  constructor(transport) {
    this.transport = transport;
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = new Set();
    this.closed = false;
    this.unsubscribe = [transport.onMessage(text => {
      try { this.receive(decodeMark(text)); }
      catch (error) { this.disconnect(error.message); }
    }), transport.onClose(reason => this.disconnect(reason))];
  }

  receive(message) {
    if (message.event) {
      for (const callback of this.listeners) callback(message);
      return;
    }
    const pending = this.pending.get(message.id);
    if (!pending) throw new Error('unexpected inspector reply');
    this.pending.delete(message.id);
    clearTimeout(pending.timeout);
    if (message.error) {
      const error = new Error(message.error.message);
      error.code = message.error.code?.name;
      pending.reject(error);
    } else if (Object.hasOwn(message, 'value')) pending.resolve(message);
    else pending.reject(new Error('invalid inspector reply'));
  }

  request(operation, fields = {}) {
    if (this.closed) return Promise.reject(new Error('inspector is disconnected'));
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`inspector request timed out: ${operation}`));
      }, 30000);
      this.pending.set(id, { resolve, reject, timeout });
      const failed = error => {
        clearTimeout(timeout);
        this.pending.delete(id);
        reject(error);
      };
      try { Promise.resolve(this.transport.send(encodeMark({ ...fields, id, op: symbol(operation) }))).catch(failed); }
      catch (error) { failed(error); }
    });
  }

  onEvent(callback) { this.listeners.add(callback); return () => this.listeners.delete(callback); }
  disconnect(reason = 'detached') {
    if (this.closed) return;
    this.closed = true;
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timeout);
      pending.reject(new Error(reason));
    }
    this.pending.clear();
    for (const unsubscribe of this.unsubscribe || []) unsubscribe();
  }
}
