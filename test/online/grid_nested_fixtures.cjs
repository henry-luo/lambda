const fixtures = [
    {name: 'automatic parent retains its nested grid content height', parent: '', child: '', height: 134, parentHeight: 134, y: 0},
    {name: 'start-aligned nested grid keeps its natural height', parent: 'height:300px', child: '', height: 134, parentHeight: 300, y: 0},
    {name: 'center-aligned nested grid uses its natural border box', parent: 'height:300px;align-items:center', child: '', height: 134, parentHeight: 300, y: 83},
    {name: 'stretched nested grid retains its assigned height', parent: 'height:300px;align-items:stretch', child: '', height: 300, parentHeight: 300, y: 0},
    {name: 'an authored nested grid height remains effective', parent: 'height:300px', child: 'height:180px', height: 204, parentHeight: 300, y: 0}
];

function fixtureHtml(fixture) {
    return '<!doctype html><style>body{margin:0}#outer{display:grid;width:300px;align-items:start;' +
        'grid-template-columns:1fr;' + fixture.parent + '}#inner{display:grid;grid-template-columns:1fr;' +
        'grid-template-rows:40px 60px;gap:10px;padding:10px 20px;border:2px solid black;' +
        'background:yellow;' + fixture.child + '}#first{background:red}#second{background:blue}</style>' +
        '<div id="outer"><div id="inner"><div id="first"></div><div id="second"></div></div></div>' +
        '<script>var inner=document.getElementById("inner").getBoundingClientRect();' +
        'var outer=document.getElementById("outer").getBoundingClientRect();' +
        'console.log("NESTED_GRID_GEOMETRY",JSON.stringify([inner.height,outer.height,inner.y]));</script>';
}

module.exports = {fixtures, fixtureHtml};
