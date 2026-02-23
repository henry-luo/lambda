#!/usr/bin/env node
const puppeteer = require('puppeteer');

(async () => {
    const browser = await puppeteer.launch({ headless: 'new' });
    const page = await browser.newPage();
    await page.setViewport({ width: 1200, height: 800 });

    // No first-letter rule — test pure font metrics
    await page.setContent(`
        <!DOCTYPE html>
        <html>
        <head>
        <style>
            div { font-size: 36px; font-family: Times; }
        </style>
        </head>
        <body>
            <div id="d1">T</div>
            <div id="d2">\u301ET\u301E</div>
            <div id="d3">\uFD3F</div>
            <div id="d4">\u301E</div>
            <div id="d5">Hello</div>
        </body>
        </html>
    `);

    const result = await page.evaluate(() => {
        const ids = ['d1', 'd2', 'd3', 'd4', 'd5'];
        const r = {};
        ids.forEach(id => {
            const el = document.getElementById(id);
            if (el) {
                r[id] = {
                    offsetHeight: el.offsetHeight,
                    content: el.textContent
                };
            }
        });
        return r;
    });

    console.log(JSON.stringify(result, null, 2));
    await browser.close();
})();
