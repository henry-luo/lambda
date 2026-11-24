const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult} = require('./render_fixture.cjs');

const render = renderFixtures('fragment-text');
for (const {name, setter, source, token} of [
    {name: 'script innerHTML preserves end tags', setter: 'innerHTML',
        source: "window.fragmentToken='</script>';window.fragmentDone=true;", token: '</script>'},
    {name: 'script innerHTML preserves embedded markup and references', setter: 'innerHTML',
        source: "window.fragmentToken='<b>&amp;</b>';window.fragmentDone=true;", token: '<b>&amp;</b>'},
    {name: 'script textContent control', setter: 'textContent',
        source: "window.fragmentToken='</script>';window.fragmentDone=true;", token: '</script>'},
]) {
    test(name, async () => {
        // escape the outer HTML script's terminator; reconstruct the exact source at runtime.
        const encoded = JSON.stringify(source).replaceAll('<', '\\u003c');
        const {log} = await render(name.replaceAll(' ', '-'), `<!doctype html><body><script>
            var source = ${encoded};
            var script = document.createElement('script');
            script.${setter} = source;
            var before = script.textContent;
            document.body.appendChild(script);
            console.log('FRAGMENT_TEXT_RESULT', JSON.stringify({same: before === source,
                done: window.fragmentDone === true, token: window.fragmentToken}));
        </script>`);
        const result = loggedResult(log, 'FRAGMENT_TEXT_RESULT');
        assert.deepEqual(result, {same: true, done: true, token});
    });
}
