// Independent ws server + real Chromium client, with all artifacts under temp/.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {spawn} = require('node:child_process');
const {WebSocketServer} = require('ws');
const root = path.resolve(__dirname, '../..');
const artifacts = path.join(root, 'temp/websocket-tests');
fs.mkdirSync(artifacts, {recursive: true});
const dir = fs.mkdtempSync(path.join(artifacts, 'browser-'));
const exe = process.env.LAMBDA_WEBSOCKET_EXE || path.join(root, 'lambda.exe');
function child(args, env={}) {
    const p = spawn(exe, args, {cwd:root, env:{...process.env, ...env}});
    let stdout='', stderr='';
    p.stdout.on('data', b => stdout += b); p.stderr.on('data', b => stderr += b);
    const timer = setTimeout(() => p.kill('SIGKILL'), 20000);
    p.done = new Promise(resolve => p.on('close', code => {clearTimeout(timer); resolve({code, stdout, stderr});}));
    return p;
}
async function lambdaBrowserApi(failures = false, watchdog = false) {
    const server = new WebSocketServer({port:0, host:'127.0.0.1', perMessageDeflate:false,
        handleProtocols: protocols => protocols.has('chat.v1') && 'chat.v1'});
    await new Promise(r => server.on('listening', r));
    let drained = false, closed = null;
    server.on('connection', (socket, req) => {
        assert.equal(req.headers.origin, 'null');
        if (req.url.endsWith('/abort')) { socket.terminate(); return; }
        socket.on('message', (bytes, binary) => {
            if (!binary && bytes.toString() === 'drain-before-close') drained = true;
            else socket.send(bytes, {binary});
        });
        socket.on('close', (code, reason) => { closed = [code, reason.toString()]; });
    });
    const url = `ws://127.0.0.1:${server.address().port}/echo`;
    const script = path.join(dir, 'browser-api.js');
    const fixture = watchdog ? '_browser_watchdog.js' : failures ? '_browser_failures.js' : '_browser_api.js';
    fs.writeFileSync(script, `const TEST_WS_URL = ${JSON.stringify(url)};\n` + fs.readFileSync(path.join(__dirname, fixture), 'utf8'));
    const html = path.join(dir, 'browser-api.html'); fs.writeFileSync(html, '<!doctype html><title>WebSocket API</title>');
    try {
        const result = await child(['js', script, '--document', html]).done;
        fs.writeFileSync(path.join(dir, watchdog ? 'browser-watchdog.log' : failures ? 'browser-failures.log' : 'browser-api.log'), result.stdout + result.stderr);
        assert.equal(result.code, 0, result.stdout + result.stderr);
        assert.match(result.stdout, watchdog ? /WEBSOCKET_WATCHDOG_OPEN/ : failures ? /WEBSOCKET_BROWSER_FAILURES_PASS/ : /WEBSOCKET_BROWSER_API_PASS/);
        assert.doesNotMatch(result.stderr, /threw:|unexpected WebSocket/);
        if (!failures && !watchdog) {assert.equal(drained, true); assert.deepEqual(closed, [3501, 'bye 世界']);}
        console.log(watchdog ? 'PASS document socket cleanup at drain timeout' : failures ? 'PASS LambdaJS WebSocket failure/close semantics' : 'PASS LambdaJS WebSocket API against ws server');
    } finally { server.close(); for (const socket of server.clients) socket.terminate(); }
}
async function chromiumClient() {
    const puppeteer = require('../../node_modules/puppeteer');
    const server = http.createServer((req, res) => res.end('<!doctype html><title>WebSocket peer</title>'));
    await new Promise(r => server.listen(0, '127.0.0.1', r));
    const origin = `http://127.0.0.1:${server.address().port}`;
    const portFile = path.join(dir, 'browser-listener-port.txt');
    fs.rmSync(portFile, {force:true});
    const p = child(['run', 'test/websocket/_browser_server.ls'], {
        WEBSOCKET_TEST_ORIGIN: origin, WEBSOCKET_TEST_PORT_FILE: portFile
    });
    let browser;
    try {
        const deadline = Date.now() + 10000;
        while (!fs.existsSync(portFile) && Date.now() < deadline) await new Promise(r=>setTimeout(r,25));
        assert.ok(fs.existsSync(portFile), 'Lambda listener started');
        const port = fs.readFileSync(portFile, 'utf8').trim();
        browser = await puppeteer.launch({headless:'shell',
            ...(process.env.CHROME_HEADLESS_SHELL ? {executablePath:process.env.CHROME_HEADLESS_SHELL} : {})});
        const page = await browser.newPage(); await page.goto(origin);
        const events = await page.evaluate(port => new Promise((resolve,reject) => {
            const ws = new WebSocket(`ws://127.0.0.1:${port}/chat`, ['chat.v1']);
            const received = [];
            ws.binaryType='arraybuffer';
            ws.onopen=()=> {ws.send('Browser 世界'); ws.send(new Uint8Array([0,255])); ws.send('');};
            ws.onmessage=e=> {received.push(typeof e.data==='string' ? e.data : Array.from(new Uint8Array(e.data))); if(received.length===3) ws.close(3502,'browser bye');};
            ws.onerror=()=>reject(new Error('Chromium WebSocket failed'));
            ws.onclose=e=>resolve({protocol:ws.protocol, received, code:e.code, reason:e.reason, clean:e.wasClean});
        }), port);
        assert.deepEqual(events, {protocol:'chat.v1', received:['Browser 世界',[0,255],''], code:3502, reason:'browser bye', clean:true});
        const result = await p.done;
        assert.equal(result.code, 0, result.stdout + result.stderr);
        assert.match(result.stdout, /3502, "browser bye", true/);
        fs.writeFileSync(path.join(dir,'chromium-result.json'), JSON.stringify(events,null,2));
        console.log('PASS Chromium WebSocket against Lambda listener');
    } finally { if(browser) await browser.close(); p.kill(); server.close(); }
}
(async()=> {await lambdaBrowserApi(); await lambdaBrowserApi(true); await lambdaBrowserApi(false, true); await chromiumClient();})().catch(e=>{console.error(e); process.exitCode=1;});
