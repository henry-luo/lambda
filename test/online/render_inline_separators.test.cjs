const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./inline_separator_fixtures.cjs');
const render = renderFixtures('inline-separators');

for (const [index, fixture] of fixtures.entries()) for (const balanced of [false, true]) {
    test(fixture.name + (balanced ? ' balanced' : ' ordinary'), async () => {
        const {image} = await render('separator-' + index + '-' + balanced, fixtureHtml(fixture, balanced));
        assert.deepEqual(rgb(image, 10, 112), [0, 255, 0], 'max-content retains inter-word spaces and fits one line');
        assert.deepEqual(rgb(image, 10, 220), [255, 255, 255], 'no extra line shifts the following block');
    });
}
