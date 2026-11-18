const {test} = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('video-first-frame');
const video = pathToFileURL(path.resolve(__dirname, '../media/test_video.mp4')).href;

for (const autoplay of [false,true]) {
    test('video first frame is available before playback settles: '+(autoplay?'autoplay':'paused'),
        {skip:process.platform!=='darwin'},async()=>{
            const {image}=await render(autoplay?'autoplay':'paused',`<!doctype html>
<style>body{margin:0;background:white}video{display:block;width:320px;height:180px}</style>
<video src="${video}" preload="auto" ${autoplay?'autoplay muted':''}></video>`);
            // the repository's first frame has a green landscape; a placeholder is grayscale.
            let green=0;
            for(let y=0;y<180;y++)for(let x=0;x<320;x++){
                const [r,g,b]=rgb(image,x,y);
                if(g>r+15&&g>b+15)green++;
            }
            assert.ok(green>320*180*.05,`expected landscape pixels, found ${green}`);
        });
}

test('video poster is retained before playback', {skip:process.platform!=='darwin'},async()=>{
    const poster='data:image/svg+xml;base64,'+Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="180"><rect width="320" height="180" fill="blue"/></svg>').toString('base64');
    const {image}=await render('poster',`<!doctype html><style>body{margin:0}video{display:block;width:320px;height:180px}</style><video src="${video}" preload="auto" poster="${poster}"></video>`);
    assert.deepEqual(rgb(image,20,20),[0,0,255]);
});
