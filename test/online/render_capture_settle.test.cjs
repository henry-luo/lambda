const {test, before, after} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const puppeteer = require('puppeteer');
const {DEFAULT_CAPTURE_SETTLE_MS, parseOptions, runLambdaRender, captureBrowser, updateRow} = require('./capture_snapshots.cjs');
const {pngLibrary} = require('./compare_snapshots.cjs');
const {rgb} = require('./render_fixture.cjs');

const root = path.resolve(__dirname, '../..');
const scratch = fs.mkdtempSync(path.join(root, 'temp/render-capture-settle-'));
const PNG = pngLibrary(root);
let server, browser, url;
const source = `<!doctype html><style>body{margin:0;background:white}
#box{width:200px;height:100px;background:red}</style><div id="box"></div><script>
setTimeout(()=>document.getElementById('box').style.background='lime',80);
setTimeout(()=>document.getElementById('box').style.background='blue',3000);
</script>`;
const cssSource = `<!doctype html><style>body{margin:0;background:white}
#box{width:200px;height:100px;background:red;animation:reveal 80ms linear forwards}
@keyframes reveal{from{background-color:red}to{background-color:lime}}
</style><div id="box"></div>`;
const idleCssCases = [
    {name: 'finished width survives a layout checkpoint', css: `#box{width:100px;height:100px;background:red;animation:grow 80ms linear forwards}
        @keyframes grow{to{width:200px}}`, sampleX: 150, expected: [255, 0, 0]},
    {name: 'finished opacity survives a layout checkpoint', css: `#box{width:200px;height:100px;background:red;opacity:0;animation:show 80ms linear forwards}
        @keyframes show{to{opacity:1}}`, sampleX: 50, expected: [255, 0, 0]},
    {name: 'no-fill CSS restores its underlying color', css: `#box{width:200px;height:100px;background:red;animation:reveal 80ms linear}
        @keyframes reveal{to{background-color:lime}}`, sampleX: 50, expected: [255, 0, 0]},
    {name: 'no-fill CSS restores default opacity', css: `#box{width:200px;height:100px;background:red;animation:hide 80ms linear}
        @keyframes hide{from{opacity:1}to{opacity:0}}`, sampleX: 50, expected: [255, 0, 0]},
    {name: 'no-fill CSS restores declared opacity', css: `#box{width:200px;height:100px;background:red;opacity:.4;animation:hide 80ms linear}
        @keyframes hide{from{opacity:1}to{opacity:0}}`, sampleX: 50, expected: [255, 153, 153]},
];
const shorthandCases = [
    {name: 'background color shorthand', endpoint: 'background:lime', expected: [0, 255, 0]},
    {name: 'background shorthand variables retain their grammar', endpoint: 'background:var(--paint)', expected: [0, 255, 0]},
    {name: 'omitted shorthand color resets to transparent', endpoint: 'background:none', expected: [255, 255, 255]},
    {name: 'later longhand overrides shorthand', endpoint: 'background:lime;background-color:blue', expected: [0, 0, 255]},
    {name: 'later shorthand overrides longhand', endpoint: 'background-color:blue;background:lime', expected: [0, 255, 0]},
    {name: 'important keyframe shorthand remains ignored', endpoint: 'background:lime;background:blue!important', expected: [0, 255, 0]},
];
function shorthandSource(fixture) {
    return `<!doctype html><style>body{margin:0;background:white}
#box{--paint:lime none;width:200px;height:100px;background:red;animation:reveal 80ms linear forwards}
@keyframes reveal{from{background-color:red}to{${fixture.endpoint}}}
</style><div id="box"></div>`;
}
const webAnimationSource = `<!doctype html><style>body{margin:0;background:white}
#box{width:200px;height:100px;background:red}</style><div id="box"></div><script>
const animation = document.getElementById('box').animate([{background:'red'},{background:'lime'}],{duration:80,fill:'forwards'});
animation.pause(); animation.currentTime = 80;
</script>`;

before(async () => {
    server = http.createServer((request, response) => {
        const index = /^\/shorthand-(\d+)$/.exec(request.url);
        const idle = /^\/idle-css-(\d+)$/.exec(request.url);
        const html = idle ? `<!doctype html><style>body{margin:0;background:white}${idleCssCases[Number(idle[1])].css}</style><div id=box></div><script>window.startupReady=true</script>`
            : request.url === '/web-animation-longhand'
            ? webAnimationSource.replaceAll("background:'", "backgroundColor:'")
            : request.url === '/css-idle-script' ? cssSource + '<script>window.startupReady=true</script>'
            : index ? shorthandSource(shorthandCases[Number(index[1])])
            : request.url === '/css' ? cssSource : request.url === '/web-animation' ? webAnimationSource : source;
        response.writeHead(200, {'Content-Type': 'text/html'}).end(html);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    url = `http://127.0.0.1:${server.address().port}/`;
    browser = await puppeteer.launch({headless: true,
        executablePath: process.env.CHROME_HEADLESS_SHELL || puppeteer.executablePath(),
        userDataDir: path.join(scratch, 'browser-profile')});
});
after(async () => {
    if (browser) await browser.close();
    if (server) await new Promise(resolve => server.close(resolve));
    fs.rmSync(scratch, {recursive: true, force: true});
});

async function native(label, args, env = process.env, route = '', defaultSettleMs) {
    const options = parseOptions(args, root, env, defaultSettleMs), output = path.join(scratch, label + '.png');
    const result = await runLambdaRender({label, url: url + route}, output, options, scratch);
    assert.equal(result.ok, true, result.error);
    return {result, options, image: PNG.sync.read(fs.readFileSync(output))};
}

test('requested capture frame matches browser and excludes later timers', async () => {
    const {result, options, image} = await native('settled', ['--settle-ms=250']);
    assert.deepEqual(rgb(image, 50, 50), [0, 255, 0], '80ms task ran, 3000ms task did not');
    const output = path.join(scratch, 'browser.png');
    const reference = await captureBrowser(browser, {url}, output, options);
    assert.equal(reference.ok, true, reference.error);
    assert.deepEqual(rgb(PNG.sync.read(fs.readFileSync(output)), 50, 50), [0, 255, 0]);
    const row = {};
    updateRow(row, 'lambda', result);
    updateRow(row, 'browser', reference);
    assert.equal(row.lambda_settle_ms, 250);
    assert.equal(row.browser_settle_ms, 250);
});

test('online default selects the same one-second frame for both captures', async () => {
    const env = {...process.env};
    delete env.RADIANT_ONLINE_SETTLE_MS;
    delete env.LAMBDA_POST_LOAD_SETTLE_MS;
    const {result, options, image} = await native('online-default', [], env, '', DEFAULT_CAPTURE_SETTLE_MS);
    assert.equal(options.settleMs, 1000);
    assert.deepEqual(rgb(image, 50, 50), [0, 255, 0]);
    const output = path.join(scratch, 'online-default-browser.png');
    const reference = await captureBrowser(browser, {url}, output, options);
    assert.equal(reference.ok, true, reference.error);
    assert.deepEqual(rgb(PNG.sync.read(fs.readFileSync(output)), 50, 50), [0, 255, 0]);
    assert.equal(result.settleMs, reference.settleMs);
});

test('explicit zero overrides a host settling window', async () => {
    const {result, image} = await native('frozen', ['--settle-ms=0'],
        {...process.env, LAMBDA_POST_LOAD_SETTLE_MS: '250'});
    assert.equal(result.settleMs, 0);
    assert.deepEqual(rgb(image, 50, 50), [255, 0, 0]);
});

test('script-free CSS animation uses the explicitly selected frame', async () => {
    const {options, image} = await native('css-settled', ['--settle-ms=250'], process.env, 'css');
    assert.deepEqual(rgb(image, 50, 50), [0, 255, 0]);
    const output = path.join(scratch, 'css-browser.png');
    const reference = await captureBrowser(browser, {url: url + 'css'}, output, options);
    assert.equal(reference.ok, true, reference.error);
    assert.deepEqual(rgb(PNG.sync.read(fs.readFileSync(output)), 50, 50), [0, 255, 0]);
});

test('script-free CSS animation keeps the default initial frame', async () => {
    const env = {...process.env};
    delete env.RADIANT_ONLINE_SETTLE_MS;
    delete env.LAMBDA_POST_LOAD_SETTLE_MS;
    const {image} = await native('css-initial', [], env, 'css');
    assert.deepEqual(rgb(image, 50, 50), [255, 0, 0]);
});

test('CSS animation with an idle script reaches the selected frame', async () => {
    const {options, image} = await native('css-idle-script', ['--settle-ms=250'], process.env, 'css-idle-script');
    assert.deepEqual(rgb(image, 50, 50), [0, 255, 0]);
    const output = path.join(scratch, 'css-idle-script-browser.png');
    const reference = await captureBrowser(browser, {url: url + 'css-idle-script'}, output, options);
    assert.equal(reference.ok, true, reference.error);
    assert.deepEqual(rgb(PNG.sync.read(fs.readFileSync(output)), 50, 50), [0, 255, 0]);
});

for (const [index, fixture] of idleCssCases.entries()) {
    test(fixture.name, async () => {
        const route = 'idle-css-' + index;
        const {options, image} = await native(route, ['--settle-ms=250'], process.env, route);
        assert.deepEqual(rgb(image, fixture.sampleX, 50), fixture.expected);
        const output = path.join(scratch, route + '-browser.png');
        const reference = await captureBrowser(browser, {url: url + route}, output, options);
        assert.equal(reference.ok, true, reference.error);
        assert.deepEqual(rgb(PNG.sync.read(fs.readFileSync(output)), fixture.sampleX, 50), fixture.expected);
    });
}

test('idle script preserves the default initial CSS frame', async () => {
    const env = {...process.env};
    delete env.RADIANT_ONLINE_SETTLE_MS;
    delete env.LAMBDA_POST_LOAD_SETTLE_MS;
    const {image} = await native('css-idle-initial', [], env, 'css-idle-script');
    assert.deepEqual(rgb(image, 50, 50), [255, 0, 0]);
});

for (const [index, fixture] of shorthandCases.entries()) {
    test('keyframe ' + fixture.name, async () => {
        const {options, image} = await native('shorthand-' + index, ['--settle-ms=250'], process.env, 'shorthand-' + index);
        assert.deepEqual(rgb(image, 50, 50), fixture.expected);
        const output = path.join(scratch, 'shorthand-browser-' + index + '.png');
        const reference = await captureBrowser(browser, {url: url + 'shorthand-' + index}, output, options);
        assert.equal(reference.ok, true, reference.error);
        assert.deepEqual(rgb(PNG.sync.read(fs.readFileSync(output)), 50, 50), fixture.expected);
    });
}

test('Web Animations background shorthand uses the same keyframe parser', async () => {
    const {options, image} = await native('web-animation', ['--settle-ms=250'], process.env, 'web-animation');
    assert.deepEqual(rgb(image, 50, 50), [0, 255, 0]);
    const output = path.join(scratch, 'web-animation-browser.png');
    const reference = await captureBrowser(browser, {url: url + 'web-animation'}, output, options);
    assert.equal(reference.ok, true, reference.error);
    assert.deepEqual(rgb(PNG.sync.read(fs.readFileSync(output)), 50, 50), [0, 255, 0]);
});

test('Web Animations longhand explicit seek remains supported', async () => {
    const {image} = await native('web-animation-longhand', ['--settle-ms=250'], process.env, 'web-animation-longhand');
    assert.deepEqual(rgb(image, 50, 50), [0, 255, 0]);
});

test('unspecified window preserves the original frozen export default', async () => {
    const env = {...process.env};
    delete env.RADIANT_ONLINE_SETTLE_MS;
    delete env.LAMBDA_POST_LOAD_SETTLE_MS;
    const {result, image} = await native('default', [], env);
    assert.equal(result.settleMs, '');
    assert.deepEqual(rgb(image, 50, 50), [255, 0, 0]);
});

test('capture window rejects invalid values and honors explicit option precedence', () => {
    for (const value of ['', '-1', '1.5', 'NaN', 'Infinity']) {
        assert.throws(() => parseOptions(['--settle-ms=' + value], root, {}), /nonnegative integer/);
    }
    assert.equal(parseOptions(['--settle-ms=0'], root,
        {RADIANT_ONLINE_SETTLE_MS: '250'}).settleMs, 0);
    assert.throws(() => parseOptions(['--settle-ms=30000'], root, {}), /navigation deadline/);
});
