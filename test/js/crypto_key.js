(async function () {
  console.log(typeof CryptoKey === 'function');
  const raw = new TextEncoder().encode('key');
  const data = new TextEncoder().encode('message');
  const key = await crypto.subtle.importKey('raw', raw,
    {name:'HMAC', hash:'SHA-256'}, true, ['sign', 'verify']);
  console.log(key instanceof CryptoKey);
  console.log(Object.prototype.toString.call(key) === '[object CryptoKey]');
  console.log(Object.getOwnPropertyNames(key).length === 0);
  console.log(key.type === 'secret' && key.extractable === true);
  console.log(key.algorithm.name === 'HMAC' && key.algorithm.hash.name === 'SHA-256');
  console.log(key.algorithm.length === 24);
  console.log(key.usages.join(',') === 'sign,verify');
  const getType = Object.getOwnPropertyDescriptor(CryptoKey.prototype, 'type').get;
  console.log(getType.call(key) === 'secret');
  for (const receiver of [{}, Object.create(CryptoKey.prototype), new Proxy(key, {})]) {
    try { getType.call(receiver); console.log(false); }
    catch (error) { console.log(error instanceof TypeError); }
  }
  for (const construct of [() => CryptoKey(), () => new CryptoKey()]) {
    try { construct(); console.log(false); }
    catch (error) { console.log(error instanceof TypeError); }
  }
  Object.freeze(key);
  raw.fill(0);
  const signature = await crypto.subtle.sign('HMAC', key, data);
  const hex = Array.from(new Uint8Array(signature), byte => byte.toString(16).padStart(2, '0')).join('');
  console.log(hex === '6e9ef29b75fffc5b7abae527d58fdadb2fe42e7219011976917343065f58ed4a');
  console.log(await crypto.subtle.verify({name:'HMAC'}, key, signature, data));
  new Uint8Array(signature)[0] ^= 1;
  console.log(await crypto.subtle.verify('HMAC', key, signature, data) === false);
  const exported = new Uint8Array(await crypto.subtle.exportKey('raw', key));
  console.log(exported.length === 3 && exported[0] === 107 && exported[2] === 121);
  exported.fill(0);
  console.log((await crypto.subtle.verify('HMAC', key,
    await crypto.subtle.sign('HMAC', key, data), data)) === true);
  const restricted = await crypto.subtle.importKey('raw', new Uint8Array([1, 2, 3]),
    {name:'HMAC', hash:'SHA-256'}, false, ['sign']);
  restricted.usages.push('verify');
  try { await crypto.subtle.exportKey('raw', restricted); console.log(false); }
  catch (error) { console.log(error instanceof DOMException && /^InvalidAccess/.test(error.name)); }
  try { await crypto.subtle.verify('HMAC', restricted, signature, data); console.log(false); }
  catch (error) { console.log(error instanceof DOMException && /^InvalidAccess/.test(error.name)); }
  const changing = new Uint8Array([4, 5, 6]);
  const snapshot = await crypto.subtle.importKey('raw', changing,
    {get name() { changing.fill(0); return 'HMAC'; }, hash:'SHA-256'}, true, ['sign']);
  console.log(new Uint8Array(await crypto.subtle.exportKey('raw', snapshot)).join(',') === '0,0,0');
  const resizable = new ArrayBuffer(16, {maxByteLength:32});
  const view = new DataView(resizable, 8, 8);
  resizable.resize(4);
  try { await crypto.subtle.importKey('raw', view, {name:'HMAC', hash:'SHA-256'}, true, ['sign']); console.log(false); }
  catch (error) { console.log(error instanceof TypeError); }
  const exceptionConstructor = DOMException;
  console.log(Object.getPrototypeOf(exceptionConstructor.prototype) === Error.prototype);
  const exception = new exceptionConstructor('message', 'AbortError');
  console.log(exception instanceof exceptionConstructor && exception.code === 20 &&
    Object.prototype.toString.call(exception) === '[object DOMException]');
  class DerivedException extends exceptionConstructor {}
  console.log(new DerivedException('derived', 'DataError') instanceof DerivedException);
  globalThis.DOMException = function Replacement() {};
  try { await crypto.subtle.exportKey('raw', restricted); console.log(false); }
  catch (error) { console.log(error instanceof exceptionConstructor && error instanceof Error &&
    Object.getPrototypeOf(error) === exceptionConstructor.prototype); }
  finally { globalThis.DOMException = exceptionConstructor; }
  const token = {};
  try { await crypto.subtle.importKey('raw', data, {get name() { throw token; }}, false, ['sign']); console.log(false); }
  catch (error) { console.log(error === token); }
  console.log('CRYPTO_KEY_DONE');
})();
