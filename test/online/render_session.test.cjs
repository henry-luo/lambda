const {test, before, after} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {spawn} = require('node:child_process');
const {parseOptions, runLambdaRender} = require('./capture_snapshots.cjs');

const root = path.resolve(__dirname, '../..');
fs.mkdirSync(path.join(root, 'temp'), {recursive: true});
const scratch = fs.mkdtempSync(path.join(root, 'temp/render-session-test-'));
const requests = [];
let server, origin;

before(async () => {
    server = http.createServer((request, response) => {
        const url = new URL(request.url, 'http://fixture.invalid');
        requests.push({path: url.pathname, run: url.searchParams.get('run'), cookie: request.headers.cookie || ''});
        response.setHeader('Cache-Control', 'no-store');
        if (url.pathname === '/redirect') {
            response.writeHead(302, {'Set-Cookie': 'redirect_cookie=kept; Path=/',
                Location: '/page?run=redirect'}).end();
            return;
        }
        if (url.pathname.endsWith('.svg')) {
            response.setHeader('Content-Type', 'image/svg+xml');
            response.end('<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><rect width="20" height="20" fill="blue"/></svg>');
            return;
        }
        const run = url.searchParams.get('run');
        const seeded = run === 'seeded' || run === 'redirect';
        if (seeded) response.setHeader('Set-Cookie', [
            'response_cookie=visible; Path=/', 'http_only=hidden; Path=/; HttpOnly']);
        response.setHeader('Content-Type', 'text/html');
        response.end('<!doctype html><body><img src="/initial.svg?run=' + run + '"><script>' +
            'var result={enabled:navigator.cookieEnabled,initial:document.cookie};' +
            'try{' + (run === 'read' ? '' : 'document.cookie="script_cookie=written; Path=/";') +
            'result.after=document.cookie;' +
            'var image=document.createElement("img");image.src="/late.svg?run=' + run + '";document.body.appendChild(image);' +
            '}catch(error){result.error=error.name+": "+error.message}' +
            'console.log("EXPORT_SESSION_RESULT",JSON.stringify(result));</script>');
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    origin = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
    if (server) await new Promise(resolve => server.close(resolve));
    fs.rmSync(scratch, {recursive: true, force: true});
});

async function render(run, route = '/page') {
    const label = 'session-' + run;
    const options = parseOptions([], root);
    const result = await runLambdaRender({label, url: `${origin}${route}?run=${run}`},
        path.join(scratch, label + '.png'), options, scratch);
    assert.equal(result.ok, true, result.error);
    const output = fs.readFileSync(path.join(scratch, result.outputLog), 'utf8');
    const records = sessionResults(output);
    assert.equal(records.length, 1, output.slice(-2000));
    return records[0];
}

function sessionResults(output) {
    return [...output.matchAll(/EXPORT_SESSION_RESULT (\{[^\n]+\})/g)].map(match => JSON.parse(match[1]));
}

async function command(name, args, input = '') {
    const options = parseOptions([], root);
    const child = spawn(options.env.LAMBDA_RENDER_EXE || path.join(root, 'lambda.exe'),
        [name, ...args], {cwd: root, timeout: options.lambdaTimeoutMs, killSignal: 'SIGKILL',
            env: {...options.env, LAMBDA_LOG_FILE: path.join(scratch, name + '-engine.log')},
            stdio: ['pipe', 'pipe', 'pipe']});
    let output = '';
    child.stdout.on('data', bytes => { output += bytes; });
    child.stderr.on('data', bytes => { output += bytes; });
    child.stdin.end(input);
    const code = await new Promise((resolve, reject) => {
        child.once('error', reject);
        child.once('close', resolve);
    });
    fs.writeFileSync(path.join(scratch, name + '-output.log'), output);
    assert.equal(code, 0, output.slice(-2000));
    return sessionResults(output);
}

test('HTTP export shares response, document and subresource cookies', async () => {
    const result = await render('seeded');
    assert.equal(result.enabled, true);
    assert.equal(result.error, undefined);
    assert.match(result.initial, /response_cookie=visible/);
    assert.doesNotMatch(result.initial, /http_only/);
    assert.match(result.after, /script_cookie=written/);
    const initial = requests.find(row => row.run === 'seeded' && row.path === '/initial.svg');
    const late = requests.find(row => row.run === 'seeded' && row.path === '/late.svg');
    assert(initial, 'initial image was not requested');
    assert(late, 'script-created image was not requested');
    assert.match(initial.cookie, /response_cookie=visible/);
    assert.match(initial.cookie, /http_only=hidden/);
    assert.match(late.cookie, /script_cookie=written/);
});

test('HTTP redirects retain cookies before the destination document loads', async () => {
    const result = await render('redirect', '/redirect');
    assert.equal(result.error, undefined);
    assert.match(result.initial, /redirect_cookie=kept/);
    const destination = requests.find(row => row.run === 'redirect' && row.path === '/page');
    assert(destination);
    assert.match(destination.cookie, /redirect_cookie=kept/);
});

test('independent exports use separate ephemeral cookie sessions', async () => {
    const writer = await render('write');
    assert.equal(writer.error, undefined);
    assert.match(writer.after, /script_cookie=written/);
    const reader = await render('read');
    assert.equal(reader.enabled, true);
    assert.equal(reader.initial, '');
    assert.equal(reader.after, '');
    assert.equal(requests.find(row => row.run === 'read' && row.path === '/page').cookie, '');
});

test('headless layout uses the same HTTP cookie session as rendering', async () => {
    const records = await command('layout', [`${origin}/page?run=layout`,
        '--view-output', path.join(scratch, 'layout.json'), '-vw', '1200', '-vh', '800']);
    assert.equal(records.length, 1);
    assert.equal(records[0].enabled, true);
    assert.equal(records[0].error, undefined);
    assert.match(records[0].after, /script_cookie=written/);
});

for (const formats of [false, true]) {
    test(`batch ${formats ? 'export' : 'PNG'} jobs retain their UI cookie session until cleanup`, async () => {
        const input = ['write', 'read'].map(run => `${origin}/page?run=${run}\t${path.join(scratch, 'batch-' + formats + '-' + run + '.png')}\t1200\t800\t1\n`).join('');
        const records = await command('render-batch', formats ? ['--export'] : [], input);
        assert.equal(records.length, 2);
        assert.equal(records[0].error, undefined);
        assert.match(records[0].after, /script_cookie=written/);
        assert.equal(records[1].error, undefined);
        assert.match(records[1].initial, /script_cookie=written/);
    });
}
