import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';

const [tools, chrome, batch] = process.argv.slice(2);
const require = createRequire(path.resolve(tools, 'package.json'));
const {instance} = await import(pathToFileURL(require.resolve('@viz-js/viz')));
const puppeteer = require('puppeteer');
const graphviz = await instance();
console.log('Graphviz', graphviz.graphvizVersion);
const browser = await puppeteer.launch({
  headless: 'shell', executablePath: chrome,
  userDataDir: path.resolve(path.dirname(batch), 'chrome-profile'),
});
try {
  const page = await browser.newPage();
  await page.setRequestInterception(true);
  page.on('request', request => request.abort());
  await page.setContent('<!doctype html><html><body><div id="output"></div></body></html>');
  await page.addScriptTag({path: require.resolve('mermaid/dist/mermaid.min.js')});
  await page.evaluate(() => mermaid.initialize({
    startOnLoad: false, securityLevel: 'strict', deterministicIds: true,
  }));
  console.log('Chromium', await browser.version());
  for (const [input, output] of JSON.parse(await fs.readFile(batch))) {
    const source = await fs.readFile(input, 'utf8');
    const svg = path.extname(input) === '.gv'
      ? graphviz.renderString(source, {format: 'svg', engine: 'dot'})
      : await page.evaluate(async (source) => {
        const host = document.querySelector('#output');
        host.innerHTML = (await mermaid.render('diagram', source)).svg;
        // The HTML DOM repairs HTML labels and serializes the SVG namespaces.
        return new XMLSerializer().serializeToString(host.querySelector('svg'));
      }, source);
    await fs.writeFile(output, svg);
  }
} finally {
  await browser.close();
}
