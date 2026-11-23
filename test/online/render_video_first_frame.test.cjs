const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures} = require('./render_fixture.cjs');
const {videoFirstFrameCases,videoFirstFrameHtml,checkVideoFirstFrame}=require('./video_first_frame_fixtures.cjs');
const render = renderFixtures('video-first-frame');

for (const fixture of videoFirstFrameCases) {
    test('video first frame: '+fixture.name,
        {skip:process.platform!=='darwin'},async()=>{
            const {image,log}=await render(fixture.name,videoFirstFrameHtml(fixture));
            assert.equal(/VIDEO_PLAY_REJECTED|event listener.*threw/.test(log),false,
                log.split('\n').filter(line=>/VIDEO_PLAY_REJECTED|threw/.test(line)).join('\n'));
            checkVideoFirstFrame(assert,fixture,image);
        });
}
