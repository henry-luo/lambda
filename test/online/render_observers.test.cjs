const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const puppeteer = require('puppeteer');
const {renderFixtures, loggedResult, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('observers');
const cases = [
    {name: 'first intersection samples without a page geometry read',
        script: `new IntersectionObserver(entries => {
            delivered++;console.log('INITIAL_INTERSECTION', entries[0].isIntersecting);
            if (entries[0].isIntersecting) entries[0].target.style.opacity = '1';
        }).observe(probe);`, color: [0, 128, 0], log: 'INITIAL_INTERSECTION true'},
    {name: 'initial resize samples the final script writes without a page geometry read',
        script: `new ResizeObserver(entries => {
            delivered++;console.log('INITIAL_RESIZE', entries[0].contentRect.width);
            if (entries[0].contentRect.width === 240) probe.style.opacity = '1';
        }).observe(probe); probe.style.width = '240px';`,
        color: [0, 128, 0], log: 'INITIAL_RESIZE 240'},
    {name: 'initial offscreen intersection is delivered without revealing the target',
        style: 'margin-top:1000px',
        script: `new IntersectionObserver(entries => {
            delivered++;console.log('INITIAL_INTERSECTION', entries[0].isIntersecting);
            if (entries[0].isIntersecting) entries[0].target.style.opacity = '1';
        }).observe(probe);`, color: [255, 255, 255], log: 'INITIAL_INTERSECTION false'},
    {name: 'disconnect before the initial sample cancels delivery',
        script: `var observer = new IntersectionObserver(() => {
            console.log('UNEXPECTED_INTERSECTION');probe.style.opacity = '1';
        });observer.observe(probe);observer.disconnect();`, color: [255, 255, 255]}
];

for (const [index, fixture] of cases.entries()) {
    test(fixture.name, async () => {
        const {image, log} = await render('observer-' + index,
            '<!doctype html><style>body{margin:0}#probe{width:200px;height:80px;' +
            'background:green;opacity:0;' + (fixture.style || '') + '}</style>' +
            '<div id="probe"></div><script>var delivered=0,probe=document.getElementById("probe");' +
            fixture.script + 'console.log("OBSERVE_SYNC",delivered);</script>');
        assert(log.includes('OBSERVE_SYNC 0'), 'observe delivered before the script finished');
        if (fixture.log) {
            assert(log.includes(fixture.log), log.slice(-2000));
            assert.equal(log.split(fixture.log).length - 1, 1, 'initial sample was delivered more than once');
        }
        assert(!log.includes('UNEXPECTED_INTERSECTION'), 'disconnected observer was delivered');
        assert.deepEqual(rgb(image, 100, 40), fixture.color);
    });
}

const settledGeometry = `<!doctype html><style>#probe{width:20px;height:40px}</style><div id="probe"></div>
<script>
const target = document.getElementById('probe');
const seen = [];
let settled = false;
const observer = new ResizeObserver(entries => {
    seen.push([settled, entries[0].contentRect.width]);
});
Promise.resolve().then(async () => {
    observer.observe(target);
    for (let count = 0; count < 20; count++) await Promise.resolve();
    target.style.width = '120px';
    settled = true;
    requestAnimationFrame(() => { target.style.width = '160px'; });
});
window.addEventListener('load', () => setTimeout(() => {
    window.result = {seen};
    console.log('SETTLED_GEOMETRY ' + JSON.stringify(window.result));
}, 200));
</script>`;

const settledResult = {seen: [[true, 160]]};

test('resize observation samples after Promise jobs and frame callbacks', async () => {
    const {log} = await render('settled-geometry', settledGeometry, {...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS:'1000', LAMBDA_CAPTURE_LOAD_CLOCK:'elapsed'});
    assert.deepEqual(loggedResult(log, 'SETTLED_GEOMETRY'), settledResult);
});

test('Chromium samples the same settled resize geometry', async () => {
    const dir = fs.mkdtempSync(path.resolve(__dirname, '../../temp/browser-observer-order-'));
    let browser;
    try {
        browser = await puppeteer.launch({headless:'shell', executablePath:process.env.CHROME_HEADLESS_SHELL,
            userDataDir:path.join(dir, 'profile')});
        const page = await browser.newPage();
        await page.setContent(settledGeometry);
        await page.waitForFunction(() => window.result, {timeout:5000});
        assert.deepEqual(await page.evaluate(() => window.result), settledResult);
    } finally {
        if (browser) await browser.close();
        fs.rmSync(dir, {recursive:true, force:true});
    }
});
