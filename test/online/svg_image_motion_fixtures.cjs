const cases = [
    {name: 'explicit opacity endpoint', style: 'opacity:0;animation:fade .5s linear forwards',
        frames: '@keyframes fade{from{opacity:0}to{opacity:1}}', expected: [0,255,0]},
    {name: 'implicit opacity endpoint', style: 'animation:fade .5s linear forwards',
        frames: '@keyframes fade{from{opacity:0}}', expected: [0,255,0]},
    {name: 'variable percentage endpoint', style: '--alpha:50%;opacity:0;animation:fade .5s linear forwards',
        frames: '@keyframes fade{from{opacity:0}to{opacity:var(--alpha)}}', expected: [128,255,128]},
    {name: 'selected midpoint', style: 'opacity:0;animation:fade 2s linear forwards',
        frames: '@keyframes fade{from{opacity:0}to{opacity:1}}', expected: [128,255,128]},
    {name: 'transform endpoint', style: 'transform:translateX(100px);animation:shift .5s linear forwards',
        frames: '@keyframes shift{from{transform:translateX(100px)}to{transform:translateX(0)}}', expected: [0,255,0]},
    {name: 'CSS background image clock', style: 'opacity:0;animation:fade .5s linear forwards',
        frames: '@keyframes fade{from{opacity:0}to{opacity:1}}', background: true, expected: [0,255,0]},
    {name: 'authored pause', style: 'opacity:0;animation:fade .5s linear forwards paused',
        frames: '@keyframes fade{from{opacity:0}to{opacity:1}}', expected: [255,255,255]},
    {name: 'authored delay', style: 'opacity:0;animation:fade .5s linear 2s forwards',
        frames: '@keyframes fade{from{opacity:0}to{opacity:1}}', expected: [255,255,255]},
    {name: 'static isolated CSS', style: 'opacity:.5', expected: [128,255,128]},
    {name: 'SMIL clock control', style: '',
        content: '<animate attributeName="opacity" from="0" to="1" dur=".5s" fill="freeze"/>', expected: [0,255,0]},
];

function fixtureHtml(fixture) {
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200">
<style>g{${fixture.style}}${fixture.frames || ''}</style>
<g><rect width="200" height="200" fill="lime"/>${fixture.content || ''}</g></svg>`;
    const url = `data:image/svg+xml,${encodeURIComponent(svg)}`;
    return `<!doctype html><style>body{margin:0;background:white}svg g{opacity:0!important}</style>
${fixture.background ? `<div style="width:200px;height:200px;background-image:url('${url}')"></div>`
    : `<img width="200" height="200" src="${url}">`}`;
}

module.exports = {cases, fixtureHtml};
