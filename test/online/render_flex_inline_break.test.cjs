const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./flex_inline_break_fixtures.cjs');
const render = renderFixtures('flex-inline-break');

for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('flex-inline-break-' + index, fixtureHtml(fixture));
        if (fixture.fractionalAtomic) {
            assert.deepEqual(rgb(image, 400, 15), fixture.wrap ? [255, 0, 0] : [0, 255, 0],
                'only genuine overflow moves the second atomic box to another line');
            if (fixture.wrap) assert.deepEqual(rgb(image, 200, 50), [0, 255, 0],
                'the overflowing atomic box still wraps');
            return;
        }
        if (fixture.strut) {
            assert.deepEqual(rgb(image, 150, fixture.lineHeight + 4), [0, 255, 0],
                'following band starts after the complete ancestor line box');
            assert.deepEqual(rgb(image, 150, fixture.lineHeight + 14), [255, 255, 255],
                'following band retains its fixed height');
            return;
        }
        if (fixture.atomicLetters) {
            assert.deepEqual(rgb(image, 150, 28), [0, 255, 0], 'inline atoms share one 24px line');
            assert.deepEqual(rgb(image, 150, 40), [255, 255, 255], 'intrinsic height does not stack inline atoms');
            return;
        }
        if (fixture.centered) {
            assert.deepEqual(rgb(image, 10, 310), [255, 0, 0], 'headline keeps its centered row');
            assert.deepEqual(rgb(image, 560, 470), [0, 255, 0], 'action follows the full heading');
            return;
        }
        assert.deepEqual(rgb(image, 30, 300), [0, 255, 0], 'action remains inside the hero');
        assert.deepEqual(rgb(image, 30, 370), [255, 255, 255], 'action does not overflow below the hero');
        assert.deepEqual(rgb(image, 500, 300), [0, 0, 255], 'adjacent card shares the bottom alignment');
        assert.deepEqual(rgb(image, 500, 350), [238, 238, 238], 'card does not overflow its row');
    });
}
