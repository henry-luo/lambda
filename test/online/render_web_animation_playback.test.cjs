const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const puppeteer = require('puppeteer');
const {renderFixtures, rgb, serveFixtureResources} = require('./render_fixture.cjs');
const {captureBrowser, parseOptions} = require('./capture_snapshots.cjs');
const {pngLibrary} = require('./compare_snapshots.cjs');

const render = renderFixtures('web-animation-autoplay');
const source = `<!doctype html><style>
  body {margin:0; background:white}
  div {width:40px;height:40px;background:lime;opacity:0}
</style>
<div id="automatic"></div><div id="explicit"></div><div id="manual"></div>
<div id="ended"></div><div id="delayed"></div><div id="started"></div><div id="cancelled"></div>
<div id="overlap"></div>
<script>
const from = [{opacity:0},{opacity:1}];
const options = {duration:300, fill:'forwards'};
document.getElementById('automatic').animate(from, options);
const held = document.getElementById('explicit').animate(from, options);
held.pause(); held.currentTime = 300;
new Animation(new KeyframeEffect(document.getElementById('manual'),from,options));
document.getElementById('ended').animate(from, {duration:200});
document.getElementById('delayed').animate([{opacity:1},{opacity:0}],
    {duration:200,delay:{valueOf() { return 1500; }},fill:'backwards'});
new Animation(new KeyframeEffect(document.getElementById('started'),from,options)).play();
const cancelled = document.getElementById('cancelled').animate(from, {duration:2000,fill:'both'});
window.addEventListener('load', () => setTimeout(() => cancelled.cancel(), 350));
const overlap = document.getElementById('overlap');
overlap.animate([{opacity:1},{opacity:1}], {duration:2000,fill:'both'});
overlap.animate([{opacity:0},{opacity:0}], {duration:300,fill:'forwards'});
</script>`;

function check(image) {
    assert.deepEqual(rgb(image,20,60), [0,255,0], 'paused explicit seek');
    assert.deepEqual(rgb(image,20,100), [255,255,255], 'unplayed constructed animation');
    assert.deepEqual(rgb(image,20,20), [0,255,0], 'automatically played animation reaches its forwards fill');
    assert.deepEqual(rgb(image,20,140), [255,255,255], 'a finished no-fill effect restores author style');
    assert.deepEqual(rgb(image,20,180), [0,255,0], 'the delay uses backwards fill');
    assert.deepEqual(rgb(image,20,220), [0,255,0], 'play starts a constructed animation');
    assert.deepEqual(rgb(image,20,260), [255,255,255], 'cancellation clears an active effect');
    assert.deepEqual(rgb(image,20,300), [255,255,255], 'later filled effect stays above the still-running earlier effect');
}

test('Web Animations preserve playback, fill, pause, cancellation and effect order', async () => {
    const {image} = await render('autoplay', source, {...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS:'1000', LAMBDA_CAPTURE_LOAD_CLOCK:'elapsed'});
    check(image);
});

const origin = serveFixtureResources((req, res) => res.writeHead(200, {'Content-Type':'text/html'}).end(source));
test('Chromium matches these initial and completed animation phases at the ordinary frame', async () => {
    const dir = fs.mkdtempSync(path.resolve(__dirname,'../../temp/browser-web-animation-'));
    let browser;
    try {
        browser = await puppeteer.launch({headless:'shell', executablePath:process.env.CHROME_HEADLESS_SHELL,
            userDataDir:path.join(dir,'profile')});
        const output = path.join(dir, 'browser.png');
        const captured = await captureBrowser(browser, {url:origin()}, output,
            parseOptions(['--settle-ms=1000'],path.resolve(__dirname,'../..')));
        assert.equal(captured.ok,true,captured.error);
        check(pngLibrary(process.cwd()).sync.read(fs.readFileSync(output)));
    } finally {
        if (browser) await browser.close();
        fs.rmSync(dir, {recursive:true, force:true});
    }
});
