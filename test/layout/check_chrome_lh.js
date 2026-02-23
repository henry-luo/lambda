#!/usr/bin/env node
const puppeteer = require('puppeteer');

(async () => {
    const browser = await puppeteer.launch({ headless: 'new' });
    const page = await browser.newPage();
    await page.setViewport({ width: 1200, height: 800 });
    
    await page.setContent(`
        <!DOCTYPE html>
        <html>
        <head>
        <style>
            div:first-letter { color: green; font-size: 36px; }
        </style>
        </head>
        <body>
            <div id="d1">\u301ET\u301E</div>
            <div id="d2" style="font-size: 16px; font-family: Times;"><span id="s1" style="font-size: 36px;">T</span>ext</div>
            <div id="d3" style="font-size: 36px; font-family: Times;">T</div>
            <div id="d4" style="font-size: 16px; font-family: Times;">T</div>
        </body>
        </html>
    `);
    
    const result = await page.evaluate(() => {
        const ids = ['d1', 'd2', 'd3', 'd4', 's1'];
        const r = {};
        ids.forEach(id => {
            const el = document.getElementById(id);
            if (el) {
                const cs = getComputedStyle(el);
                r[id] = {
                    offsetHeight: el.offsetHeight,
                    bcrHeight: el.getBoundingClientRect().height,
                    fontSize: cs.fontSize,
                    lineHeight: cs.lineHeight,
                    fontFamily: cs.fontFamily
                };
            }
        });
        // Also check the ::first-letter pseudo
        const d1 = document.getElementById('d1');
        const flcs = getComputedStyle(d1, '::first-letter');
        r['d1_fl'] = {
            fontSize: flcs.fontSize,
            lineHeight: flcs.lineHeight,
            fontFamily: flcs.fontFamily
        };
        return r;
    });
    
    console.log(JSON.stringify(result, null, 2));
    await browser.close();
})();
