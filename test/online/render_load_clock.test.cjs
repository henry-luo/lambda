const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),puppeteer=require('puppeteer');
const {renderFixtures,rgb,loggedResult,serveFixtureResources}=require('./render_fixture.cjs');
const {serveLoadClock}=require('./load_clock_fixtures.cjs');
const {captureBrowser,parseOptions}=require('./capture_snapshots.cjs');
const {pngLibrary}=require('./compare_snapshots.cjs');
const render=renderFixtures('load-clock'),origin=serveFixtureResources(serveLoadClock);

function checkFrame(image) {
    assert.deepEqual(rgb(image,40,40),[0,255,0],'load time counts toward the 1800ms script timer');
    const fade=rgb(image,40,120);
    assert.ok(Math.abs(fade[0]-128)<20&&Math.abs(fade[1]-128)<20&&fade[2]===0,
        'CSS animation age includes the one-second settlement, excluding loading: '+fade);
    assert.deepEqual(rgb(image,40,200),[255,0,0],'authored CSS pause stays paused');
}

test('elapsed loading carries timers into the fixed post-load frame without aging CSS',async()=>{
    const {image,log}=await render('elapsed',{url:origin()},{...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS:'1000',LAMBDA_CAPTURE_LOAD_CLOCK:'elapsed'});
    checkFrame(image);
    const timing=loggedResult(log,'LOAD_CLOCK_RESULT');
    assert.ok(timing.load>=1100,JSON.stringify(timing));
    assert.ok(Math.abs(timing.dateLoad-timing.load)<5,'Date and performance advance together during load');
    assert.ok(Math.abs(timing.settle-500)<1,'pending timers retain their deadline across clock migration');
    assert.ok(Math.abs(timing.dateSettle-timing.settle)<2,'Date and performance stay continuous after freezing');
    assert.ok(timing.originError<2,'timeOrigin + now remains consistent with Date.now');
});

for(const mode of ['frozen','elapsed'])test('four-second anti-flicker timer with '+mode+' loading',async()=>{
    const {image}=await render('anti-flicker-'+mode,{url:origin()+'/anti-flicker'},{...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS:'1000',LAMBDA_CAPTURE_LOAD_CLOCK:mode});
    assert.deepEqual(rgb(image,40,40),mode==='elapsed'?[0,255,0]:[255,255,255]);
});

for(const fixture of [
    {name:'Chromium reference keeps the same elapsed-load and paused-CSS policy',route:'',check:checkFrame},
    {name:'Chromium four-second anti-flicker reference',route:'/anti-flicker',
        check:image=>assert.deepEqual(rgb(image,40,40),[0,255,0])},
])test(fixture.name,async()=>{
    const scratch=fs.mkdtempSync(path.resolve(__dirname,'../../temp/browser-load-clock-'));
    let browser;
    try {
        browser=await puppeteer.launch({headless:'shell',executablePath:process.env.CHROME_HEADLESS_SHELL,
            userDataDir:path.join(scratch,'profile')});
        const output=path.join(scratch,'browser.png');
        const result=await captureBrowser(browser,{url:origin()+fixture.route},output,
            parseOptions(['--settle-ms=1000'],path.resolve(__dirname,'../..')));
        assert.equal(result.ok,true,result.error);
        fixture.check(pngLibrary(process.cwd()).sync.read(fs.readFileSync(output)));
    } finally {
        if(browser)await browser.close();
        fs.rmSync(scratch,{recursive:true,force:true});
    }
});
