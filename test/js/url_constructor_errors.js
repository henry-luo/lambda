for (const input of ['', '.', '../child', 'http://[']) {
  let rejected = false;
  try { new URL(input); } catch (error) { rejected = error instanceof TypeError; }
  console.log(rejected);
}
for (const base of ['', 'relative', 'http://[', null]) {
  let rejected = false;
  try { new URL('https://example.com/path', base); }
  catch (error) { rejected = error instanceof TypeError; }
  console.log(rejected);
}
console.log(new URL('', 'https://example.com/a/').href === 'https://example.com/a/');
console.log(new URL('.', 'https://example.com/a/').href === 'https://example.com/a/');
console.log(new URL('https://example.com/', undefined).href === 'https://example.com/');
const sentinel = {};
try { new URL({toString() { throw sentinel; }}, 'invalid'); }
catch (error) { console.log(error === sentinel); }
try { new URL('https://example.com/', {toString() { throw sentinel; }}); }
catch (error) { console.log(error === sentinel); }
console.log('URL_CONSTRUCTOR_ERRORS_DONE');
