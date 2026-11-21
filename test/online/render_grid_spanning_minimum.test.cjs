const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult} = require('./render_fixture.cjs');
const render = renderFixtures('grid-spanning-minimum');

const cases = [
    {name: 'spanning flexible tracks zero the automatic minimum', tracks: '1fr 1fr', span: 2, minimum: '', width: 400},
    {name: 'zero fr tracks still zero a spanning automatic minimum', tracks: '0fr 0fr', span: 2, minimum: '', width: 0},
    {name: 'spanning flexible rows zero the automatic block minimum', tracks: '1fr 1fr', span: 2, minimum: '', width: 400, rows: true},
    {name: 'a single flexible track retains its content minimum', tracks: '1fr 1fr', span: 1, minimum: '', width: 600},
    {name: 'an explicit item minimum survives flexible spanning', tracks: '1fr 1fr', span: 2, minimum: 'min-width:650px', width: 650},
    {name: 'intrinsic track minima survive flexible spanning', tracks: 'minmax(min-content,1fr) minmax(min-content,1fr)', span: 2, minimum: '', width: 600}
];
for (const [index, fixture] of cases.entries()) test(fixture.name, async () => {
    const {log} = await render('case-' + index, `<!doctype html><style>
        body{margin:0}#grid{display:grid;width:400px;${fixture.rows ? 'height:400px;grid-template-rows' : 'grid-template-columns'}:${fixture.tracks}}
        #item{grid-${fixture.rows ? 'row' : 'column'}:span ${fixture.span};${fixture.rows ? 'width' : 'height'}:60px;background:green;${fixture.minimum}}
        #wide{${fixture.rows ? 'height:600px;width:40px' : 'width:600px;height:40px'};background:red}
        </style><div id="grid"><div id="item"><div id="wide"></div></div></div><script>
        console.log('GRID_SPAN_MINIMUM', JSON.stringify({width:document.getElementById('item').getBoundingClientRect().${fixture.rows ? 'height' : 'width'}}));
        </script>`);
    assert.equal(loggedResult(log, 'GRID_SPAN_MINIMUM').width, fixture.width);
});
