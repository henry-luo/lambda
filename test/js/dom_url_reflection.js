const base = document.getElementById('base');
function getterFor(element, name) {
  let proto = Object.getPrototypeOf(element);
  while (proto) {
    const descriptor = Object.getOwnPropertyDescriptor(proto, name);
    if (descriptor) return descriptor.get;
    proto = Object.getPrototypeOf(proto);
  }
  throw Error('missing URL getter');
}
const script = document.createElement('script');
const getSrc = getterFor(script, 'src');
console.log(script.src === '');
script.src = 'bundle.js';
console.log(script.getAttribute('src') === 'bundle.js');
console.log(script.src === 'https://example.test/assets/bundle.js');
console.log(getSrc.call(script) === script.src);
console.log(new URL(script.src).origin === 'https://example.test');
base.href = 'https://other.test/root/';
console.log(script.src === 'https://other.test/root/bundle.js');
script.src = '';
console.log(script.src === 'https://other.test/root/');
script.src = 'http://[';
console.log(script.src === 'http://[');
script.removeAttribute('src');
console.log(getSrc.call(script) === '');
for (const tag of ['source', 'iframe']) {
  const element = document.createElement(tag);
  element.src = '../asset';
  console.log(element.src === 'https://other.test/asset');
  console.log(element.getAttribute('src') === '../asset');
}
for (const tag of ['a', 'area', 'link']) {
  const element = document.createElement(tag);
  console.log(element.href === '');
  element.href = '../next#section';
  console.log(element.href === 'https://other.test/next#section');
  console.log(element.getAttribute('href') === '../next#section');
  if (tag !== 'link') console.log(element.hostname === 'other.test' && element.hash === '#section');
}
console.log(base.href === 'https://other.test/root/');
console.log('DOM_URL_REFLECTION_DONE');
