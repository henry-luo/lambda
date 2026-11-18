const fixtures = [
    {name: 'direct text control', content: 'MMMM', expected: 'green'},
    {name: 'block child text', content: '<h1>MMMM</h1>', expected: 'green'},
    {name: 'nested inline text', content: '<h1><span><b>MMMM</b></span></h1>', expected: 'green'},
    {name: 'radial background child text', content: '<h1>MMMM</h1>', radial: true, expected: 'green'},
    {name: 'translated owner child text', content: '<h1>MMMM</h1>', extra: 'transform:translate(40px,20px)', expected: 'green'},
    {name: 'nested clipping owners paint inside last', content: '<h1 class="inner">MMMM</h1>', background: 'red', expected: 'green'},
    {name: 'offset child retains owner gradient coordinates', content: '<h1 style="margin-left:260px">MM</h1>',
        gradient: 'linear-gradient(90deg,red 50%,lime 50%)', extra: 'width:420px', noRed: true},
    {name: 'ordinary foreground control', content: '<h1>MMMM</h1>', extra: 'background:none;background-clip:border-box;color:lime', expected: 'green'},
];

function fixtureHtml(fixture) {
    const color = fixture.background || 'lime';
    const gradient = fixture.gradient || (fixture.radial ? `radial-gradient(${color},${color})` : `linear-gradient(${color},${color})`);
    return `<!doctype html><style>body{margin:0;background:white}
.owner{margin:20px;padding:10px;width:300px;font:64px/1 Arial;background:${gradient};background-clip:text;color:transparent;${fixture.extra || ''}}
h1{font:inherit;margin:12px 0;padding-left:16px}.inner{background:linear-gradient(lime,lime);background-clip:text}
</style><div class="owner">${fixture.content}</div>`;
}

function inkByChannel(image, channel) {
    let ink = 0;
    for (let y = 0; y < 200; y++) for (let x = 0; x < 450; x++) {
        const i = (y * image.width + x) * 4;
        if (image.data[i + channel] > 150 && image.data[i + (channel + 1) % 3] < 80 && image.data[i + (channel + 2) % 3] < 80) ink++;
    }
    return ink;
}

const greenInk = (image) => inkByChannel(image, 1);
module.exports = {fixtures, fixtureHtml, greenInk, inkByChannel};
