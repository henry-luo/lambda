const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./flex_ratio_constraint_fixtures.cjs');
const render=renderFixtures('flex-ratio-constraints');
for(const [index,fixture]of fixtures.entries())test('flex ratio constraints: '+fixture.name,async()=>{
    const {image,log}=await render('ratio-'+index,fixtureHtml(fixture));
    const match=/FLEX_RATIO_MEASURE (\[[^\n]+\])/.exec(log);
    assert.ok(match,log.slice(-2000));
    const actual=JSON.parse(match[1]);
    assert.ok(actual.every((value,i)=>Math.abs(value-fixture.expected[i])<=1),
        'got '+actual+'; expected '+fixture.expected);
    assert.deepEqual(rgb(image,fixture.expected[0]+Math.min(50,fixture.expected[2]/2),
        fixture.expected[1]+Math.min(50,fixture.expected[3]/2)),[0,255,0]);
});

test('flex containers keep the height transferred from their preferred aspect ratio', async () => {
    const source = `<!doctype html><style>
        html, body, p { margin: 0 }
        .box { display: flex; flex-direction: column; justify-content: center;
            width: 240px; aspect-ratio: 8/5; font: 20px/20px sans-serif }
        .row { flex-direction: row; align-items: center }
        .grow > div { flex: 1 }
        .tall > div { height: 220px; flex: none }
        .scroll { overflow: hidden }
        .max { max-height: 100px }
        .bordered { box-sizing: border-box; border: 4px solid }
        .wrapper { display: flex; flex-direction: column }
        .wrap { align-items: center }
        .wrap > div { display: flex; flex-direction: column; width: 100%; max-width: 100px;
            font: 20px/20px monospace }
    </style>
    <div class="box" id="column"><div>Text</div></div>
    <div class="box row" id="row"><div>Text</div></div>
    <div class="box grow" id="grow"><div>Text</div></div>
    <div class="box tall" id="tall"><div>Text</div></div>
    <div class="box tall scroll" id="scroll"><div>Text</div></div>
    <div class="box max" id="max"><div>Text</div></div>
    <div class="box bordered" id="bordered"><div>Text</div></div>
    <div class="wrapper"><div class="box scroll wrap" id="nested">
        <div><div><p>Words in a title that wrap at least once</p></div></div>
    </div></div>
    <script>
        const sizes = {};
        for (const box of document.querySelectorAll('.box')) {
            const container = box.getBoundingClientRect();
            const child = box.firstElementChild.getBoundingClientRect();
            sizes[box.id] = [container.height, child.height, child.top - container.top];
        }
        console.log('FLEX_RATIO_CONTAINER ' + JSON.stringify(sizes));
    </script>`;

    const {log} = await render('ratio-container-height', source);
    const {nested, ...boxes} = loggedResult(log, 'FLEX_RATIO_CONTAINER');
    assert.deepEqual(boxes, {
        column: [150, 20, 65], row: [150, 20, 65], grow: [150, 150, 0],
        tall: [220, 220, 0], scroll: [150, 220, -35], max: [100, 20, 40],
        bordered: [150, 20, 65],
    });
    assert.equal(nested[0], 150);
    assert.ok(nested[1] > 20 && nested[1] < 150, 'the nested title wraps inside the container');
    assert.equal(nested[2], (150 - nested[1]) / 2);
});
