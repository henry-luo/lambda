const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {gradientDoubleStopCases, gradientDoubleStopHtml} = require('./gradient_double_stop_fixtures.cjs');
const render = renderFixtures('gradient-double-stop');

for (const fixture of gradientDoubleStopCases) {
    test('double-position gradient: ' + fixture.name, async () => {
        const {image} = await render(fixture.name, gradientDoubleStopHtml(fixture));
        for (const [x, y] of [[1, 1], [80, 80], [158, 158]])
            assert.deepEqual(rgb(image, x, y), [1, 10, 20]);
    });
}
