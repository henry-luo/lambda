// Offline browser/native references; no JavaScript is shipped into the demo.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawn } = require('child_process');
const root = path.resolve(__dirname, '../../../..');
const base = path.resolve(__dirname, '..');
const temp = path.join(root, 'temp/doom');
const binary = process.env.LAMBDA_EXECUTABLE || path.join(root, 'lambda.exe');
const reference = process.env.DOOM_REFERENCE_DIR ? path.resolve(root, process.env.DOOM_REFERENCE_DIR) : path.join(base, 'reference');
const puppeteer = require(path.join(root, 'node_modules/puppeteer'));
const { PNG } = require(path.join(root, 'test/render/node_modules/pngjs'));
const manifest = JSON.parse(fs.readFileSync(path.join(base, 'reference/poses.json')));
const selected = process.argv.slice(2);
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const readPng = file => PNG.sync.read(fs.readFileSync(file));
function patchMean(png, rect) {
  const [x, y, width, height] = rect;
  if (x < 0 || y < 0 || x + width > png.width || y + height > png.height) throw new Error('Landmark outside image');
  const channels = [0, 0, 0];
  for (let row = y; row < y + height; row++) for (let col = x; col < x + width; col++) {
    const index = (row * png.width + col) * 4;
    channels.forEach((_, channel) => { channels[channel] += png.data[index + channel]; });
  }
  return channels.map(value => value / (width * height));
}
function validateLandmarks(pose, nativePng, browserPng) {
  return pose.landmarks.map(landmark => {
    const actual = patchMean(nativePng, landmark.rect);
    const reference = patchMean(browserPng, landmark.rect);
    for (let channel = 0; channel < 3; channel++) {
      if ([actual[channel], reference[channel]].some(value => value < landmark.rgb_min[channel] || value > landmark.rgb_max[channel]) ||
          Math.abs(actual[channel] - reference[channel]) > landmark.max_mean_channel_delta)
        throw new Error(`${pose.name}: ${landmark.name} channel ${channel}: native=${actual}, browser=${reference}`);
    }
    return { name: landmark.name, rect: landmark.rect, native_rgb: actual, browser_rgb: reference };
  });
}
function filterDifference(visible, hidden, rect) {
  let changedPixels = 0, peak = 0;
  const left = Math.max(0, Math.floor(rect.x)), top = Math.max(0, Math.floor(rect.y));
  const right = Math.min(visible.width, Math.ceil(rect.x + rect.width));
  const bottom = Math.min(visible.height, Math.ceil(rect.y + rect.height));
  for (let y = top; y < bottom; y++) for (let x = left; x < right; x++) {
    const index = (y * visible.width + x) * 4;
    let change = 0;
    for (let channel = 0; channel < 3; channel++) change = Math.max(change, Math.abs(visible.data[index + channel] - hidden.data[index + channel]));
    if (change > 2) changedPixels++;
    peak = Math.max(peak, change);
  }
  return { rect, changed_pixels: changedPixels, peak_channel_change: peak };
}
function native(args, log) {
  return new Promise((resolve, reject) => {
    const fd = fs.openSync(log, 'w');
    const child = spawn(binary, args, { cwd: root,
      env: { ...process.env, LAMBDA_EXEC_BACKEND: process.env.LAMBDA_EXEC_BACKEND || 'auto' }, stdio: ['ignore', fd, fd] });
    child.on('error', reject);
    child.on('exit', code => { fs.closeSync(fd); code === 0 ? resolve() : reject(new Error(`Native pose failed (${code}); see ${log}`)); });
  });
}
async function main() {
  if (!process.env.CHROME_HEADLESS_SHELL) throw new Error('Set CHROME_HEADLESS_SHELL.');
  fs.mkdirSync(temp, { recursive: true });
  fs.mkdirSync(reference, { recursive: true });
  const browser = await puppeteer.launch({ executablePath: process.env.CHROME_HEADLESS_SHELL,
    userDataDir: path.join(temp, 'pose-browser-profile'), headless: true, args: ['--no-sandbox'] });
  try {
    for (const pose of manifest.poses.filter(pose => !selected.length || selected.includes(pose.name))) {
      const nativePng = path.join(temp, pose.name + '-native.png');
      async function captureNative(output, hideActor) {
        fs.writeFileSync(path.join(temp, 'pose-request.json'), JSON.stringify({ name: pose.name, hide_actor: hideActor }));
        fs.writeFileSync(path.join(temp, 'pose-events.json'), JSON.stringify({ name: 'DOOM pose ' + pose.name,
          html: 'test/demo/doom/fixtures/_pose.ls', viewport: manifest.viewport, events: [
            { type: 'assert_attribute', target: { selector: '#doom' }, attribute: 'data-captured', equals: pose.name },
            { type: 'assert_attribute', target: { selector: '#doom' }, attribute: 'data-skill', equals: String(pose.skill) },
            { type: 'render', file: path.relative(root, output) }
          ] }));
        await native(['view', 'test/demo/doom/fixtures/_pose.ls', '--headless', '--event-file',
          'temp/doom/pose-events.json'], output.replace(/\.png$/, '.log'));
      }
      await captureNative(nativePng);
      const exported = JSON.parse(fs.readFileSync(path.join(temp, 'pose-export.json')));
      if (exported.name !== pose.name) throw new Error('Stale pose export');
      const html = path.join(temp, pose.name + '.html');
      fs.writeFileSync(html, exported.html);
      const page = await browser.newPage();
      await page.setViewport({ width: manifest.viewport.width, height: manifest.viewport.height,
        deviceScaleFactor: manifest.viewport.scale });
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      page.on('requestfailed', request => errors.push(request.url() + ': ' + request.failure().errorText));
      await page.goto('file://' + html, { waitUntil: 'networkidle0' });
      await page.evaluate(() => document.fonts.ready);
      if (errors.length) throw new Error(errors.join('\n'));
      const selectedMap = await page.$eval('#map-picker', element => element.value);
      if (selectedMap !== pose.map) throw new Error(`${pose.name}: map selector says ${selectedMap}`);
      const selectedSkill = await page.$eval('#skill-picker', element => element.value);
      if (selectedSkill !== String(pose.skill)) throw new Error(`${pose.name}: difficulty selector says ${selectedSkill}`);
      const output = path.join(reference, pose.name + '.png');
      await page.screenshot({ path: output });
      const validation = { selected_map: selectedMap, selected_skill: selectedSkill,
        landmarks: validateLandmarks(pose, readPng(nativePng), readPng(output)) };
      if (pose.filtered_actor) {
        const actor = pose.filtered_actor;
        const rect = await page.$eval(actor.selector, element => element.getBoundingClientRect().toJSON());
        const nativeHidden = path.join(temp, pose.name + '-hidden-native.png');
        const browserHidden = path.join(temp, pose.name + '-hidden-browser.png');
        await captureNative(nativeHidden, actor.id);
        await page.$eval(actor.selector, element => { element.style.visibility = 'hidden'; });
        await page.screenshot({ path: browserHidden });
        validation.filtered_actor = {};
        for (const [engine, visible, hidden] of [['native', nativePng, nativeHidden], ['browser', output, browserHidden]]) {
          const difference = filterDifference(readPng(visible), readPng(hidden), rect);
          if (difference.changed_pixels < actor.min_changed_pixels || difference.peak_channel_change < actor.min_peak_channel_change)
            throw new Error(`${pose.name}: ${engine} filtered actor missing: ${JSON.stringify(difference)}`);
          validation.filtered_actor[engine] = { ...difference, hidden_sha256: hash(hidden) };
        }
      }
      const recordedNative = path.join(reference, pose.name + '.native.png');
      fs.copyFileSync(nativePng, recordedNative);
      fs.writeFileSync(path.join(reference, pose.name + '.browser.json'), JSON.stringify({
        pose, viewport: manifest.viewport, browser: await browser.version(),
        source: 'fixtures/_pose.ls and the shared scene/presenter; authored declarations exported without computed geometry.',
        html_sha256: hash(html), browser_sha256: hash(output), native_sha256: hash(nativePng),
        native_image: path.basename(recordedNative), browser_image: path.basename(output),
        native_binary: path.relative(root, binary), native_binary_sha256: hash(binary),
        validation: { ...validation, passed: true }
      }, null, 2) + '\n');
      await page.close();
      console.log(pose.name + ': native/browser landmarks and actor checks passed');
    }
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
