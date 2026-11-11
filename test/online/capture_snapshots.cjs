#!/usr/bin/env node

// Capture the same online page inventory with Lambda and Chromium.
const fs = require('fs');
const path = require('path');
const {spawn} = require('child_process');
const puppeteer = require('puppeteer');

const root = process.cwd();
const pagesPath = path.join(root, 'test/online/pages.csv');
const snapshotsDir = path.join(root, 'test/online/snapshots');
const lambdaExe = process.env.LAMBDA_RENDER_EXE || path.join(root, 'lambda.exe');
const concurrency = Number.parseInt(process.env.RADIANT_ONLINE_CAPTURE_JOBS || '4', 10);
const navigationTimeoutMs = Number.parseInt(
    process.env.RADIANT_ONLINE_CAPTURE_TIMEOUT_MS || '20000', 10);
const lambdaTimeoutMs = Number.parseInt(
    process.env.RADIANT_ONLINE_RENDER_TIMEOUT_MS || '60000', 10);
const viewport = {width: 1200, height: 800};

function parseCsvLine(line) {
    const fields = [];
    let field = '';
    let quoted = false;
    for (let index = 0; index < line.length; index++) {
        const character = line[index];
        if (quoted && character === '"' && line[index + 1] === '"') {
            field += '"';
            index++;
        } else if (character === '"') {
            quoted = !quoted;
        } else if (character === ',' && !quoted) {
            fields.push(field);
            field = '';
        } else {
            field += character;
        }
    }
    if (quoted) throw new Error('unterminated quoted CSV field');
    fields.push(field);
    return fields;
}

function csvField(value) {
    const text = String(value ?? '');
    return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

function readPages() {
    const lines = fs.readFileSync(pagesPath, 'utf8').trimEnd().split(/\r?\n/);
    if (parseCsvLine(lines[0] || '').join(',') !==
            'label,url,expect_linked_resources') {
        throw new Error(`${pagesPath} has an unexpected header`);
    }
    const labels = new Set();
    return lines.slice(1).map((line, index) => {
        const fields = parseCsvLine(line);
        if (fields.length !== 3 || !/^[A-Za-z0-9_-]+$/.test(fields[0]) ||
                !/^https?:\/\//.test(fields[1]) ||
                !['true', 'false'].includes(fields[2])) {
            throw new Error(`${pagesPath}:${index + 2} has an invalid row`);
        }
        if (labels.has(fields[0])) throw new Error(`duplicate page label: ${fields[0]}`);
        labels.add(fields[0]);
        return {label: fields[0], url: fields[1]};
    });
}

function elapsedMs(start) {
    return Number(process.hrtime.bigint() - start) / 1e6;
}

function runLambdaRender(page, outputPath) {
    return new Promise((resolve) => {
        const start = process.hrtime.bigint();
        let output = '';
        let timedOut = false;
        let child;
        try {
            child = spawn(lambdaExe, [
                'render', page.url, '-o', outputPath,
                '-vw', String(viewport.width), '-vh', String(viewport.height),
            ], {cwd: root, stdio: ['ignore', 'pipe', 'pipe'], detached: process.platform !== 'win32'});
        } catch (error) {
            resolve({ok: false, exitCode: -1, elapsedMs: elapsedMs(start), error: error.message});
            return;
        }

        const appendOutput = (chunk) => {
            if (output.length < 65536) output += chunk.toString();
        };
        child.stdout.on('data', appendOutput);
        child.stderr.on('data', appendOutput);
        const timer = setTimeout(() => {
            timedOut = true;
            try {
                if (process.platform === 'win32') child.kill('SIGKILL');
                else process.kill(-child.pid, 'SIGKILL');
            } catch (_) { }
        }, lambdaTimeoutMs);
        child.on('error', (error) => {
            clearTimeout(timer);
            resolve({ok: false, exitCode: -1, elapsedMs: elapsedMs(start), error: error.message});
        });
        child.on('close', (exitCode) => {
            clearTimeout(timer);
            const loadError = /failed to download html from url|could not resolve host|page could not be loaded/i
                .test(output);
            const exists = fs.existsSync(outputPath) && fs.statSync(outputPath).size > 0;
            const error = timedOut ? 'timed out' :
                loadError ? 'Lambda reported a page-load error' :
                    !exists ? 'Lambda did not write an image' : '';
            resolve({
                ok: !error && exitCode === 0,
                exitCode: exitCode === null ? -1 : exitCode,
                elapsedMs: elapsedMs(start),
                error,
            });
        });
    });
}

async function captureBrowser(browser, pageCase, outputPath) {
    const start = process.hrtime.bigint();
    let tab;
    try {
        tab = await browser.newPage();
        await tab.setViewport({...viewport, deviceScaleFactor: 1});
        tab.setDefaultNavigationTimeout(navigationTimeoutMs);
        const response = await tab.goto(pageCase.url, {
            waitUntil: 'networkidle2', timeout: navigationTimeoutMs,
        });
        await tab.screenshot({path: outputPath, type: 'png'});
        const status = response ? response.status() : 0;
        return {
            ok: status === 0 || (status >= 200 && status < 400),
            elapsedMs: elapsedMs(start),
            error: status >= 400 ? `HTTP ${status}` : '',
        };
    } catch (error) {
        let screenshotError = '';
        if (tab) {
            try {
                await tab.screenshot({path: outputPath, type: 'png'});
            } catch (captureError) {
                screenshotError = captureError.message;
            }
        }
        return {
            ok: false,
            elapsedMs: elapsedMs(start),
            error: [error.message, screenshotError].filter(Boolean).join('; '),
        };
    } finally {
        if (tab) await tab.close().catch(() => {});
    }
}

async function main() {
    if (!Number.isInteger(concurrency) || concurrency < 1) {
        throw new Error('RADIANT_ONLINE_CAPTURE_JOBS must be a positive integer');
    }
    const pages = readPages();
    fs.mkdirSync(snapshotsDir, {recursive: true});

    let browser = null;
    let browserStartupError = '';
    try {
        const executablePath = process.env.CHROME_HEADLESS_SHELL;
        browser = await puppeteer.launch({
            ...(executablePath ? {executablePath} : {}),
            headless: executablePath ? true : 'shell',
            args: ['--disable-background-networking'],
        });
    } catch (error) {
        browserStartupError = error.message;
        process.stderr.write(`capture_snapshots: Chromium launch failed: ${browserStartupError}\n`);
    }

    const results = new Array(pages.length);
    let next = 0;
    let completed = 0;
    async function worker() {
        while (next < pages.length) {
            const index = next++;
            const pageCase = pages[index];
            const lambdaPath = path.join(snapshotsDir, `${pageCase.label}-lambda.png`);
            const browserPath = path.join(snapshotsDir, `${pageCase.label}-browser.png`);
            fs.rmSync(lambdaPath, {force: true});
            fs.rmSync(browserPath, {force: true});

            const lambda = await runLambdaRender(pageCase, lambdaPath);
            const browserCapture = browser
                ? await captureBrowser(browser, pageCase, browserPath)
                : {ok: false, elapsedMs: 0, error: browserStartupError};
            results[index] = {pageCase, lambda, browser: browserCapture};
            completed++;
            process.stdout.write(
                `[${completed}/${pages.length}] ${pageCase.label} ` +
                `lambda=${lambda.ok ? 'ok' : 'failed'} ` +
                `browser=${browserCapture.ok ? 'ok' : 'failed'}\n`);
        }
    }

    await Promise.all(Array.from({length: Math.min(concurrency, pages.length)}, worker));
    if (browser) await browser.close();

    const header = [
        'label', 'url', 'lambda_ok', 'lambda_exit_code', 'lambda_elapsed_ms',
        'lambda_error', 'browser_ok', 'browser_elapsed_ms', 'browser_error',
        'lambda_snapshot', 'browser_snapshot',
    ];
    const rows = results.map(({pageCase, lambda, browser: browserCapture}) => [
        pageCase.label, pageCase.url, lambda.ok, lambda.exitCode,
        lambda.elapsedMs.toFixed(1), lambda.error, browserCapture.ok,
        browserCapture.elapsedMs.toFixed(1), browserCapture.error,
        `${pageCase.label}-lambda.png`, `${pageCase.label}-browser.png`,
    ]);
    fs.writeFileSync(path.join(snapshotsDir, 'capture_results.csv'),
        [header, ...rows].map((row) => row.map(csvField).join(',')).join('\n') + '\n');

    const failures = results.filter(({lambda, browser: browserCapture}) =>
        !lambda.ok || !browserCapture.ok).length;
    process.stdout.write(`Captured ${pages.length} page cases; ${failures} had at least one capture failure.\n`);
    if (failures) process.exitCode = 1;
}

main().catch((error) => {
    process.stderr.write(`capture_snapshots: ${error.stack || error.message}\n`);
    process.exitCode = 1;
});
