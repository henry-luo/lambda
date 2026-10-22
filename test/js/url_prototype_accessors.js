const url = new URL('https://user:pass@www.copilot.money:8443/path?q=1#part');
const expected = ['https://user:pass@www.copilot.money:8443/path?q=1#part',
  'https://www.copilot.money:8443', 'https:', 'user', 'pass',
  'www.copilot.money:8443', 'www.copilot.money', '8443', '/path', '?q=1', '#part'];
const names = ['href', 'origin', 'protocol', 'username', 'password', 'host',
  'hostname', 'port', 'pathname', 'search', 'hash', 'searchParams'];
for (let index = 0; index < names.length; index++) {
  const name = names[index];
  const descriptor = Object.getOwnPropertyDescriptor(URL.prototype, name);
  const value = descriptor.get.call(url);
  if (index < expected.length && value !== expected[index]) throw Error(name);
  if (name === 'searchParams' && value.get('q') !== '1') throw Error(name);
  if (!descriptor.enumerable || !descriptor.configurable ||
      typeof descriptor.set !== (name === 'origin' || name === 'searchParams' ? 'undefined' : 'function'))
    throw Error('descriptor ' + name);
  try { descriptor.get.call(Object.create(URL.prototype)); throw Error('forged ' + name); }
  catch (error) { if (!(error instanceof TypeError)) throw error; }
}
console.log(Object.getOwnPropertyNames(url).length === 0);
const getHostname = Object.getOwnPropertyDescriptor(URL.prototype, 'hostname').get;
Object.defineProperty(url, 'hostname', {value: 'shadowed'});
console.log(getHostname.call(url) === 'www.copilot.money');
Object.freeze(url);
console.log(getHostname.call(url) === 'www.copilot.money');
console.log(URL.prototype.toString.call(url) === expected[0]);
console.log(JSON.stringify(url) === JSON.stringify(expected[0]));
console.log('URL_PROTOTYPE_ACCESSORS_DONE');
