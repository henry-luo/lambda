function check(name, condition) {
  if (!condition) throw new Error('Request: ' + name);
  console.log(name + ': pass');
}
async function main() {
  const input = new Request('https://example.com/path?q=1', {
    method: 'post', headers: {'x-test': 'original'}, body: '{"value":7}'
  });
  check('constructor metadata and branding', input instanceof Request && Object.prototype.toString.call(input) === '[object Request]' && input.url === 'https://example.com/path?q=1' && input.method === 'POST');
  check('headers and inferred content type', input.headers instanceof Headers && input.headers.get('x-test') === 'original' && input.headers.get('content-type') === 'text/plain;charset=UTF-8' && !input.bodyUsed);
  const clone = input.clone();
  clone.headers.set('x-test', 'changed');
  if (typeof gc === 'function') gc();
  check('clone owns headers and body', clone !== input && clone.headers !== input.headers && input.headers.get('x-test') === 'original' && (await clone.json()).value === 7 && clone.bodyUsed && !input.bodyUsed);
  check('text body consumption', await input.text() === '{"value":7}' && input.bodyUsed);
  let rejected = false;
  try {await input.text();} catch (error) {rejected = error instanceof TypeError;}
  check('consumed body rejection', rejected);
  let cloneRejected = false;
  try {input.clone();} catch (error) {cloneRejected = error instanceof TypeError;}
  check('consumed clone rejection', cloneRejected);
  const bytes = new Uint8Array([65, 66]);
  const binary = new Request('https://example.com/', {method: 'PUT', body: bytes});
  bytes[0] = 90;
  check('binary body snapshot', new Uint8Array(await binary.arrayBuffer()).join(',') === '65,66');
  const empty = new Request('https://example.com/');
  check('empty GET body', empty.method === 'GET' && await empty.text() === '' && await empty.text() === '' && !empty.bodyUsed && !empty.signal.aborted);
  const streamed = new Request('https://example.com/', {method: 'POST', body: 'ABC'});
  const stream = streamed.body;
  const reader = stream.getReader();
  check('body stream identity and null body', stream === streamed.body && empty.body === null && !streamed.bodyUsed);
  const chunk = await reader.read();
  check('body stream consumes shared bytes', !chunk.done && chunk.value instanceof Uint8Array && new TextDecoder().decode(chunk.value) === 'ABC' && streamed.bodyUsed && (await reader.read()).done);
  const controller = new AbortController();
  const dependent = new Request('https://example.com/', {signal: controller.signal});
  const dependentClone = dependent.clone();
  const reason = {message: 'canceled'};
  if (typeof gc === 'function') gc();
  controller.abort(reason);
  check('dependent abort signals', dependent.signal !== controller.signal && dependentClone.signal !== dependent.signal && dependent.signal.aborted && dependentClone.signal.aborted && dependent.signal.reason === reason && dependentClone.signal.reason === reason);
  const already = new Request('https://example.com/', {signal: controller.signal});
  check('already-aborted source signal', already.signal.aborted && already.signal.reason === reason);
  const original = new Request('https://example.com/', {method: 'POST', headers: {'x-test': 'copied'}, body: 'transfer'});
  const transferred = new Request(original);
  check('request input transfers body', original.bodyUsed && !transferred.bodyUsed && transferred.headers.get('x-test') === 'copied' && await transferred.text() === 'transfer');
  const overrideSource = new Request('https://example.com/', {method: 'POST', headers: {'x-test': 'kept'}, body: 'original'});
  const overridden = new Request(overrideSource, {body: 'replacement'});
  check('request body override preserves metadata', overridden.headers.get('x-test') === 'kept' && await overridden.text() === 'replacement' && overrideSource.bodyUsed);
  let errors = 0;
  for (const init of [{method: 'GET', body: 'x'}, {method: 'HEAD', body: 'x'}, {method: 'TRACE'}, {method: 'bad method'}, {signal: {}}]) {
    try {new Request('https://example.com/', init);} catch (error) {if (error instanceof TypeError) errors++;}
  }
  try {new Request('https://user:password@example.com/');} catch (error) {if (error instanceof TypeError) errors++;}
  check('method body signal and URL validation', errors === 6);
  const getUrl = Object.getOwnPropertyDescriptor(Request.prototype, 'url').get;
  let receiverError = false;
  try {getUrl.call({});} catch (error) {receiverError = error instanceof TypeError;}
  check('prototype accessors validate receivers', getUrl.call(empty) === 'https://example.com/' && receiverError);
  class Custom extends Request {}
  check('Request subclass prototype', new Custom('https://example.com/') instanceof Custom);
  console.log('REQUEST_BODY_DONE');
}
main().catch(error => {console.log('REQUEST_BODY_FAILED', error.message); throw error;});
