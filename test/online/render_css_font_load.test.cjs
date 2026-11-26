const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {test} = require('node:test');
const puppeteer = require('puppeteer');
const {loggedResult, renderFixtures, rgb, serveFixtureResources} = require('./render_fixture.cjs');

const font = fs.readFileSync(path.resolve(__dirname, '../../lambda-input/test/fixtures/fonts/FreeSans.woff'));
const source = `<!doctype html>
    <style>
        @font-face {
            font-family: Probe Face;
            src: url(/invalid.woff) format('woff'), url(/valid.woff) format('woff');
            unicode-range: U+0041-005A;
        }
        @font-face { font-family: Broken Face; src: url(/broken.woff) format('woff'); }
        body { margin: 0; background: white }
        #ready { width: 40px; height: 40px; background: red }
        #glyphs { display: inline-block; font: 30px monospace; white-space: nowrap }
    </style>
    <div id="ready"></div><span id="glyphs">WWWWWWWWWW</span>
    <script>
        const fontSpec = 'normal 400 30px "Probe Face"';
        Promise.all([
            document.fonts.load(fontSpec, 'AZ'),
            document.fonts.load(fontSpec, 'a'),
            document.fonts.load('30px NoSuchFace', 'A'),
            document.fonts.load('initial').then(() => 'fulfilled', error => error.name),
            document.fonts.load('30px "Broken Face"', 'A').then(() => 'fulfilled', error => error.name),
        ]).then(async ([matched, excluded, unknown, syntax, failed]) => {
            const glyphs = document.getElementById('glyphs');
            const fallbackWidth = glyphs.getBoundingClientRect().width;
            glyphs.style.fontFamily = '"Probe Face", monospace';
            const face = matched[0];
            window.fontResult = {
                count: matched.length, status: face?.status, branded: face instanceof FontFace,
                loadedSelf: !!face && await face.loaded === face,
                geometryChanged: Math.abs(glyphs.getBoundingClientRect().width - fallbackWidth) > 1,
                excluded: excluded.length, unknown: unknown.length, syntax, failed,
            };
            document.getElementById('ready').style.background = 'lime';
            console.log('CSS font ready ' + JSON.stringify(window.fontResult));
        }, error => console.log('CSS font error ' + error.name));
    </script>`;

const expected = {
    count: 1, status: 'loaded', branded: true, loadedSelf: true, geometryChanged: true,
    excluded: 0, unknown: 0, syntax: 'SyntaxError', failed: 'NetworkError',
};
const render = renderFixtures('css-font-load');
const origin = serveFixtureResources((req, res) => {
    if (req.url === '/valid.woff') {
        setTimeout(() => res.writeHead(200, {'Content-Type': 'font/woff'}).end(font), 150);
    } else if (req.url === '/invalid.woff') {
        res.writeHead(200, {'Content-Type': 'font/woff'}).end('invalid font bytes');
    } else if (req.url === '/broken.woff') {
        res.writeHead(404, {'Content-Type': 'text/plain'}).end('missing');
    } else {
        res.writeHead(200, {'Content-Type': 'text/html'}).end(source);
    }
});

test('CSS font loading reports a usable fallback face, Unicode exclusions and failures', async () => {
    const {log, image} = await render('css-font-matches', {url: origin() + '/'}, {...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS: '1000', LAMBDA_CAPTURE_LOAD_CLOCK: 'elapsed'});
    assert.deepEqual(rgb(image, 20, 20), [0, 255, 0], 'font load promise settled');
    assert.deepEqual(loggedResult(log, 'CSS font ready'), expected);
});

test('Chromium agrees on CSS font loading and invalid sources', async () => {
    const scratch = fs.mkdtempSync(path.resolve(__dirname, '../../temp/browser-css-font-load-'));
    let browser;
    try {
        browser = await puppeteer.launch({headless: 'shell', executablePath: process.env.CHROME_HEADLESS_SHELL,
            userDataDir: path.join(scratch, 'profile')});
        const page = await browser.newPage();
        await page.goto(origin() + '/');
        await page.waitForFunction(() => window.fontResult, {timeout: 5000});
        assert.deepEqual(await page.evaluate(() => window.fontResult), expected);
    } finally {
        if (browser) await browser.close();
        fs.rmSync(scratch, {recursive: true, force: true});
    }
});
