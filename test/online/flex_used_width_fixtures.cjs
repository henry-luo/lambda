const fixtures = [];
for (const width of ['auto', '1px', '100px']) {
    for (const height of ['auto', 'min-content']) {
        for (const nested of [false, true]) {
            fixtures.push({name: `preferred ${width}, height ${height}, nested=${nested}`, width, height, nested});
        }
    }
}
fixtures.push(
    {name: 'maximum content height at assigned width', width: '100px', height: 'max-content'},
    {name: 'fit content height at assigned width', width: '100px', height: 'fit-content'},
    {name: 'intrinsic minimum height at assigned width', width: '100px', style: 'min-height:min-content'},
    {name: 'intrinsic maximum height at assigned width', width: '100px', style: 'max-height:max-content'},
    {name: 'padding and border use the content width', width: '100px', height: 'min-content', style: 'padding:10px;border:2px solid black', rowHeight: 76},
    {name: 'descendant percentages retain their own basis', width: '100px', height: 'min-content', childWidth: '50%', rowHeight: 76},
    {name: 'shrinking wraps at the assigned width', width: '500px', flex: '1 1 auto', rowWidth: 360, rowHeight: 76},
    {name: 'fixed height control', width: '100px', height: '120px', rowHeight: 120},
    {name: 'explicit zero height control', width: '100px', height: '0px', rowHeight: 40},
    {name: 'row intrinsic height inside a column', width: '1px', height: 'min-content', outerColumn: true},
);

function fixtureHtml(fixture) {
    return `<!doctype html><meta charset="utf-8"><style>
body{margin:0}*{box-sizing:border-box}#outer{${fixture.outerColumn ? 'display:flex;flex-direction:column;' : ''}width:${fixture.rowWidth || 600}px}
#row{display:flex;align-items:flex-end;width:100%;${fixture.outerColumn ? 'height:min-content;flex:none;' : ''}background:#eee}
#left{display:flex;flex-direction:column;flex:${fixture.flex || '1 0 0'};width:${fixture.width || '100px'};
height:${fixture.height || 'auto'};gap:12px;align-items:flex-start;background:red;${fixture.style || ''}}
#label{margin:0;font:12px/16px monospace}#title{${fixture.nested ? 'display:flex;flex-direction:column;' : ''}width:${fixture.childWidth || '100%'};height:auto}
#text{margin:0;font:20px/24px monospace}#right{flex:none;width:200px;height:40px;background:blue}#band{flex:none;height:10px;width:100%;background:lime}
</style><div id="outer"><div id="row"><div id="left"><p id="label">caption</p>
<div id="title"><p id="text">AAAA BBBB CCCC DDDD</p></div></div><div id="right"></div></div><div id="band"></div></div>`;
}

module.exports = {fixtures, fixtureHtml};
