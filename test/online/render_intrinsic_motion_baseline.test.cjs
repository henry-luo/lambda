const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('intrinsic-motion-baseline');

for (const display of ['block', 'flex', 'grid']) {
    for (const ending of ['implicit', 'explicit', 'authored-opacity', 'variable-opacity', 'inherited-opacity']) {
        test(`intrinsic ${display} sizing preserves ${ending} animation baseline`, async () => {
            const half = ending === 'authored-opacity' || ending === 'variable-opacity';
            const inherited = ending === 'inherited-opacity';
            const source = `<!doctype html><style>
body{margin:0;background:red}main{display:${display};flex-direction:column;width:300px}
section{position:relative;overflow:hidden;${inherited ? 'opacity:.5' : ''}}
#box{width:300px;height:200px;background:lime;
${inherited ? 'opacity:inherit;' : ending === 'variable-opacity' ? '--fade-opacity:50%;opacity:var(--fade-opacity);' : half ? 'opacity:.5;' : ''}
animation:.7s linear 50ms both fade}
@keyframes fade{from{opacity:0}${ending === 'explicit' ? 'to{opacity:1}' : ''}}
</style><main><section><div id="box"></div></section></main>`;
            const {image} = await render(`${display}-${ending}`, source,
                {...process.env, LAMBDA_POST_LOAD_SETTLE_MS: '1000'});
            const actual = rgb(image, 50, 50);
            const expected = inherited ? [191, 64, 0] : half ? [128, 128, 0] : [0, 255, 0];
            assert.ok(actual.every((value, index) => Math.abs(value - expected[index]) <= 1),
                `completed fade: ${actual}, expected ${expected}`);
        });
    }
}
