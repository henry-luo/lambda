const fixtures = [
    {name: 'literal width control', width: '200px'},
    {name: 'variable width', width: 'var(--size)'},
    {name: 'calculated percentage width', width: 'calc(100% - 360px)'},
    {name: 'minimum function width', width: 'min(200px, 100%)'},
    {name: 'maximum function width', width: 'max(200px, 25%)'},
    {name: 'clamped function width', width: 'clamp(160px, 50%, 200px)'},
    {name: 'variable fallback width', width: 'var(--missing, 200px)'},
    {name: 'maximum width clamps percentage', width: '100%', style: 'max-width:200px'},
    {name: 'calculated maximum clamps auto', width: 'auto', style: 'max-width:calc(100% - 360px)'},
    {name: 'minimum width clamps preferred width', width: '100px', style: 'min-width:200px'},
    {name: 'variable content-box width with edges', width: 'var(--size)', style: 'box-sizing:content-box;padding:10px;border:2px solid black', cardHeight: 264},
    {name: 'variable border-box width with edges', width: 'var(--size)', style: 'padding:10px;border:2px solid black'},
    {name: 'percentage border-box width with edges', width: '50%', style: 'padding:10px;border:2px solid black', cardHeight: 320},
    {name: 'calculated border-box width with edges', width: 'calc(50%)', style: 'padding:10px;border:2px solid black', cardHeight: 320},
    {name: 'own font-relative width control', width: '20em', style: 'font-size:10px'},
    {name: 'own font-relative calculated width', width: 'calc(20em)', style: 'font-size:10px'},
];

function fixtureHtml(fixture) {
    return `<!doctype html><style>body{margin:0}*{box-sizing:border-box}
#outer{display:flex;flex-direction:column;width:600px;--size:200px}
#card{width:100%;padding:20px;background:#eee}#narrow{width:${fixture.width};margin:auto;${fixture.style || ''}}
img{display:block;width:100%;height:auto;aspect-ratio:1}#band{height:10px;flex:none;background:lime}
</style><div id="outer"><div id="card"><div id="narrow"><img src="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='200' height='200'%3E%3Crect width='200' height='200' fill='red'/%3E%3C/svg%3E"></div></div><div id="band"></div></div>`;
}

module.exports = {fixtures, fixtureHtml};
