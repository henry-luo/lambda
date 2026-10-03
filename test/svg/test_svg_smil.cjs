// exercise document-time animation through real UI events at both cache modes and densities.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const {spawnSync} = require('child_process');
const {PNG} = require('../render/node_modules/pngjs');
const {assertPixels} = require('./svg_pixel_assertions.cjs');
const {launchBrowser, serveWorkspace} = require('./svg_test_browser.cjs');
const root = path.resolve(__dirname, '../..');
const selected = process.argv.slice(2).filter(arg => !arg.startsWith('--'));
const fixtures = selected.length ? selected : require('./svg_smil_fixtures.json');
const scales = process.argv.includes('--1x') ? [1] : [1, 2];
const browserMode = process.argv.includes('--browser');
const useReferences = process.argv.includes('--references');
if (useReferences && !browserMode) throw new Error('--references requires --browser');
const output = path.join(root, 'temp/svg-p12', browserMode ? (useReferences ? 'browser-references' : 'browser-raw') : 'ui-matrix');
fs.mkdirSync(output, {recursive: true});
const executable = path.join(root, 'lambda.exe');
const report = {binary_sha256: crypto.createHash('sha256').update(fs.readFileSync(executable)).digest('hex'), results: []};
function runNative() {
  for (const name of fixtures) {
    const fixture = JSON.parse(fs.readFileSync(path.join(root, 'test/ui', name + '.json')));
    for (const scale of scales) for (const mode of ['off', 'eager']) {
      const prefix = path.join(output, `${name}-${scale}x-${mode}`);
      const events = prefix + '-events.json';
      const resultFile = prefix + '-result.json';
      fs.writeFileSync(events, JSON.stringify({...fixture, device_scale: scale}, null, 2) + '\n');
      fs.rmSync(resultFile, {force: true}); // an aborted launch must not reuse an earlier PASS.
      const fixtureEnv = Object.fromEntries((fixture.env || []).map(entry => [entry.key, entry.value]));
      const run = spawnSync(executable, ['view', fixture.html, '--event-file', events,
        '--event-result', resultFile, '--headless', '--font-dir', 'test/layout/data/font', '--no-log'],
        {cwd: root, timeout: 90000, encoding: 'utf8',
          env: {...process.env, ...fixtureEnv, TMPDIR: path.join(root, 'temp'), RADIANT_SVG_LAYER: mode}});
      fs.writeFileSync(prefix + '.log', (run.stdout || '') + (run.stderr || '') + (run.error ? run.error.message : ''));
      const result = fs.existsSync(resultFile) ? JSON.parse(fs.readFileSync(resultFile)) : null;
      const expected = fixture.events.filter(event => event.type.startsWith('assert_')).length;
      const passed = run.status === 0 && result?.result === 'PASS' &&
        result.assertions.failed === 0 && result.assertions.passed === expected;
      report.results.push({fixture: name, scale, mode, exit: run.status, expected, passed, result});
      console.log(`${name} ${scale}x ${mode}: ${result?.assertions.passed || 0}/${expected}${passed ? '' : ' FAIL'}`);
      fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
    }
  }
  if (report.results.some(result => !result.passed)) process.exitCode = 1;

}

async function runBrowser() {
  const server = await serveWorkspace(root);
  let browser;
  try {
    browser = await launchBrowser(output); report.browser = await browser.version();
    for (const name of fixtures) for (const scale of scales) {
      const fixture = JSON.parse(fs.readFileSync(path.join(root, 'test/ui', name + '.json')));
      const source = (useReferences && fixture.browser_reference) || fixture.html;
      const page = await browser.newPage();
      const assertions = [];
      try {
        await page.setViewport({...fixture.viewport, deviceScaleFactor: scale});
        await page.goto(`http://127.0.0.1:${server.address().port}/${source}`, {waitUntil: 'load'});
        await page.evaluate(() => document.fonts.ready);
        // timing-attribute changes made by fixture scripts reach SVG paint on the next frame.
        await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
        for (const event of fixture.events) {
          if (event.type === 'advance_time') { await new Promise(resolve => setTimeout(resolve, event.ms)); continue; }
          if (event.type === 'click') {
            if (event.target) await page.click(event.target.selector);
            else await page.mouse.click(event.x, event.y);
            await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
            continue;
          }
          if (event.type === 'key_press') {
            await page.keyboard.press(event.key);
            await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
            continue;
          }
          if (event.type === 'assert_pixel') {
            assertions.push(...assertPixels(PNG.sync.read(await page.screenshot()), [event], scale));
            continue;
          }
          let actual, passed;
          if (event.type === 'assert_attribute') {
            actual = await page.$eval(event.target.selector, (node, name) => node.getAttribute(name), event.attribute);
            passed = actual === event.equals;
          } else if (event.type === 'assert_hit_test') {
            actual = await page.evaluate(event => !!document.elementFromPoint(event.x, event.y)?.matches(event.expected_selector), event);
            passed = actual;
          } else throw new Error('unsupported SMIL browser event: ' + event.type);
          assertions.push({event, actual, passed});
        }
        const passed = assertions.length > 0 && assertions.every(assertion => assertion.passed);
        report.results.push({fixture: name, source, scale, passed, assertions});
        console.log(`${name} ${scale}x browser: ${assertions.filter(assertion => assertion.passed).length}/${assertions.length}${passed ? '' : ' FAIL'}`);
        for (const assertion of assertions.filter(assertion => !assertion.passed)) console.log(JSON.stringify(assertion));
        fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
      } finally { await page.close(); }
    }
    if (report.results.some(result => !result.passed)) process.exitCode = 1;
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}
(async () => { if (browserMode) await runBrowser(); else runNative(); })().catch(error => { console.error(error); process.exitCode = 1; });
