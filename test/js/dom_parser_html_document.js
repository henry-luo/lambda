const parsed = new DOMParser().parseFromString('<!doctype html><html lang="en"><head><title>Hello &amp; next</title><meta name="example" content="retained"></head><body><main>Body</main><script>globalThis.parser_script_ran = true</script></body></html>', 'text/html');
console.log('head metadata stays in head: ' + (parsed.head.querySelector('meta').content === 'retained' && !parsed.body.querySelector('meta')));
console.log('document title: ' + (parsed.title === 'Hello & next'));
console.log('document root attributes: ' + (parsed.documentElement.lang === 'en'));
console.log('body structure: ' + (parsed.body.children.length === 2 && parsed.body.firstElementChild.textContent === 'Body' && parsed.body instanceof HTMLBodyElement));
console.log('parsed node ownership: ' + (parsed.head.ownerDocument === parsed && parsed.body.ownerDocument === parsed));
console.log('parser document stays inert: ' + (parsed.defaultView === null && typeof parser_script_ran === 'undefined'));
