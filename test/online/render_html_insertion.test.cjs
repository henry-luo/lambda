const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {htmlInsertionCases, htmlInsertionHtml} = require('./html_insertion_fixtures.cjs');
const render = renderFixtures('html-insertion');

for (const kind of htmlInsertionCases) test('HTML insertion: ' + kind, async () => {
    const {image, log} = await render(kind, htmlInsertionHtml(kind));
    assert.deepEqual(loggedResult(log, 'HTML_INSERTION_RESULT'), {failures:[]});
    assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    if (kind === 'stylesheet') assert.deepEqual(rgb(image, 140, 40), [0, 0, 255]);
});
