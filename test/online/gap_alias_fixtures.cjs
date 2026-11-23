const fixtures = [
    {name: 'standard column gap', gap: 'column-gap:4px'},
    {name: 'font relative column gap', gap: 'column-gap:.25rem'},
    {name: 'legacy column gap', gap: 'grid-column-gap:.25rem'},
    {name: 'legacy variable column gap', gap: '--space:4px;grid-column-gap:var(--space)'},
    {name: 'legacy gap shorthand', gap: 'grid-gap:4px'},
    {name: 'later legacy alias wins', gap: 'column-gap:8px;grid-column-gap:4px'},
    {name: 'later standard spelling wins', gap: 'grid-column-gap:8px;column-gap:4px'},
    {name: 'important legacy alias wins', gap: 'grid-column-gap:4px!important;column-gap:8px'},
];

function fixtureHtml(fixture) {
    return `<!doctype html><style>html{font-size:16px}body{margin:0}.nav{display:flex;width:500px}.item{display:inline-block}.inner{display:flex;padding:26px 12px;${fixture.gap}}.label{width:64px;height:24px;background:red}.second .label{width:48px}.icon{width:16px;height:24px;background:blue}</style><div class="nav"><div id="wrapper"><div class="item"><div class="inner"><div class="label"></div><div class="icon"></div></div></div><div class="item second"><div class="inner"><div class="label"></div><div class="icon"></div></div></div></div></div><script>var w=document.getElementById('wrapper').getBoundingClientRect(),a=document.querySelectorAll('.item')[0].getBoundingClientRect(),b=document.querySelectorAll('.item')[1].getBoundingClientRect();console.log('GAP_ALIAS',JSON.stringify({width:w.width,a:a.width,b:b.width,y1:a.y,y2:b.y}));</script>`;
}

function checkGapAlias(assert, result) {
    assert.equal(result.a, 108);
    assert.equal(result.b, 92);
    assert.equal(result.width, 200, 'intrinsic width includes both nested gaps');
    assert.equal(result.y1, result.y2, 'navigation items remain on the same line');
}

module.exports = {fixtures, fixtureHtml, checkGapAlias};
