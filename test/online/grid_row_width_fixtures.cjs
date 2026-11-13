const fixtures = [];
for (const flex of [false, true]) {
    for (const imageHeight of ['auto', '100%']) {
        fixtures.push({name: `percentage grid item, image ${imageHeight}, flex=${flex}`, flex, imageHeight});
    }
}
fixtures.push(
    {name: 'half-width grid item', width: '50%'},
    {name: 'calculated percentage grid item', width: 'calc(100% - 16px)'},
    {name: 'automatic grid item width', width: 'auto'},
    {name: 'fixed overflowing grid item width', width: '320px', rowHeight: 320},
    {name: 'zero grid item width', width: '0px'},
    {name: 'border-box percentage grid item', style: 'padding:10px;border:2px solid black'},
    {name: 'maximum grid item width', style: 'max-width:100px'},
    {name: 'minimum grid item width', style: 'min-width:320px', rowHeight: 320},
);

function fixtureHtml(fixture) {
    return `<!doctype html><style>body{margin:0}*{box-sizing:border-box}
#section{width:600px;${fixture.flex ? 'display:flex;align-items:center;min-height:400px;' : ''}background:#eee}
#wrapper{width:100%}#grid{display:grid;width:100%;grid-template-columns:1fr 1fr 1fr;gap:16px}
#center{min-width:280px;height:200px;background:blue}#left,#right{overflow:hidden;background:yellow}#left{width:100%}#right{width:${fixture.width || '100%'};${fixture.style || ''}}
img{display:block;width:100%;height:${fixture.imageHeight || 'auto'};aspect-ratio:1;object-fit:contain}
#band{height:10px;background:lime}</style><div id="section"><div id="wrapper"><div id="grid"><div id="left"></div>
<div id="center"></div><div id="right"><img src="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='200' height='200'%3E%3Crect width='200' height='200' fill='red'/%3E%3C/svg%3E"></div>
</div></div></div><div id="band"></div>`;
}

module.exports = {fixtures, fixtureHtml};
