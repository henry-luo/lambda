const {after} = require('node:test');
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
    return async (label, source) => {
        const html = path.join(scratch, label + '.html');
        const png = path.join(scratch, label + '.png');
        fs.writeFileSync(html, source);
        const result = await runLambdaRender({label, url: html}, png, parseOptions([], root), scratch);
        assert.equal(result.ok, true, result.error);
        const image = PNG.sync.read(fs.readFileSync(png));
        return {image, log: fs.readFileSync(path.join(scratch, result.outputLog), 'utf8')};
    };
}

function rgb(image, x, y) {
    const offset = (y * image.width + x) * 4;
    return [...image.data.subarray(offset, offset + 3)];
}

module.exports = {renderFixtures, rgb};
