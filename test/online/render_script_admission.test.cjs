const {test, after} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {promisify} = require('node:util');
const execFile = promisify(require('node:child_process').execFile);
const {renderFixtures, serveFixtureResources, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('script-admission');
const root = path.resolve(__dirname, '../..');
const scratch = fs.mkdtempSync(path.join(root, 'temp/script-admission-view-'));
after(() => fs.rmSync(scratch, {recursive:true, force:true}));
const source = '/*' + 'x'.repeat(128 * 1024) + '*/document.getElementById("paint").style.background="blue";' +
    'console.log("SCRIPT_ADMISSION_EXECUTED");';
const origin = serveFixtureResources((request, response) => {
    const url = new URL(request.url, 'http://fixture.invalid');
    response.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/entry.js') {
        response.writeHead(200, {'Content-Type':'text/javascript'}).end(source);
    } else if (url.pathname === '/page') {
        const schedule = url.searchParams.get('schedule');
        const attribute = schedule === 'blocking' ? '' : schedule === 'module' ? 'type="module"' : schedule;
        response.writeHead(200, {'Content-Type':'text/html'}).end('<!doctype html><style>body{margin:0}</style>' +
            '<div id="paint" style="width:100px;height:100px;background:red"></div>' +
            '<script ' + attribute + ' src="/entry.js"></script>');
    } else response.writeHead(404).end();
});

const cases = [
    {name:'full-blocking', schedule:'blocking', executed:true},
    {name:'full-defer', schedule:'defer', executed:true},
    {name:'full-async', schedule:'async', executed:true},
    {name:'full-module', schedule:'module', executed:true},
    {name:'explicit-prelayout-defer', schedule:'defer', limit:'RADIANT_JS_PRELAYOUT_DEFER_BYTES'},
    {name:'explicit-prelayout-async', schedule:'async', limit:'RADIANT_JS_PRELAYOUT_DEFER_BYTES'},
    {name:'explicit-prelayout-module', schedule:'module', limit:'RADIANT_JS_PRELAYOUT_DEFER_BYTES'},
    {name:'external-budget', schedule:'blocking', limit:'RADIANT_JS_EXTERNAL_SCRIPT_BYTES'},
    {name:'total-budget', schedule:'defer', limit:'RADIANT_JS_TOTAL_SCRIPT_BYTES'},
];
function admissionEnvironment(limit, value = '1024') {
    const env = {...process.env, RADIANT_JS_TASK_DIAGNOSTICS:'1'};
    for (const name of ['RADIANT_JS_PRELAYOUT_DEFER_BYTES', 'RADIANT_JS_EXTERNAL_SCRIPT_BYTES', 'RADIANT_JS_TOTAL_SCRIPT_BYTES'])
        delete env[name];
    if (limit) env[limit] = value;
    return env;
}
for (const entry of cases) test('script admission profile: ' + entry.name, async () => {
    const {image} = await render(entry.name, {url:origin() + '/page?schedule=' + entry.schedule},
        admissionEnvironment(entry.limit));
    assert.deepEqual(rgb(image, 50, 50), entry.executed ? [0,0,255] : [255,0,0]);
});

for (const schedule of ['defer', 'async', 'override']) test('static view admission: ' + schedule, async () => {
    const env = admissionEnvironment(schedule === 'override' ? 'RADIANT_JS_PRELAYOUT_DEFER_BYTES' : undefined, '0');
    env.LAMBDA_LOG_FILE = path.join(scratch, schedule + '.log');
    const {stdout, stderr} = await execFile(env.LAMBDA_RENDER_EXE || path.join(root, 'lambda.exe'),
        ['view', origin() + '/page?schedule=' + (schedule === 'override' ? 'defer' : schedule), '--headless'],
        {cwd:root, env, timeout:20000, killSignal:'SIGKILL'});
    const output = stdout + stderr;
    if (schedule === 'override') assert.match(output, /SCRIPT_ADMISSION_EXECUTED/);
    else {
        assert.match(output, /status=skipped-large-defer/);
        assert.doesNotMatch(output, /SCRIPT_ADMISSION_EXECUTED/);
    }
});
