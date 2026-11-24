const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const render = renderFixtures('selector-identifiers');

for (const kind of ['double-hyphen', 'invalid-has', 'invalid-not']) {
    test('selector identifiers and failed functions: ' + kind, async () => {
        const rule = kind === 'double-hyphen' ? 'body > div:has(.--marker)' :
            '.page:' + (kind === 'invalid-has' ? 'has' : 'not') + '(.marker,:unknown-pseudo)';
        const {image, log} = await render(kind, `<!doctype html><style>
            body{margin:0}#page{width:120px;height:100px;background:blue}
            ${rule}{display:none}
            </style><div id=page class=page><span>page</span></div>
            <div id=cookie><section><div class=--marker>cookie</div></section></div>
            <script>
            const failures=[],page=document.getElementById('page'),cookie=document.getElementById('cookie');
            const marker=cookie.querySelector('div');
            function check(ok,message){if(!ok)failures.push(message)}
            function visible(expected){check(page.getBoundingClientRect().width===(expected?120:0),'page visibility')}
            try {
                visible(true);
                if(${JSON.stringify(kind)}==='double-hyphen'){
                    check(document.querySelector('.--marker')===marker,'class selector query');
                    check(!page.matches(':has(.--marker)'),'unrelated witness');
                    page.appendChild(marker);visible(false);
                    check(page.matches(':has(.--marker)'),'descendant witness');
                    cookie.appendChild(marker);visible(true);
                } else {
                    let rejected=false;try{document.querySelector(${JSON.stringify(rule)})}catch(e){rejected=e.name==='SyntaxError'}
                    check(rejected,'invalid selector query rejected');
                }
            } catch(error){failures.push(error.name+': '+error.message)}
            console.log('SELECTOR_IDENTIFIER_RESULT',JSON.stringify({failures}));
            </script>`);
        assert.deepEqual(loggedResult(log, 'SELECTOR_IDENTIFIER_RESULT'), {failures:[]});
        assert.deepEqual(rgb(image, 30, 40), [0, 0, 255]);
    });
}
