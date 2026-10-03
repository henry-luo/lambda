// preserve native and browser samples independently; specification exceptions remain visible.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const {execFileSync} = require('child_process');
const {launchBrowser, serveWorkspace} = require('./svg_test_browser.cjs');
const {PNG} = require('../render/node_modules/pngjs');
const {assertPixels} = require('./svg_pixel_assertions.cjs');
const root = path.resolve(__dirname, '../..');
const names = process.argv.slice(2).filter(arg => !arg.startsWith('--'));
const fixtures = names.length ? names : require('./svg_paint_fixtures.json');
const browserMode = process.argv.includes('--browser');
const useReferences = process.argv.includes('--references');
const reference = process.argv.find(arg => arg.startsWith('--reference='))?.slice('--reference='.length);
const outputArg = process.argv.find(arg => arg.startsWith('--out='))?.slice('--out='.length);
const out = path.resolve(root, outputArg || `temp/svg-p7-p10/paint-${browserMode ? 'browser' : 'native'}`);
if (!out.startsWith(path.join(root, 'temp') + path.sep)) throw new Error('captures must be under ./temp/');
if (useReferences && !browserMode) throw new Error('--references requires --browser');
fs.mkdirSync(out, {recursive: true});

async function main() {
  const report = {binary_sha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(root, 'lambda.exe'))).digest('hex'), results: []};
  let server, browser;
  try {
    if (browserMode) {
      server = await serveWorkspace(root);
      browser = await launchBrowser(out);
      report.browser = await browser.version();
    }
    for (const name of fixtures) {
      const fixture = JSON.parse(fs.readFileSync(path.join(root, `test/ui/${name}.json`)));
      if (fixture.events.some(event => !event.type.startsWith('assert_')))
        throw new Error(`${name} requires the UI event runner; this gate checks static paint only`);
      const source = reference || (useReferences && fixture.browser_reference) || fixture.html;
      for (const scale of [1, 2]) for (const mode of browserMode ? ['browser'] : ['off', 'eager']) {
        const file = path.join(out, `${name}-${scale}x-${mode}.png`);
        if (browserMode) {
          const page = await browser.newPage();
          await page.setViewport({...fixture.viewport, deviceScaleFactor: scale});
          await page.goto(`http://127.0.0.1:${server.address().port}/${source}`, {waitUntil: 'load'});
          await page.evaluate(() => document.fonts.ready);
          await page.screenshot({path: file});
          await page.close();
        } else {
          const fixtureEnv = Object.fromEntries((fixture.env || []).map(entry => [entry.key, entry.value]));
          const log = execFileSync(path.join(root, 'lambda.exe'), ['render', fixture.html, '-o', file,
            '-vw', String(fixture.viewport.width), '-vh', String(fixture.viewport.height), '--scale', String(scale), '--no-log'],
            {cwd: root, timeout: 120000, env: {...process.env, ...fixtureEnv, RADIANT_SVG_LAYER: mode, TMPDIR: path.join(root, 'temp')}});
          fs.writeFileSync(file + '.log', log);
        }
        const png = PNG.sync.read(fs.readFileSync(file));
        const assertions = assertPixels(png, fixture.pixel_oracles?.[String(scale)] || fixture.events, scale);
        const result = {name, source: browserMode ? source : fixture.html, scale, mode, dimensions: [png.width, png.height], assertions,
          passed: assertions.length > 0 && assertions.every(assertion => assertion.passed)};
        report.results.push(result);
        console.log(`${name} ${scale}x ${mode}: ${assertions.filter(a => a.passed).length}/${assertions.length}`);
        for (const assertion of assertions.filter(a => !a.passed)) console.log(JSON.stringify(assertion));
        fs.writeFileSync(path.join(out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
      }
    }
    if (report.results.some(result => !result.passed)) process.exitCode = 1;
  } finally {
    if (browser) await browser.close();
    if (server) await new Promise(resolve => server.close(resolve));
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
