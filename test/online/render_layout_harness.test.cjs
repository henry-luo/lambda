const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult} = require('./render_fixture.cjs');

const render = renderFixtures('layout-harness');

test('layout harness inequality assertions allow fixture cleanup and reject equal values', async () => {
    const {log} = await render('inheritance-cleanup', `<!doctype html>
        <script src="/resources/testharness.js"></script>
        <div id="container"><div id="target"></div></div>
        <script>
            const container = document.getElementById('container');
            const target = document.getElementById('target');
            test(() => {
                container.style.display = 'initial';
                target.style.display = 'unset';
                assert_not_equals(getComputedStyle(container).display, 'table');
                assert_not_equals(getComputedStyle(target).display, 'table');
                container.style.display = 'table';
                target.style.display = 'inherit';
                assert_equals(getComputedStyle(target).display, 'table');
                container.style.display = '';
                target.style.display = '';
            });
            let failures = 0;
            for (const value of [1, 'table', null, undefined, NaN]) {
                try { assert_not_equals(value, value); }
                catch (error) { failures++; }
            }
            assert_not_equals(0, -0);
            assert_not_equals({}, {});
            console.log('HARNESS_RESULT', JSON.stringify({
                container: getComputedStyle(container).display,
                target: getComputedStyle(target).display,
                failures
            }));
        </script>`);
    assert.deepEqual(loggedResult(log, 'HARNESS_RESULT'), {
        container: 'block', target: 'block', failures: 5
    });
});
