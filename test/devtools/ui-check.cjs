// Run with utils/devtool/node_modules/.bin/electron test/devtools/ui-check.cjs.
const { app, BrowserWindow } = require('electron');
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
require('../../utils/devtool/main.js');

const root = path.resolve(__dirname, '../..');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(window, expression) {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    if (await window.webContents.executeJavaScript(expression)) return;
    await pause(50);
  }
  throw new Error(`UI condition timed out: ${expression}`);
}
async function capture(window, filename) {
  // DOM commits can precede Chromium's compositor; wait for the painted frame.
  await window.webContents.executeJavaScript('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
  await fs.writeFile(path.join(root, 'temp/devtools', filename), (await window.webContents.capturePage()).toPNG());
}
function setInput(selector, value) {
  return `(() => { const input = document.querySelector(${JSON.stringify(selector)}); Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, ${JSON.stringify(value)}); input.dispatchEvent(new Event('input', {bubbles:true})); })()`;
}
function click(window, label) {
  return window.webContents.executeJavaScript(`Array.from(document.querySelectorAll('button')).find(button => button.textContent === ${JSON.stringify(label)}).click()`);
}

app.whenReady().then(async () => {
  let window;
  try {
    while (!(window = BrowserWindow.getAllWindows()[0])) await pause(25);
    await waitFor(window, '!!document.querySelector(".menu-items")');
    await click(window, 'Live');
    await waitFor(window, '!!document.querySelector("#inspector-document")');
    await window.webContents.executeJavaScript(setInput('#inspector-document', 'test/devtools/document.html'));
    await click(window, 'Launch');
    await waitFor(window, '!!document.querySelector(".inspector-preview img")');
    await window.webContents.executeJavaScript(`Array.from(document.querySelectorAll('.inspector-node-label')).find(button => button.textContent === '<body>').parentElement.querySelector('.inspector-expand').click()`);
    await waitFor(window, `Array.from(document.querySelectorAll('.inspector-node-label')).some(button => button.textContent.includes('id="card"'))`);
    await window.webContents.executeJavaScript(`Array.from(document.querySelectorAll('.inspector-node-label')).find(button => button.textContent.includes('id="card"')).click()`);
    await waitFor(window, `document.querySelector('.inspector-box')?.textContent.includes('228.00')`);
    await fs.mkdir(path.join(root, 'temp/devtools'), { recursive: true });
    await capture(window, 'live-inspector.png');
    await window.webContents.executeJavaScript(setInput('[aria-label="CSS value"]', '320px'));
    await click(window, 'Apply');
    await waitFor(window, `document.querySelector('.inspector-box')?.textContent.includes('348.00')`);
    await waitFor(window, `document.querySelector('.inspector-preview img')?.dataset.revision === '2' && document.querySelector('.inspector-preview img').complete`);
    await capture(window, 'live-inspector-edited.png');
    await click(window, 'Undo');
    await waitFor(window, `document.querySelector('.inspector-box')?.textContent.includes('228.00')`);
    await click(window, 'Source');
    await waitFor(window, `document.querySelector('.inspector-source')?.textContent.includes('Lambda DevTools fixture')`);
    await click(window, 'Stop');
    await window.webContents.executeJavaScript(`document.querySelector('.inspector-native input').click()`);
    await click(window, 'Launch');
    await waitFor(window, `document.querySelector('.inspector-status')?.textContent.includes('Connected') && !!document.querySelector('.inspector-preview img')`);
    await click(window, 'Tests');
    await waitFor(window, '!!document.querySelector(".left-sidebar")');
    assert(await window.webContents.executeJavaScript('!!document.querySelector(".comparison-panel")'));
    console.log('PASS: Live workspace, tree/style/PNG, edit/undo, source, native window and comparison workspace.');
  } catch (error) {
    console.error(error);
    process.exitCode = 1;
    if (window && !window.isDestroyed()) {
      await fs.mkdir(path.join(root, 'temp/devtools'), { recursive: true });
      await fs.writeFile(path.join(root, 'temp/devtools/live-inspector-failure.png'), (await window.webContents.capturePage()).toPNG());
    }
  } finally {
    if (window && !window.isDestroyed()) window.close();
    app.quit();
  }
});
