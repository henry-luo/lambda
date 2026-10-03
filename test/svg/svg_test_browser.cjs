// shared launch and workspace origin for SVG reference captures.
const fs = require('fs');
const path = require('path');
const http = require('http');
const puppeteer = require('../render/node_modules/puppeteer');
async function launchBrowser(output) {
  return puppeteer.launch({executablePath: process.env.CHROME_HEADLESS_SHELL,
    headless: true, args: ['--no-sandbox', '--disable-gpu', '--disable-lcd-text'],
    userDataDir: path.join(output, 'chrome-profile')});
}
async function serveWorkspace(root) {
  const server = http.createServer((request, response) => {
    const file = path.resolve(root, '.' + decodeURIComponent(new URL(request.url, 'http://localhost').pathname));
    if (!file.startsWith(root + path.sep)) { response.writeHead(403); response.end(); return; }
    fs.readFile(file, (error, data) => {
      if (error) { response.writeHead(404); response.end(); return; }
      response.setHeader('Content-Type', file.endsWith('.svg') ? 'image/svg+xml' : file.endsWith('.html') ? 'text/html' : 'application/octet-stream');
      response.end(data);
    });
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  return server;
}
module.exports = {launchBrowser, serveWorkspace};
