import fs from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';
import {createRequire} from 'node:module';
import puppeteer from 'puppeteer';

const require = createRequire(import.meta.url);
const referenceDir = path.dirname(new URL(import.meta.url).pathname);
const suiteDir = path.resolve(referenceDir, '..');
const config = JSON.parse(await fs.readFile(path.join(referenceDir, 'corpus.json'), 'utf8'));
const write = process.argv.includes('--write');
const mermaidScript = require.resolve('mermaid/dist/mermaid.min.js');
const adapterScript = path.join(referenceDir, 'mermaid_svg_adapter.mjs');

function attrs(mark, tag) {
  const matches = [...mark.matchAll(new RegExp(`<${tag}\\s+([^;>]+)`, 'g'))];
  return matches.map((match) => Object.fromEntries(
    [...match[1].matchAll(/([\w-]+):\s*'([^']*)'/g)].map((attr) => [attr[1], attr[2]])));
}

function labels(mark, tag) {
  return [...mark.matchAll(new RegExp(`<${tag}\\s+[^>]*;\\s*<label;\\s*'([^']*)'`, 'g'))]
    .map((match) => match[1]);
}

function checkSemantic(scene, mark, destination) {
  const expectedNodes = attrs(mark, 'node');
  const expectedEdges = attrs(mark, 'edge');
  const expectedNodeLabels = labels(mark, 'node');
  for (let index = 0; index < expectedNodes.length; index++) {
    const expected = expectedNodes[index];
    const actual = scene.nodes.find((node) => node.id === expected.id);
    if (!actual || (expected.shape && actual.shape !== expected.shape) ||
        (expectedNodeLabels[index] && actual.label !== expectedNodeLabels[index])) {
      throw new Error(`Mermaid semantic reference drift: ${destination} node ${expected.id}; ` +
        `actual=${JSON.stringify(actual)} nodes=${JSON.stringify(scene.nodes)}`);
    }
  }
  for (const expected of expectedEdges) {
    const actual = scene.edges.find((edge) =>
      (!expected.from || edge.from === expected.from) && (!expected.to || edge.to === expected.to));
    if (!actual) throw new Error(`Mermaid semantic reference drift: ${destination} edge; ` +
      `edges=${JSON.stringify(scene.edges)}`);
  }
}

const browser = await puppeteer.launch({headless: true});
try {
  const page = await browser.newPage();
  await page.setViewport({width: 1200, height: 900, deviceScaleFactor: 1});
  await page.setContent('<!doctype html><html><body><div id="diagram"></div></body></html>');
  await page.addScriptTag({path: mermaidScript});
  await page.addScriptTag({path: adapterScript, type: 'module'});
  await page.waitForFunction(() => globalThis.lambdaMermaidAdapter != null);
  await page.evaluate(() => mermaid.initialize({startOnLoad: false, securityLevel: 'loose',
    flowchart: {htmlLabels: true}}));

  let count = 0;
  for (const entry of config.cases.filter((item) => item.reference)) {
    const source = await fs.readFile(path.join(suiteDir, entry.destination), 'utf8');
    const scene = await page.evaluate(async ({sourceText, id}) => {
      const rendered = await mermaid.render(`lambda-${id}`, sourceText);
      const container = document.getElementById('diagram');
      container.innerHTML = rendered.svg;
      await document.fonts.ready;
      return lambdaMermaidAdapter.adaptMermaidSvg(container.querySelector('svg'));
    }, {sourceText: source, id: count});
    const expectedPath = path.join(suiteDir, entry.expected);
    const expected = await fs.readFile(expectedPath, 'utf8');
    checkSemantic(scene, expected, entry.expected);
    if (write) {
      const outputDir = path.join(suiteDir, 'expected/reference');
      await fs.mkdir(outputDir, {recursive: true});
      const output = await page.evaluate((value) =>
        lambdaMermaidAdapter.formatGraphSceneMark(value), scene);
      await fs.writeFile(path.join(outputDir, `${path.basename(entry.expected, '.mark')}.mark`), output);
    }
    count++;
  }
  console.log(`${write ? 'generated' : 'verified'} ${count} Mermaid browser references`);
} finally {
  await browser.close();
}
