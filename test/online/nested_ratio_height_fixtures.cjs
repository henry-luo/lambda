const fixtures = [
    {name: 'fixed ratio icon in a nested flex item', style: 'width:16px', height: 48},
    {name: 'calculated ratio icon width', style: 'width:calc(8px + 8px)', height: 48},
    {name: 'ratio width uses its own font and variable', style: '--side:1em;font-size:20px;width:var(--side)', height: 52},
    {name: 'ratio width retains its authored maximum', style: 'width:100px;max-width:16px', height: 48},
    {name: 'non-square ratio transfers the resolved width', style: 'width:32px;aspect-ratio:2', height: 48},
    {name: 'empty ratio icon retains natural sizing', style: 'width:16px', height: 48, empty: true},
    {name: 'definite height overrides the preferred ratio', style: 'width:16px;height:80px', height: 112},
];
function fixtureHtml(fixture) {
    const svg = '<svg width="100%" height="100%" viewBox="0 0 16 16"><rect width="16" height="16" fill="lime"/></svg>';
    return `<!doctype html><style>body{margin:0;font:16px Arial}.form{display:flex;width:500px;gap:12px}.field{width:240px;height:44px;flex:none;background:blue}.button{display:flex;align-items:center;padding:16px 20px;line-height:16px;background:red}.wrapper{display:flex;overflow:clip}.icon{aspect-ratio:1;flex:none;display:flex;${fixture.style}}.copy{width:100px;height:16px;flex:none}.following{width:500px;height:10px;background:cyan}</style><div class="form"><div class="field"></div><div class="button"><div class="wrapper"><div class="icon">${fixture.empty ? '' : svg}</div></div><div class="copy"></div></div></div><div class="following"></div>`;
}
module.exports = {fixtures, fixtureHtml};
