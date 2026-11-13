const fixtures = [
    {name: 'grid direction from a custom property', style: '--flow:column;grid-auto-flow:var(--flow)', column: true},
    {name: 'grid direction and packing from a custom property', style: '--flow:column dense;grid-auto-flow:var(--flow)', column: true},
    {name: 'grid direction variable within a packing list', style: '--direction:column;grid-auto-flow:var(--direction) dense', column: true},
    {name: 'grid direction from a variable fallback', style: 'grid-auto-flow:var(--missing,column)', column: true},
    {name: 'invalid substituted flow resets at computed value time', style: '--flow:bogus;grid-auto-flow:column;grid-auto-flow:var(--flow)', column: false},
];
function fixtureHtml(fixture) {
    return `<!doctype html><style>body{margin:0}.grid{display:grid;width:240px;height:240px;grid-auto-rows:80px;grid-auto-columns:80px;align-content:start;justify-content:start;${fixture.style}}.grid div{width:80px;height:80px}.first{background:red}.second{background:blue}</style><div class="grid"><div class="first"></div><div class="second"></div></div>`;
}
module.exports = {fixtures, fixtureHtml};
