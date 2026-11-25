const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),puppeteer=require('puppeteer');
const {renderFixtures,loggedResult,serveFixtureResources}=require('./render_fixture.cjs');
const {serveLoadClock}=require('./load_clock_fixtures.cjs');
const {captureBrowser,parseOptions}=require('./capture_snapshots.cjs');
const render=renderFixtures('load-frames'),origin=serveFixtureResources(serveLoadClock);

function checkFrames(log,mode,browser=false) {
    const result=loggedResult(log,'LOAD_FRAMES_RESULT');
    console.log('LOAD_FRAMES_CHECK',mode,browser?'Chromium':'native',JSON.stringify(result));
    assert.equal(result.reentrant,false,'frames wait for the script and its microtasks');
    assert.equal(result.canceled,false,'canceled callbacks stay canceled');
    if(mode==='frozen')assert.equal(result.before,0,'frozen capture retains its load boundary');
    else {
        assert.ok(result.load>=1100,'the admitted resource still blocks window.load');
        assert.ok(result.before>0&&result.first<result.load/2,'a frame runs while the resource is loading');
        assert.equal(result.nestedBeforeLoad,true,'a callback can request another frame before load');
        // CDP's CSS playback pause also freezes Chromium's document-timeline rAF timestamp.
        if(!browser)assert.ok(result.clockError<2,'native rAF and performance.now share the frame clock');
    }
}

for(const blocker of ['script','image'])for(const mode of ['elapsed','frozen'])
test('native '+mode+' frames during '+blocker+' loading',async()=>{
    const {log}=await render(blocker+'-'+mode,{url:origin()+'/?frames='+blocker+'&clock='+mode},{...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS:'1000',LAMBDA_CAPTURE_LOAD_CLOCK:mode});
    checkFrames(log,mode);
});

for(const blocker of ['script','image'])test('Chromium frames during '+blocker+' loading',async()=>{
    const scratch=fs.mkdtempSync(path.resolve(__dirname,'../../temp/browser-load-frames-'));
    let browser;
    const log=[];
    try {
        browser=await puppeteer.launch({headless:'shell',executablePath:process.env.CHROME_HEADLESS_SHELL,
            userDataDir:path.join(scratch,'profile')});
        const host={newPage:async()=>{
            const page=await browser.newPage();page.on('console',message=>log.push(message.text()));return page;
        }};
        const result=await captureBrowser(host,{url:origin()+'/?frames='+blocker},path.join(scratch,'browser.png'),
            parseOptions(['--settle-ms=1000'],path.resolve(__dirname,'../..')));
        assert.equal(result.ok,true,result.error);
        checkFrames(log.join('\n'),'elapsed',true);
    } finally {
        if(browser)await browser.close();
        fs.rmSync(scratch,{recursive:true,force:true});
    }
});
