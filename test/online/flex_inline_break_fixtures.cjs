const fixtures = [];
for (const contents of [false, true]) {
    for (const nestedBreak of [false, true]) {
        fixtures.push({name: `inline break, contents=${contents}, nested=${nestedBreak}`,
            contents, nestedBreak});
    }
}
fixtures.push(
    ...['inline-block', 'inline-flex', 'inline-grid'].flatMap(display => [false, true].map(wrap => ({
        name: `fractional ${display} edges ${wrap ? 'overflow and wrap' : 'fit on one line'}`,
        fractionalAtomic: true, display, wrap,
    }))),
    ...['block', 'flex', 'grid'].flatMap(parent => [24, 80].map(lineHeight => ({
        name: `inline atomic child retains ${lineHeight}px ancestor strut in ${parent}`,
        strut: true, parent, lineHeight,
    }))),
    ...['flow-root', 'block', 'inline-block'].map(display => ({name: `atomic inline letters in ${display}`, atomicLetters: true, display})),
    ...['absolute', 'fixed'].map(position => ({name: `atomic inline letters with ${position} decoration`, atomicLetters: true, display: 'block', decoration: position})),
    {name: 'mixed inline font sizes', nestedBreak: true, mixed: true},
    {name: 'preserved whitespace', nestedBreak: true, whitespace: 'pre-wrap'},
    {name: 'nonwrapping forced lines', nestedBreak: true, whitespace: 'nowrap'},
    {name: 'consecutive forced breaks', nestedBreak: true, breaks: '<br><br>'},
    {name: 'direct text and break control', direct: true},
    {name: 'hidden forced break control', nestedBreak: true, hidden: true},
    {name: 'centered nested flex retains its measured allocation', centered: true},
);

function fixtureHtml(fixture) {
    if (fixture.fractionalAtomic) return `<!doctype html><style>
body{margin:0;font:20px/30px monospace}
#row{float:left;width:${fixture.wrap ? '446.8' : '446.9125061'}px;text-align:center;background:red}
#first{display:${fixture.display};width:348.8812866px;height:30px;background:blue}
#second{display:inline-block;width:98.03125px;height:20px;background:lime}
</style><div id="row"><div id="first"></div><div id="second"></div></div>`;
    if (fixture.strut) return `<!doctype html><style>
body{margin:0}#outer{display:${fixture.parent};${fixture.parent === 'flex' ? 'flex-direction:column;' : ''}width:300px}
#column{display:flex;flex-direction:column;width:300px}
#heading{margin:0;font:20px/${fixture.lineHeight}px monospace;background:red}
#wrapper{display:block}#atom{display:inline-block;line-height:24px;width:100%;background:blue}
#band{height:10px;width:300px;background:lime}
</style><div id="outer"><div id="column"><div id="heading"><span id="wrapper"><span id="atom"><span style="display:block">ABC</span></span></span></div></div><div id="band"></div></div>`;
    if (fixture.atomicLetters) return `<!doctype html><style>
body{margin:0}#outer{display:flex;flex-direction:column;align-items:flex-start;width:300px}
#line{position:relative;display:${fixture.display};font:20px/24px monospace;background:red}
#line div{display:inline-block;position:relative;transform:translate(0,0%)}
#decoration{position:${fixture.decoration || 'absolute'};width:100%;height:2px;bottom:0}
#band{width:300px;height:10px;background:lime}
</style><div id="outer"><div id="line">${[...'ABCDEFGH'].map(letter => `<div>${letter}</div>`).join('')}${fixture.decoration ? '<span id="decoration"></span>' : ''}</div><div id="band"></div></div>`;
    if (fixture.centered) return `<!doctype html><style>
body{margin:0}*{box-sizing:border-box}
#hero{display:flex;flex-direction:column;align-items:center;justify-content:center;
width:1200px;height:800px;padding-top:84px;background:#eee}
#column{display:flex;flex-direction:column;align-items:center;justify-content:center;
width:100%;height:min-content;gap:32px;padding-bottom:80px;overflow:clip}
#wrap{display:contents}#title{display:flex;flex-direction:column;flex:none;width:100%;height:auto}
h1{margin:0;font:48px/60px monospace;text-align:center;background:red}
#button{display:flex;flex:none;width:100px;height:56px;background:lime}
</style><div id="hero"><div id="column"><div id="wrap"><div id="title">
<h1>AAAA AAAA AAAA<br><span>BBBB BBBB BBBB</span></h1>
</div></div><div id="button"></div></div></div>`;
    const breaks = fixture.breaks || '<br>';
    const boundary = fixture.nestedBreak
        ? `<span${fixture.hidden ? ' style="display:none"' : ''}>${breaks}</span>` : breaks;
    const heading = fixture.direct ? 'AAAA AAAA AAAA AAAA<br>BBBB'
        : `<span>AAAA AAAA AAAA AAAA</span>${boundary}<span>BBBB</span>`;
    return `<!doctype html><meta charset="utf-8"><style>
body{margin:0;font:20px/24px monospace}*{box-sizing:border-box}
#hero{display:flex;align-items:flex-end;width:600px;height:360px;padding:20px;background:#eee}
#row{display:flex;align-items:flex-end;justify-content:space-between;width:560px;height:min-content}
#column,#group,#title{display:flex;flex-direction:column;align-items:flex-start;flex:none;height:min-content}
#column{width:240px;gap:40px;background:red}#group{width:100%;gap:24px}#title{width:100%}
#spacer{height:42px;width:1px;flex:none}#wrap{${fixture.contents ? 'display:contents' : ''}}
h1{margin:0;font:30px/36px monospace;${fixture.whitespace ? 'white-space:' + fixture.whitespace : ''}}
${fixture.mixed ? 'h1{font-size:28px;line-height:36px}h1 span{font-size:30px}' : ''}
p{margin:0;font:12px/24px monospace}#button{width:80px;height:48px;flex:none;background:lime}
#card{width:100px;height:100px;flex:none;background:blue}
</style><div id="hero"><div id="row"><div id="column"><div id="group"><div id="spacer"></div>
<div id="wrap"><div id="title"><h1>${heading}</h1></div></div><p>first line<br>second line</p>
</div><div id="button"></div></div><div id="card"></div></div></div>`;
}

module.exports = {fixtures, fixtureHtml};
