const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {gifPlaybackCases, gifPlaybackHtml} = require('./gif_playback_fixtures.cjs');
const render = renderFixtures('gif-playback');

for (const fixture of gifPlaybackCases) {
    test('GIF playback: ' + fixture.name, async () => {
        const {image} = await render(fixture.name, gifPlaybackHtml(fixture.plays),
            {...process.env, RADIANT_ONLINE_SETTLE_MS:String(fixture.ms),
                LAMBDA_POST_LOAD_SETTLE_MS:String(fixture.ms)});
        assert.deepEqual(rgb(image, 40, 40), fixture.color);
    });
}
