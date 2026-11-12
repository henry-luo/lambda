const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('stylesheet');

const cases = [
    {name: 'insertRule on a newly connected empty style survives export', css: '',
        script: `var style=document.createElement('style');
            document.head.appendChild(style);
            style.appendChild(document.createTextNode(''));
            style.sheet.insertRule('#probe {width:240px;height:80px;background:red}',0);`,
        width: '240px', rgb: [255, 0, 0], inside: 200, outside: 250},
    {name: 'rule declaration edits survive an unrelated DOM mutation',
        css: '#probe {width:60px;height:80px;background:blue}',
        script: `var rule=document.getElementById('source').sheet.cssRules[0];
            rule.style.width='240px';rule.style.background='red';
            document.body.setAttribute('data-edited','true');`,
        width: '240px', rgb: [255, 0, 0], inside: 200, outside: 250},
    {name: 'sheet reads consume text replacement before subsequent CSSOM edits',
        css: '#probe {width:60px;height:80px;background:blue}',
        script: `var style=document.getElementById('source');
            style.textContent='#probe {width:240px;height:80px;background:green}';
            console.log('SHEET_WIDTH',style.sheet.cssRules[0].style.width);
            style.sheet.insertRule('#probe {background:red}',1);`,
        width: '240px', rgb: [255, 0, 0], inside: 200, outside: 250, sheetWidth: '240px'},
    {name: 'later text replacement supersedes earlier CSSOM edits', css: '',
        script: `var style=document.createElement('style');document.head.appendChild(style);
            style.sheet.insertRule('#probe {width:240px;height:80px;background:red}',0);
            style.textContent='#probe {width:80px;height:80px;background:green}';`,
        width: '80px', rgb: [0, 128, 0], inside: 40, outside: 100}
];

for (const [index, fixture] of cases.entries()) {
    test(fixture.name, async () => {
        const label = 'stylesheet-' + index;
        const {image, log} = await render(label, '<!doctype html><style>body{margin:0}</style>' +
            '<style id="source">' + fixture.css + '</style><div id="probe"></div><script>' +
            fixture.script + '\nconsole.log("COMPUTED_WIDTH",getComputedStyle(document.getElementById("probe")).width);</script>');
        assert(log.includes('COMPUTED_WIDTH ' + fixture.width), log.slice(-2000));
        if (fixture.sheetWidth) assert(log.includes('SHEET_WIDTH ' + fixture.sheetWidth), log.slice(-2000));
        assert.deepEqual(rgb(image, fixture.inside, 40), fixture.rgb, 'CSSOM style missing from exported pixels');
        assert.deepEqual(rgb(image, fixture.outside, 40), [255, 255, 255], 'export retained stale width');
    });
}
