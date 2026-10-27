// Captures the Three.js reference for each case JSON in a directory:
// CHROME_HEADLESS_SHELL=... node test/scene3d/oracle/capture.cjs temp/pbr_oracle direct point ...
const http = require('http');
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer');
const [dir, ...cases] = process.argv.slice(2);
(async () => {
  const root = process.cwd();
  const server = http.createServer((req, res) => {
    const name = path.resolve(root, '.' + decodeURIComponent(req.url.split('?')[0]));
    if (!name.startsWith(root + path.sep)) { res.writeHead(403); res.end(); return; }
    try {
      res.setHeader('Content-Type', name.endsWith('.js') ? 'text/javascript' : name.endsWith('.html') ? 'text/html' : 'application/json');
      res.end(fs.readFileSync(name));
    } catch (e) { res.writeHead(404); res.end(e.message); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const browser = await puppeteer.launch({executablePath: process.env.CHROME_HEADLESS_SHELL, headless: true,
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']});
  try {
    const page = await browser.newPage();
    page.on('pageerror', e => console.log('browser error:', e.message));
    for (const key of cases) {
      const description = JSON.parse(fs.readFileSync(path.join(dir, key + '.json'), 'utf8'));
      await page.setViewport({width: description.width, height: description.height, deviceScaleFactor: 1});
      await page.goto('http://127.0.0.1:' + server.address().port + '/test/scene3d/oracle/pbr.html?data=/' + path.join(dir, key + '.json'));
      await page.waitForFunction('globalThis.pbrReady === true', {timeout: 30000});
      await page.screenshot({path: path.join(dir, key + '-chromium.png')});
      console.log('captured', key);
    }
    fs.writeFileSync(path.join(dir, 'browser.json'), JSON.stringify({three: '0.186.1', browser: await browser.version(), gl: 'ANGLE SwiftShader'}, null, 2));
  } finally { await browser.close(); server.close(); }
})().catch(e => { console.error(e); process.exit(1); });
