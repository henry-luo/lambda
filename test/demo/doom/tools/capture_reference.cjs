// Offline reference generation only; the application has no JavaScript runtime.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const root = path.resolve(__dirname, '../../../..');
const puppeteer = require(path.join(root, 'node_modules/puppeteer'));
const base = path.resolve(__dirname, '..');
const cases = [
  { name: 'depth-order', width: 440, height: 200 },
  { name: 'context-planes', width: 880, height: 660 },
  { name: 'background-clip', width: 220, height: 100 },
  { name: 'concave-floor-order', width: 1280, height: 336,
    reference_limitation: 'Chromium 154 drops a concave floor when a wall outside the probed pixel precedes it. Native assertions require the floor at (320,104) in both DOM orders under CSS Transforms 2 sections 4.1.2 and 4.2.' },
  { name: 'viewer-concave', width: 1280, height: 336,
    reference_limitation: 'Chromium 154 loses the near-clipped roof in reversed DOM order. The two panels have identical geometry; native assertions use the visible roof required by CSS Transforms 2 sections 4.1.2 and 4.2 in both panels.' }
];
async function main() {
  if (!process.env.CHROME_HEADLESS_SHELL) throw new Error('Set CHROME_HEADLESS_SHELL to the bundled headless shell.');
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME_HEADLESS_SHELL, headless: true,
    userDataDir: path.join(root, 'temp/doom/reference-profile'),
    args: ['--no-sandbox', '--disable-dev-shm-usage']
  });
  try {
    for (const fixture of cases) {
      const page = await browser.newPage();
      await page.setViewport({ width: fixture.width, height: fixture.height, deviceScaleFactor: 1 });
      await page.goto('file://' + path.join(base, 'tests/render', fixture.name + '.html'));
      const output = path.join(base, 'reference', fixture.name + '.png');
      await page.screenshot({ path: output });
      fs.writeFileSync(path.join(base, 'reference', fixture.name + '.browser.json'), JSON.stringify({
        ...fixture, scale: 1, browser: await browser.version(),
        spec: 'https://drafts.csswg.org/css-transforms-2/', revision: '2025-11-30',
        shapes: { spec: 'https://drafts.csswg.org/css-shapes/#shape-function', revision: '2026-05-07' },
        sha256: crypto.createHash('sha256').update(fs.readFileSync(output)).digest('hex')
      }, null, 2) + '\n');
      await page.close();
    }
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
