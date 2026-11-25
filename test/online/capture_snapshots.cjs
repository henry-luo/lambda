#!/usr/bin/env node

// Capture the shared online inventory, preserving successful sides during retries.
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const {spawn} = require('child_process');
const puppeteer = require('puppeteer');
const {readCsvObjects, readPageCatalog, serializeCsv} = require('../../utils/online_pages.cjs');

const viewport = {width: 1200, height: 800};
const DEFAULT_CAPTURE_SETTLE_MS = 1000;
const CAPTURE_ANIMATION_ORIGIN = 'post-load';
const resultColumns = [
    'label', 'url', 'lambda_ok', 'lambda_exit_code', 'lambda_elapsed_ms',
    'lambda_error', 'browser_ok', 'browser_elapsed_ms', 'browser_error',
    'lambda_snapshot', 'browser_snapshot', 'lambda_signal', 'lambda_log',
    'lambda_output_log', 'lambda_load_ok', 'browser_http_status', 'browser_final_url',
    'browser_retry_after', 'lambda_attempt_utc', 'browser_attempt_utc',
    'lambda_timeout_ms', 'browser_timeout_ms', 'browser_log',
    'lambda_settle_ms', 'browser_settle_ms',
    'lambda_animation_origin', 'browser_animation_origin',
    'lambda_script_load_clock', 'browser_script_load_clock',
    'browser_http_user_agent',
];

function integerOption(value, name, minimum = 1) {
    const number = Number(value);
    if ((typeof value === 'string' && value.trim() === '') || !Number.isSafeInteger(number) || number < minimum) {
        throw new Error(`${name} must be a ${minimum === 0 ? 'nonnegative' : 'positive'} integer`);
    }
    return number;
}

function defaultRenderTimeoutMs(root, env) {
    function constant(file, name) {
        const source = fs.readFileSync(path.join(root, file), 'utf8');
        const match = source.match(new RegExp(`^#define ${name} (\\d+)\\s*$`, 'm'));
        if (!match) throw new Error(`cannot read ${name} from ${file}`);
        return Number(match[1]);
    }
    // Share the engine watchdog and online launcher's grace period. The previous
    // 60s parent deadline interrupted scripts with a valid 120s CPU budget.
    const header = 'lambda/module/radiant/script_timeout.hpp';
    let seconds = constant(header, 'RADIANT_SCRIPT_EXEC_TIMEOUT_MAX_SECONDS');
    if (env.LAMBDA_JS_EXEC_TIMEOUT_SECONDS) {
        seconds = Math.min(integerOption(env.LAMBDA_JS_EXEC_TIMEOUT_SECONDS,
            'LAMBDA_JS_EXEC_TIMEOUT_SECONDS'),
        constant(header, 'RADIANT_SCRIPT_EXEC_TIMEOUT_ENV_MAX_SECONDS'));
    }
    return (seconds + constant('test/test_radiant_online_view_gtest.cpp',
        'ONLINE_VIEW_CLEANUP_GRACE_SECONDS')) * 1000;
}

function nativeHttpUserAgent(root = path.resolve(__dirname, '../..'), env = process.env) {
    const inputRoot = path.resolve(root, env.LAMBDA_INPUT_ROOT || 'lambda-input');
    const source = fs.readFileSync(path.join(inputRoot, 'lambda/network/http_client.h'), 'utf8')
        .replace(/\\\r?\n/g, '');
    const definition = source.match(/^#define RADIANT_HTTP_CLIENT_USER_AGENT[ \t]+(.+)$/m)?.[1];
    const literals = definition?.match(/"(?:[^"\\]|\\.)*"/g);
    if (!literals?.length || definition.replace(/"(?:[^"\\]|\\.)*"/g, '').trim()) {
        throw new Error('cannot read native HTTP user-agent profile');
    }
    return literals.map(literal => JSON.parse(literal)).join('');
}

function parseOptions(args, root, env = process.env, defaultSettleMs) {
    const settle = env.RADIANT_ONLINE_SETTLE_MS ?? env.LAMBDA_POST_LOAD_SETTLE_MS ?? defaultSettleMs;
    const options = {
        root, env, failedOnly: false, renderer: 'both', labels: null,
        loadClock: env.LAMBDA_CAPTURE_LOAD_CLOCK ?? 'elapsed',
        settleMs: settle === undefined ? undefined : integerOption(settle, 'capture settle window', 0),
        jobs: integerOption(env.RADIANT_ONLINE_CAPTURE_JOBS || 4, 'RADIANT_ONLINE_CAPTURE_JOBS'),
        navigationTimeoutMs: integerOption(env.RADIANT_ONLINE_CAPTURE_TIMEOUT_MS || 20000,
            'RADIANT_ONLINE_CAPTURE_TIMEOUT_MS'),
        lambdaTimeoutMs: integerOption(env.RADIANT_ONLINE_RENDER_TIMEOUT_MS ||
            defaultRenderTimeoutMs(root, env), 'RADIANT_ONLINE_RENDER_TIMEOUT_MS'),
    };
    for (const arg of args) {
        if (arg === '--failed-only') options.failedOnly = true;
        else if (arg.startsWith('--renderer=')) options.renderer = arg.slice('--renderer='.length);
        else if (arg.startsWith('--labels=')) options.labels = new Set(arg.slice('--labels='.length).split(','));
        else if (arg.startsWith('--settle-ms=')) options.settleMs = integerOption(arg.slice('--settle-ms='.length), '--settle-ms', 0);
        else throw new Error(`unknown option: ${arg}`);
    }
    if (!['both', 'lambda', 'browser'].includes(options.renderer)) throw new Error('invalid --renderer');
    if (!['elapsed', 'frozen'].includes(options.loadClock)) throw new Error('invalid capture load clock');
    if (options.settleMs !== undefined && options.settleMs > options.navigationTimeoutMs) {
        throw new Error('capture settle window must not exceed the navigation deadline');
    }
    return options;
}

function scriptLoadClock(options, tool) {
    return options.settleMs === undefined ? '' : tool === 'browser' ? 'elapsed' : options.loadClock ?? 'elapsed';
}

function hasPng(file) {
    let fd;
    try {
        fd = fs.openSync(file, 'r');
        const signature = Buffer.alloc(24);
        const end = Buffer.alloc(12);
        const size = fs.fstatSync(fd).size;
        return size >= 45 &&
            fs.readSync(fd, signature, 0, signature.length, 0) === signature.length &&
            signature.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])) &&
            signature.subarray(12, 16).toString() === 'IHDR' &&
            signature.readUInt32BE(16) > 0 && signature.readUInt32BE(20) > 0 &&
            fs.readSync(fd, end, 0, end.length, size - end.length) === end.length &&
            end.readUInt32BE(0) === 0 && end.subarray(4, 8).toString() === 'IEND';
    } catch (_) {
        return false;
    } finally {
        if (fd !== undefined) fs.closeSync(fd);
    }
}

function selectWork(pages, previous, options, snapshotsDir) {
    const known = new Set(pages.map(({label}) => label));
    if (options.labels) {
        for (const label of options.labels) if (!known.has(label)) throw new Error(`unknown page: ${label}`);
    }
    return pages.filter(({label}) => !options.labels || options.labels.has(label)).map((page) => {
        const old = previous.get(page.label);
        const reusable = old && old.url === page.url ? old : {};
        const needed = (tool) => (options.renderer === 'both' || options.renderer === tool) &&
            (!options.failedOnly || reusable[`${tool}_ok`] !== 'true' ||
                !hasPng(path.join(snapshotsDir, `${page.label}-${tool}.png`)));
        const work = {page, lambda: needed('lambda'), browser: needed('browser')};
        if (options.renderer === 'both' && options.settleMs !== undefined && (work.lambda || work.browser)) {
            // a retry at a new window also refreshes its successful counterpart to keep a comparable pair.
            for (const tool of ['lambda', 'browser']) {
                if (String(reusable[`${tool}_settle_ms`] ?? '') !== String(options.settleMs) ||
                    reusable[`${tool}_animation_origin`] !== CAPTURE_ANIMATION_ORIGIN ||
                    reusable[`${tool}_script_load_clock`] !== scriptLoadClock(options, tool)) work[tool] = true;
            }
        }
        return work;
    }).filter((work) => work.lambda || work.browser);
}

function elapsedMs(start) {
    return Number(process.hrtime.bigint() - start) / 1e6;
}

async function readLambdaDiagnostics(files) {
    let loadComplete = false;
    let error = '';
    let lastError = '';
    for (const file of files) {
        if (!fs.existsSync(file)) continue;
        const lines = readline.createInterface({input: fs.createReadStream(file), crlfDelay: Infinity});
        for await (const line of lines) {
            if (/MEMSTAGE.*load_html: cascade_done/.test(line)) loadComplete = true;
            if (/\[ERR!\]|Assertion failed:/.test(line)) {
                lastError = line.replace(/\x1b\[[0-9;]*m/g, '').trim();
            }
            if (!error && /Failed to download HTML from URL|Page Could Not Be Loaded/.test(line)) {
                error = line.trim();
            }
            const status = line.match(/document-response: rendering HTTP ([45]\d\d) body/);
            if (status) error = `HTTP ${status[1]} document response`;
        }
    }
    return {loadComplete: loadComplete && !error, error, lastError};
}

async function runLambdaRender(page, outputPath, options, snapshotsDir) {
    const start = process.hrtime.bigint();
    const outputLog = path.join(snapshotsDir, `${page.label}-lambda-output.log`);
    const engineLog = path.join(snapshotsDir, `${page.label}-lambda.log`);
    const fd = fs.openSync(outputLog, 'w');
    fs.writeFileSync(engineLog, '');
    const executable = options.env.LAMBDA_RENDER_EXE || path.join(options.root, 'lambda.exe');
    let timedOut = false;
    let launchError = '';
    let exitCode;
    let signal;
    try {
        const outcome = await new Promise((resolve) => {
            const child = spawn(executable, [
                'render', page.url, '-o', outputPath,
                '-vw', String(viewport.width), '-vh', String(viewport.height),
            ], {
                cwd: options.root, detached: process.platform !== 'win32', stdio: ['ignore', fd, fd],
                env: {...options.env,
                    ...(options.settleMs === undefined ? {} : {
                        LAMBDA_POST_LOAD_SETTLE_MS: String(options.settleMs),
                        LAMBDA_CAPTURE_LOAD_CLOCK: scriptLoadClock(options, 'lambda'),
                    }),
                    LAMBDA_LOG_FILE: engineLog, LAMBDA_LOG_LEVEL: 'INFO',
                    VIEW_MEM_STAGES: '1', VIEW_MEM_STATS: '1',
                    RADIANT_VIEW_TIMING_PATH: path.join(snapshotsDir, `${page.label}-timing.tsv`)},
            });
            const timer = setTimeout(() => {
                timedOut = true;
                try {
                    if (process.platform === 'win32') child.kill('SIGKILL');
                    else process.kill(-child.pid, 'SIGKILL');
                } catch (error) {
                    if (error.code !== 'ESRCH') launchError = error.message;
                }
            }, options.lambdaTimeoutMs);
            child.once('error', (error) => { launchError = error.message; });
            child.once('close', (code, terminationSignal) => {
                clearTimeout(timer);
                resolve({code, terminationSignal});
            });
        });
        exitCode = outcome.code;
        signal = outcome.terminationSignal;
    } finally {
        fs.closeSync(fd);
    }
    const diagnostic = await readLambdaDiagnostics([outputLog, engineLog]);
    const error = timedOut ? `timed out after ${options.lambdaTimeoutMs}ms` : launchError ||
        diagnostic.error || (signal ? `terminated by ${signal}: ${diagnostic.lastError}` : '') ||
        (exitCode !== 0 ? diagnostic.lastError || `exit code ${exitCode}` : '') ||
        (!diagnostic.loadComplete ? 'missing document load completion' : '') ||
        (!hasPng(outputPath) ? 'Lambda did not write a valid PNG' : '');
    return {ok: !error, error, exitCode: exitCode ?? -1, signal: signal || '',
        elapsedMs: elapsedMs(start), timeoutMs: options.lambdaTimeoutMs, settleMs: options.settleMs ?? '',
        animationOrigin: options.settleMs === undefined ? '' : CAPTURE_ANIMATION_ORIGIN,
        scriptLoadClock: scriptLoadClock(options, 'lambda'),
        loadOk: diagnostic.loadComplete,
        outputLog: path.basename(outputLog), log: path.basename(engineLog)};
}

async function captureBrowser(browser, pageCase, outputPath, options, diagnosticPath) {
    const start = process.hrtime.bigint();
    const deadline = Date.now() + options.navigationTimeoutMs;
    let tab;
    let session;
    let status = 0;
    let finalUrl = '';
    let retryAfter = '';
    const documentResponses = [];
    const pending = new Set();
    const failedRequests = [];
    const lifecycle = [];
    let result;
    let httpUserAgent = '';
    try {
        tab = await browser.newPage();
        httpUserAgent = nativeHttpUserAgent(options.root, options.env);
        // headless product tokens can select a different server-rendered document.
        // match the native wire profile while preserving Chromium's navigator identity.
        await tab.setExtraHTTPHeaders({'User-Agent': httpUserAgent});
        tab.on('request', (request) => pending.add(request));
        tab.on('requestfinished', (request) => pending.delete(request));
        tab.on('requestfailed', (request) => {
            pending.delete(request);
            if (failedRequests.length < 64) failedRequests.push({url: request.url(),
                type: request.resourceType(), error: request.failure()?.errorText});
        });
        // Script-initiated redirects can supersede goto's response. Keep the
        // main document's final response rather than accepting an earlier 200.
        tab.on('response', (response) => {
            if (response.request().isNavigationRequest() && response.frame() === tab.mainFrame()) {
                documentResponses.push(response);
            }
        });
        // goto can resolve with null when page script supersedes a navigation.
        // Observe the main frame's actual loading lifecycle before selecting its
        // committed response; an intermediate HTTP redirect is not a loaded page.
        session = await tab.createCDPSession();
        await session.send('Page.enable');
        if (options.settleMs !== undefined) {
            // native startup holds animation time at zero while resources load.
            await session.send('Animation.enable');
            await session.send('Animation.setPlaybackRate', {playbackRate: 0});
        }
        const tree = await session.send('Page.getFrameTree');
        const mainFrame = tree.frameTree.frame.id;
        let loading = false;
        const stopped = new Set();
        session.on('Page.frameStartedLoading', ({frameId}) => {
            if (frameId === mainFrame) {
                loading = true;
                lifecycle.push({event: 'started', elapsedMs: elapsedMs(start)});
            }
        });
        session.on('Page.frameStoppedLoading', ({frameId}) => {
            if (frameId !== mainFrame) return;
            loading = false;
            lifecycle.push({event: 'stopped', elapsedMs: elapsedMs(start)});
            for (const resolve of stopped) resolve();
        });
        await tab.setViewport({...viewport, deviceScaleFactor: 1});
        tab.setDefaultTimeout(options.navigationTimeoutMs);
        const navigationResponse = await tab.goto(pageCase.url, {
            // Long-lived analytics/fetch traffic can prevent networkidle2 even
            // after the document's load event. Capture a loaded document frame.
            waitUntil: 'load', timeout: options.navigationTimeoutMs,
        });
        await session.send('Page.getFrameTree');
        if (loading) {
            await new Promise((resolve, reject) => {
                const done = () => { clearTimeout(timer); stopped.delete(done); resolve(); };
                const timer = setTimeout(() => {
                    stopped.delete(done);
                    reject(new Error('document navigation did not finish loading'));
                }, Math.max(1, deadline - Date.now()));
                stopped.add(done);
            });
        }
        finalUrl = tab.url();
        const withoutFragment = (url) => url.split('#')[0];
        const response = documentResponses.findLast((candidate) =>
            withoutFragment(candidate.url()) === withoutFragment(finalUrl)) || navigationResponse;
        if (!response) throw new Error('navigation produced no document response');
        status = response.status();
        finalUrl = response.url();
        retryAfter = response.headers()['retry-after'] || '';
        if (status < 200 || status >= 300) throw new Error(`HTTP ${status}`);
        if (options.settleMs !== undefined)
            await session.send('Animation.setPlaybackRate', {playbackRate: 1});
        if (options.settleMs > 0) {
            let timeout;
            try {
                // sample the next rendering opportunity after the same post-load window as Lambda.
                await Promise.race([
                    tab.evaluate((ms) => new Promise((resolve) => {
                        setTimeout(() => requestAnimationFrame(resolve), ms);
                    }), options.settleMs),
                    new Promise((_, reject) => {
                        timeout = setTimeout(() => reject(new Error('capture settle window exceeded navigation deadline')),
                            Math.max(1, deadline - Date.now()));
                    }),
                ]);
            } finally { clearTimeout(timeout); }
        }
        await tab.screenshot({path: outputPath, type: 'png', timeout: options.navigationTimeoutMs});
        result = {ok: hasPng(outputPath), error: hasPng(outputPath) ? '' : 'invalid browser PNG',
            elapsedMs: elapsedMs(start), timeoutMs: options.navigationTimeoutMs, status, finalUrl, retryAfter};
    } catch (error) {
        const documentResponse = documentResponses.at(-1);
        if (documentResponse) {
            status = documentResponse.status();
            finalUrl = documentResponse.url();
            retryAfter = documentResponse.headers()['retry-after'] || '';
        }
        result = {ok: false, error: error.message, elapsedMs: elapsedMs(start),
            timeoutMs: options.navigationTimeoutMs, status, finalUrl, retryAfter};
    } finally {
        try {
            if (diagnosticPath) {
                // Keep origin evidence without retaining cookies or authorization headers.
                const headerNames = ['server', 'content-type', 'location', 'retry-after',
                    'cf-mitigated', 'cf-ray', 'x-cache', 'x-sucuri-block'];
                const responses = documentResponses.map((response) => ({
                    url: response.url(), status: response.status(), remote: response.remoteAddress(),
                    headers: Object.fromEntries(headerNames.filter((name) => name in response.headers())
                        .map((name) => [name, response.headers()[name]])),
                }));
                const state = session ? await session.send('Runtime.evaluate', {
                    expression: 'JSON.stringify({url:location.href,readyState:document.readyState,' +
                        'title:document.title,text:document.body?.innerText.slice(0,4096)||""})',
                    returnByValue: true, timeout: 1000,
                }, {timeout: 1500}).catch(() => null) : null;
                const documentState = state?.result?.value ? JSON.parse(state.result.value) : null;
                fs.writeFileSync(diagnosticPath, JSON.stringify({label: pageCase.label, url: pageCase.url,
                    attemptedUtc: new Date().toISOString(), ...result,
                    animationOrigin: options.settleMs === undefined ? '' : CAPTURE_ANIMATION_ORIGIN,
                    scriptLoadClock: scriptLoadClock(options, 'browser'),
                    httpUserAgent, responses, lifecycle, document: documentState,
                    failedRequests, pendingRequests: [...pending].slice(0, 64)
                        .map((request) => ({url: request.url(), type: request.resourceType()})),
                }, null, 2) + '\n');
            }
        } finally {
            if (session) await session.detach().catch(() => {});
            if (tab) await tab.close().catch(() => {});
        }
    }
    return {...result, httpUserAgent, settleMs: options.settleMs ?? '',
        scriptLoadClock: scriptLoadClock(options, 'browser'),
        animationOrigin: options.settleMs === undefined ? '' : CAPTURE_ANIMATION_ORIGIN,
        log: diagnosticPath ? path.basename(diagnosticPath) : ''};
}

function updateRow(row, tool, result) {
    row[`${tool}_ok`] = String(result.ok);
    row[`${tool}_error`] = result.error;
    row[`${tool}_elapsed_ms`] = result.elapsedMs.toFixed(1);
    row[`${tool}_attempt_utc`] = new Date().toISOString();
    row[`${tool}_timeout_ms`] = result.timeoutMs ?? '';
    row[`${tool}_settle_ms`] = result.settleMs ?? '';
    row[`${tool}_animation_origin`] = result.animationOrigin ?? '';
    row[`${tool}_script_load_clock`] = result.scriptLoadClock ?? '';
    if (tool === 'lambda') {
        row.lambda_exit_code = result.exitCode;
        row.lambda_signal = result.signal;
        row.lambda_load_ok = String(result.loadOk);
        row.lambda_log = result.log;
        row.lambda_output_log = result.outputLog;
    } else {
        row.browser_http_user_agent = result.httpUserAgent ?? '';
        row.browser_http_status = result.status;
        row.browser_final_url = result.finalUrl;
        row.browser_retry_after = result.retryAfter;
        row.browser_log = result.log;
    }
}

function promoteSnapshot(staged, destination, ok) {
    if (!fs.existsSync(staged)) return;
    // Error-page images remain diagnostics; canonical PNGs represent loaded pages.
    fs.renameSync(staged, ok ? destination : destination.replace(/\.png$/, '.failed.png'));
}

async function main(args = process.argv.slice(2)) {
    const root = path.resolve(__dirname, '../..');
    const options = parseOptions(args, root, process.env, DEFAULT_CAPTURE_SETTLE_MS);
    const snapshotsDir = path.join(root, 'test/online/snapshots');
    fs.mkdirSync(snapshotsDir, {recursive: true});
    fs.mkdirSync(path.join(root, 'temp'), {recursive: true});
    const scratch = fs.mkdtempSync(path.join(root, 'temp/online-capture-'));
    const manifest = path.join(snapshotsDir, 'capture_results.csv');
    const pages = readPageCatalog(path.join(root, 'test/online/pages.csv'));
    const previous = new Map((fs.existsSync(manifest) ? readCsvObjects(manifest) : [])
        .map((row) => [row.label, row]));
    const rows = pages.map((page) => ({
        label: page.label, url: page.url, lambda_ok: 'false', browser_ok: 'false',
        lambda_error: 'not attempted', browser_error: 'not attempted',
        lambda_snapshot: `${page.label}-lambda.png`, browser_snapshot: `${page.label}-browser.png`,
        ...(previous.get(page.label)?.url === page.url ? previous.get(page.label) : {}),
    }));
    const byLabel = new Map(rows.map((row) => [row.label, row]));
    const work = selectWork(pages, previous, options, snapshotsDir);
    const persist = () => {
        const staged = path.join(scratch, 'capture_results.csv');
        fs.writeFileSync(staged, serializeCsv(resultColumns, rows));
        fs.renameSync(staged, manifest);
    };
    process.stdout.write(`Selected ${work.length}/${pages.length} page cases; ` +
        `Lambda parent timeout ${options.lambdaTimeoutMs}ms.\n`);
    let browser;
    let completed = 0;
    let next = 0;
    try {
        if (work.some((item) => item.browser)) {
            const executablePath = options.env.CHROME_HEADLESS_SHELL;
            browser = await puppeteer.launch({
                ...(executablePath ? {executablePath} : {}),
                headless: executablePath ? true : 'shell',
                userDataDir: path.join(scratch, 'chromium'),
            });
        }
        async function worker() {
            while (next < work.length) {
                const item = work[next++];
                const row = byLabel.get(item.page.label);
                for (const tool of ['lambda', 'browser']) {
                    if (!item[tool]) continue;
                    const destination = path.join(snapshotsDir, `${item.page.label}-${tool}.png`);
                    const staged = path.join(scratch, `${item.page.label}-${tool}.png`);
                    if (row[`${tool}_ok`] !== 'true' && fs.existsSync(destination)) {
                        fs.renameSync(destination, destination.replace(/\.png$/, '.failed.png'));
                    }
                    const result = tool === 'lambda'
                        ? await runLambdaRender(item.page, staged, options, snapshotsDir)
                        : await captureBrowser(browser, item.page, staged, options,
                            path.join(snapshotsDir, `${item.page.label}-browser.json`));
                    updateRow(row, tool, result);
                    promoteSnapshot(staged, destination, result.ok);
                    persist();
                }
                completed++;
                process.stdout.write(`[${completed}/${work.length}] ${item.page.label} ` +
                    `lambda=${row.lambda_ok} browser=${row.browser_ok}\n`);
            }
        }
        const workers = await Promise.allSettled(
            Array.from({length: Math.min(options.jobs, work.length)}, worker));
        const failedWorker = workers.find((result) => result.status === 'rejected');
        if (failedWorker) throw failedWorker.reason;
        persist();
    } finally {
        if (browser) await browser.close();
        fs.rmSync(scratch, {recursive: true, force: true});
    }
    const failures = rows.filter((row) => row.lambda_ok !== 'true' || row.browser_ok !== 'true').length;
    process.stdout.write(`Processed ${completed} cases; ${failures}/${pages.length} still have a renderer failure.\n`);
    if (failures) process.exitCode = 1;
}

if (require.main === module) main().catch((error) => {
    process.stderr.write(`capture_snapshots: ${error.stack || error.message}\n`);
    process.exitCode = 1;
});

module.exports = {DEFAULT_CAPTURE_SETTLE_MS, parseOptions, selectWork, defaultRenderTimeoutMs, nativeHttpUserAgent, readLambdaDiagnostics,
    runLambdaRender, captureBrowser, hasPng, updateRow, promoteSnapshot};
