const {before, after} = require('node:test');
const http = require('node:http');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {parseOptions, runLambdaRender} = require('./capture_snapshots.cjs');
const {pngLibrary} = require('./compare_snapshots.cjs');

function renderFixtures(name) {
    const root = path.resolve(__dirname, '../..');
    fs.mkdirSync(path.join(root, 'temp'), {recursive: true});
    const scratch = fs.mkdtempSync(path.join(root, 'temp/render-' + name + '-test-'));
    const PNG = pngLibrary(root);
    after(() => fs.rmSync(scratch, {recursive: true, force: true}));
    return async (label, source, env = process.env) => {
        const html = path.join(scratch, label + '.html');
        const png = path.join(scratch, label + '.png');
        fs.writeFileSync(html, source);
        const result = await runLambdaRender({label, url: html}, png, parseOptions([], root, env), scratch);
        assert.equal(result.ok, true, result.error);
        const image = PNG.sync.read(fs.readFileSync(png));
        return {image, log: fs.readFileSync(path.join(scratch, result.outputLog), 'utf8')};
    };
}

function rgb(image, x, y) {
    const offset = (y * image.width + x) * 4;
    return [...image.data.subarray(offset, offset + 3)];
}

function layoutCount(log) {
    return (log.match(/\[LAYOUT_PROFILE\] retained-stage:/g) || []).length;
}

function loggedResult(log, tag) {
    const match = new RegExp(tag + ' (\\{[^\\n]+\\})').exec(log);
    assert.ok(match, log);
    return JSON.parse(match[1]);
}

function serveFixtureResources(handler) {
    let server, origin;
    before(async () => {
        server = http.createServer(handler);
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
        origin = 'http://127.0.0.1:' + server.address().port;
    });
    after(async () => { if (server) await new Promise(resolve => server.close(resolve)); });
    return () => origin;
}

module.exports = {renderFixtures, rgb, layoutCount, loggedResult, serveFixtureResources};
