const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('observers');
const cases = [
    {name: 'first intersection samples without a page geometry read',
        script: `new IntersectionObserver(entries => {
            delivered++;console.log('INITIAL_INTERSECTION', entries[0].isIntersecting);
            if (entries[0].isIntersecting) entries[0].target.style.opacity = '1';
        }).observe(probe);`, color: [0, 128, 0], log: 'INITIAL_INTERSECTION true'},
    {name: 'initial resize samples the final script writes without a page geometry read',
        script: `new ResizeObserver(entries => {
            delivered++;console.log('INITIAL_RESIZE', entries[0].contentRect.width);
            if (entries[0].contentRect.width === 240) probe.style.opacity = '1';
        }).observe(probe); probe.style.width = '240px';`,
        color: [0, 128, 0], log: 'INITIAL_RESIZE 240'},
    {name: 'initial offscreen intersection is delivered without revealing the target',
        style: 'margin-top:1000px',
        script: `new IntersectionObserver(entries => {
            delivered++;console.log('INITIAL_INTERSECTION', entries[0].isIntersecting);
            if (entries[0].isIntersecting) entries[0].target.style.opacity = '1';
        }).observe(probe);`, color: [255, 255, 255], log: 'INITIAL_INTERSECTION false'},
    {name: 'disconnect before the initial sample cancels delivery',
        script: `var observer = new IntersectionObserver(() => {
            console.log('UNEXPECTED_INTERSECTION');probe.style.opacity = '1';
        });observer.observe(probe);observer.disconnect();`, color: [255, 255, 255]}
];

for (const [index, fixture] of cases.entries()) {
    test(fixture.name, async () => {
        const {image, log} = await render('observer-' + index,
            '<!doctype html><style>body{margin:0}#probe{width:200px;height:80px;' +
            'background:green;opacity:0;' + (fixture.style || '') + '}</style>' +
            '<div id="probe"></div><script>var delivered=0,probe=document.getElementById("probe");' +
            fixture.script + 'console.log("OBSERVE_SYNC",delivered);</script>');
        assert(log.includes('OBSERVE_SYNC 0'), 'observe delivered before the script finished');
        if (fixture.log) {
            assert(log.includes(fixture.log), log.slice(-2000));
            assert.equal(log.split(fixture.log).length - 1, 1, 'initial sample was delivered more than once');
        }
        assert(!log.includes('UNEXPECTED_INTERSECTION'), 'disconnected observer was delivered');
        assert.deepEqual(rgb(image, 100, 40), fixture.color);
    });
}
