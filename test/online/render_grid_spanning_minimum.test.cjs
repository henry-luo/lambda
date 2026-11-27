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

for (const rows of [true, false]) {
    const tracks = rows ? 'rows' : 'columns';
    const axis = rows ? 'row' : 'column';
    const size = rows ? 'height' : 'width';
    const start = rows ? 'y' : 'x';
    const cssom = rows ? 'gridTemplateRows' : 'gridTemplateColumns';
    test('indefinite flexible ' + tracks + ' share content space without absorbing gaps', async () => {
        const {log} = await render('indefinite-' + tracks, `<!doctype html><style>
            body{margin:0}#grid{display:grid;width:${rows ? '100px' : 'max-content'};
                grid-template-${tracks}:auto 1fr auto 1fr;${axis}-gap:12px}
            #first{grid-${axis}:1/3}#second{grid-${axis}:3/5}
            #first div{${size}:30px}#second div{${size}:70px}
            </style><div id="grid"><div id="first"><div></div></div><div id="second"><div></div></div></div><script>
            const grid=document.getElementById('grid'),first=document.getElementById('first'),second=document.getElementById('second');
            console.log('GRID_INDEFINITE_FRACTIONS '+JSON.stringify({tracks:getComputedStyle(grid).${cssom},
                extent:grid.getBoundingClientRect().${size},first:first.getBoundingClientRect().${size},
                secondStart:second.getBoundingClientRect().${start},second:second.getBoundingClientRect().${size}}));
            </script>`);
        assert.deepEqual(loggedResult(log, 'GRID_INDEFINITE_FRACTIONS'), {
            tracks: '0px 58px 0px 58px', extent: 152, first: 70, secondStart: 82, second: 70,
        });
    });
}
