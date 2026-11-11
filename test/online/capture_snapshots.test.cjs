const {test, before, after} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const http = require('http');
const puppeteer = require('puppeteer');
const {parseCsv, serializeCsv} = require('../../utils/online_pages.cjs');
const {defaultRenderTimeoutMs, selectWork, readLambdaDiagnostics,
    captureBrowser, hasPng, promoteSnapshot} = require('./capture_snapshots.cjs');

const root = path.resolve(__dirname, '../..');
fs.mkdirSync(path.join(root, 'temp'), {recursive: true});
const scratch = fs.mkdtempSync(path.join(root, 'temp/online-capture-test-'));
const tinyPng = Buffer.from(
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1cAAAAASUVORK5CYII=',
    'base64');
let server;
let browser;
let origin;

before(async () => {
    server = http.createServer((request, response) => {
        if (request.url === '/denied') {
            response.writeHead(403).end('denied');
        } else if (request.url === '/redirect') {
            response.setHeader('Content-Type', 'text/html');
            response.end('<script>location.replace("/denied")</script>');
        } else if (request.url === '/poll') {
            setTimeout(() => response.end('ok'), 100);
        } else {
            response.setHeader('Content-Type', 'text/html');
            response.end('<body style="background:green">loaded<script>' +
                'setInterval(()=>fetch("/poll"),50);</script></body>');
        }
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    origin = `http://127.0.0.1:${server.address().port}`;
    const executablePath = process.env.CHROME_HEADLESS_SHELL;
    browser = await puppeteer.launch({...(executablePath ? {executablePath} : {}),
        headless: executablePath ? true : 'shell', userDataDir: path.join(scratch, 'chromium')});
});

after(async () => {
    if (browser) await browser.close();
    if (server) await new Promise((resolve) => server.close(resolve));
    fs.rmSync(scratch, {recursive: true, force: true});
});

test('loaded pages with recurring network traffic still produce a browser PNG', async () => {
    const output = path.join(scratch, 'loaded.png');
    const result = await captureBrowser(browser, {url: origin}, output, {navigationTimeoutMs: 2500});
    assert.equal(result.ok, true, result.error);
    assert.equal(result.status, 200);
    assert.equal(result.timeoutMs, 2500);
    assert.equal(hasPng(output), true);
});

test('an HTTP rejection cannot become a successful page snapshot', async () => {
    const output = path.join(scratch, 'denied.png');
    const result = await captureBrowser(browser, {url: `${origin}/denied`}, output,
        {navigationTimeoutMs: 2500});
    assert.equal(result.ok, false);
    assert.equal(result.status, 403);
    assert.equal(fs.existsSync(output), false);
});

test('a script redirect cannot hide the final document rejection behind an initial 200', async () => {
    const output = path.join(scratch, 'redirect.png');
    const result = await captureBrowser(browser, {url: `${origin}/redirect`}, output,
        {navigationTimeoutMs: 2500});
    assert.equal(result.ok, false);
    assert.equal(result.status, 403);
    assert.equal(result.finalUrl, `${origin}/denied`);
    assert.equal(fs.existsSync(output), false);
});

test('failed-only selection preserves the successful renderer and rejects stale URL results', () => {
    const pages = [{label: 'page', url: 'https://example.com/'}];
    fs.writeFileSync(path.join(scratch, 'page-lambda.png'), tinyPng);
    const previous = new Map([['page', {url: pages[0].url, lambda_ok: 'true', browser_ok: 'false'}]]);
    const options = {failedOnly: true, renderer: 'both'};
    let work = selectWork(pages, previous, options, scratch);
    assert.equal(work[0].lambda, false);
    assert.equal(work[0].browser, true);
    previous.get('page').url = 'https://example.org/';
    work = selectWork(pages, previous, options, scratch);
    assert.equal(work[0].lambda, true);
});

test('late download failures remain failures after load-completion telemetry', async () => {
    const log = path.join(scratch, 'late-error.log');
    fs.writeFileSync(log, 'x'.repeat(70000) + '\nFailed to download HTML from URL: https://example.com/\n' +
        'MEMSTAGE: load_html: cascade_done\n');
    const result = await readLambdaDiagnostics([log]);
    assert.equal(result.loadComplete, false);
    assert.match(result.error, /Failed to download/);
});

test('retry CSV round-trips multiline diagnostics', () => {
    const rows = [{label: 'test', error: 'first, "quoted"\nsecond'}];
    assert.deepEqual(parseCsv(serializeCsv(['label', 'error'], rows)),
        [['label', 'error'], ['test', rows[0].error]]);
});

test('failed staging images preserve a previously successful canonical PNG', () => {
    const canonical = path.join(scratch, 'canonical.png');
    const staging = path.join(scratch, 'staging.png');
    fs.writeFileSync(canonical, tinyPng);
    fs.writeFileSync(staging, 'incomplete');
    promoteSnapshot(staging, canonical, false);
    assert.equal(hasPng(canonical), true);
    assert.equal(hasPng(canonical.replace('.png', '.failed.png')), false);
});

test('parent deadline follows the engine watchdog and online launcher grace constants', () => {
    const configRoot = path.join(scratch, 'config');
    fs.mkdirSync(path.join(configRoot, 'lambda/module/radiant'), {recursive: true});
    fs.mkdirSync(path.join(configRoot, 'test'), {recursive: true});
    fs.writeFileSync(path.join(configRoot, 'lambda/module/radiant/script_timeout.hpp'),
        '#define RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS 9\n' +
        '#define RADIANT_SCRIPT_EXEC_TIMEOUT_ENV_MAX_SECONDS 19\n');
    fs.writeFileSync(path.join(configRoot, 'test/test_radiant_online_view_gtest.cpp'),
        '#define ONLINE_VIEW_CLEANUP_GRACE_SECONDS 3\n');
    assert.equal(defaultRenderTimeoutMs(configRoot, {}), 12000);
    assert.equal(defaultRenderTimeoutMs(configRoot, {LAMBDA_JS_EXEC_TIMEOUT_SECONDS: '99'}), 22000);
});
