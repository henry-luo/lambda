// compare portable SVG/PDF exports with the same rendered fixture assertions at each density.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const {execFileSync} = require('child_process');
const {launchBrowser} = require('./svg_test_browser.cjs');
const {PNG} = require('../render/node_modules/pngjs');
const {assertPixels} = require('./svg_pixel_assertions.cjs');

const root = path.resolve(__dirname, '../..');
const names = process.argv.slice(2).filter(arg => !arg.startsWith('--'));
const fixtures = names.length ? names : require('./svg_export_fixtures.json');
const scales = process.argv.includes('--1x') ? [1] : [1, 2];
const out = path.join(root, 'temp/svg-p13/export-matrix');
const relocated = path.join(out, 'relocated');
fs.mkdirSync(relocated, {recursive: true});
const report = {binary_sha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(root, 'lambda.exe'))).digest('hex'), results: []};
const outlinedFonts = new Set(['svg_font_shorthands', 'svg_text_resources']);

function render(fixture, name, scale, format) {
  const target = path.join(out, `${name}-${scale}x.${format}`);
  const fixtureEnv = Object.fromEntries((fixture.env || []).map(entry => [entry.key, entry.value]));
  const output = execFileSync(path.join(root, 'lambda.exe'), ['render', fixture.html, '-o', target,
    '-vw', String(fixture.viewport.width), '-vh', String(fixture.viewport.height),
    '--scale', String(scale), '--no-log'], {cwd: root, timeout: 120000,
    env: {...process.env, ...fixtureEnv, TMPDIR: path.join(root, 'temp')}});
  fs.writeFileSync(target + '.log', output);
  return target;
}

async function main() {
  const browser = await launchBrowser(out);
  report.browser = await browser.version();
  try {
    const page = await browser.newPage();
    await page.setRequestInterception(true);
    let resourceRequests = [];
    page.on('request', request => {
      // moving the artifact must not leave dependencies on its source document or working directory.
      if (request.isNavigationRequest() || request.url().startsWith('data:')) request.continue();
      else { resourceRequests.push(request.url()); request.abort(); }
    });
    for (const name of fixtures) {
      const fixture = JSON.parse(fs.readFileSync(path.join(root, `test/ui/${name}.json`)));
      let contentDimensions;
      for (const scale of scales) {
        const events = fixture.pixel_oracles?.[String(scale)] || fixture.events;
        const result = {name, scale, formats: {}};
        report.results.push(result);
        for (const format of ['png', 'svg', 'pdf']) {
          try {
            const target = render(fixture, name, scale, format);
            let raster = target;
            const formatResult = {};
            result.formats[format] = formatResult;
            if (format === 'svg') {
              const moved = path.join(relocated, path.basename(target));
              fs.copyFileSync(target, moved);
              resourceRequests = [];
              const source = fs.readFileSync(moved, 'utf8');
              formatResult.structure = await page.evaluate(source => {
                const doc = new DOMParser().parseFromString(source, 'image/svg+xml');
                const errors = [];
                if (doc.querySelector('parsererror')) errors.push(doc.querySelector('parsererror').textContent);
                const ids = [...doc.querySelectorAll('[id]')].map(element => element.id);
                if (new Set(ids).size !== ids.length) errors.push('duplicate resource IDs');
                for (const element of doc.querySelectorAll('*')) for (const attr of element.attributes) {
                  for (const match of attr.value.matchAll(/url\(#([^)]*)\)/g))
                    if (!ids.includes(match[1])) errors.push('unresolved resource: ' + match[1]);
                  if (attr.localName === 'href' && !attr.value.startsWith('data:'))
                    errors.push('non-embedded image link: ' + attr.value);
                }
                return {errors, dimensions: ['width', 'height'].map(name => Number(doc.documentElement.getAttribute(name))),
                  paths: doc.querySelectorAll('path').length, images: doc.querySelectorAll('image').length,
                  gradients: doc.querySelectorAll('linearGradient, radialGradient').length};
              }, source);
              const [width, height] = formatResult.structure.dimensions;
              await page.setViewport({width, height, deviceScaleFactor: 1});
              if (outlinedFonts.has(name) && (formatResult.structure.paths === 0 || formatResult.structure.images !== 0))
                formatResult.structure.errors.push('font outlines did not remain vector paths');
              if (name === 'svg_gradient_paints' && formatResult.structure.gradients === 0)
                formatResult.structure.errors.push('representable SVG gradients were rasterized');
              await page.goto('file://' + moved, {waitUntil: 'load'});
              await page.evaluate(() => document.fonts.ready);
              raster = target + '.png';
              await page.screenshot({path: raster});
              formatResult.resourceRequests = [...resourceRequests];
            } else if (format === 'pdf') {
              const prefix = target + '-raster';
              // Poppler's Cairo backend preserves aligned image pixels; Splash resamples even at 1:1.
              execFileSync('pdftocairo', ['-r', '72', '-png', '-singlefile', target, prefix], {timeout: 120000});
              raster = prefix + '.png';
              const images = execFileSync('pdfimages', ['-list', target], {encoding: 'utf8'});
              fs.writeFileSync(target + '-images.txt', images);
              formatResult.vectorRetained = !outlinedFonts.has(name) || !/^\s*\d+\s+\d+\s+(image|smask)\s/m.test(images);
            }
            const png = PNG.sync.read(fs.readFileSync(raster));
            formatResult.dimensions = [png.width, png.height];
            // vector outputs fit content bounds; the viewport controls layout and the raster crop.
            if (format === 'svg' && scale === 1) contentDimensions = formatResult.structure.dimensions;
            formatResult.expectedDimensions = format === 'png'
              ? [fixture.viewport.width * scale, fixture.viewport.height * scale]
              : contentDimensions.map(dimension => dimension * scale);
            formatResult.assertions = assertPixels(png, events, scale);
            formatResult.passed = formatResult.assertions.length > 0 &&
              formatResult.dimensions.every((dimension, index) => dimension === formatResult.expectedDimensions[index]) &&
              formatResult.assertions.every(assertion => assertion.passed) &&
              !(formatResult.structure?.errors.length) && !(formatResult.resourceRequests?.length) &&
              formatResult.vectorRetained !== false;
            console.log(`${name} ${scale}x ${format}: ${formatResult.assertions.filter(a => a.passed).length}/${formatResult.assertions.length}${formatResult.passed ? '' : ' FAIL'}`);
            for (const failed of formatResult.assertions.filter(a => !a.passed)) console.log(JSON.stringify(failed));
            if (formatResult.structure?.errors.length) console.log(formatResult.structure.errors.join('\n'));
          } catch (error) {
            result.formats[format] = {passed: false, error: error.message};
            console.log(`${name} ${scale}x ${format}: ERROR ${error.message}`);
          }
          fs.writeFileSync(path.join(out, 'report.json'), JSON.stringify(report, null, 2) + '\n');
        }
      }
    }
    if (report.results.some(result => Object.values(result.formats).some(format => !format.passed))) process.exitCode = 1;
  } finally { await browser.close(); }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
