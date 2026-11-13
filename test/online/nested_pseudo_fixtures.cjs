const selectors = ['a:hover', 'p::before', 'div:is(.active,.other)',
    'span:not(.other)', 'input:checked', 'font:hover'];
const fixtures = ['style', 'media', 'supports'].flatMap(context => selectors.map(selector =>
    ({name: context + ': ' + selector, context, selector})));
fixtures.push({name: 'custom component blocks stay declarations', context: 'style',
    prefix: '--tokens:{a:hover{width:1px}};width:{var(--size)};'});
fixtures.push({name: 'function fallback blocks stay inside the declaration', context: 'supports',
    prefix: 'width:var(--size,{var(--fallback)});'});

function fixtureHtml(fixture) {
    const body = (fixture.prefix || fixture.selector + '{color:green}') +
        '@media(min-width:900px){.child{width:100px;background:red}}';
    const contents = fixture.context === 'media' ? '@media(min-width:1px){' + body + '}' :
        fixture.context === 'supports' ? '@supports(display:block){' + body + '}' : body;
    return '<!doctype html><style>body{margin:0}.outer{width:200px}' +
        '.child{width:200px;height:100px;background:blue}.outer{' + contents +
        '}</style><div class="outer"><div class="child"></div></div>';
}
module.exports = {fixtures, fixtureHtml};
