const fixtures = [
    {name: 'direct safe area inset', style: 'margin-left:env(safe-area-inset-left)', x: 0},
    {name: 'safe area inset in math', style: 'margin-left:max(env(safe-area-inset-right),24px)', x: 24},
    {name: 'safe area insets in inherited custom properties', parent: '--edge:max(env(safe-area-inset-left),24px);--pad:12px var(--edge)', style: 'padding:var(--pad);margin-left:calc(var(--edge) * -1)', x: 0, y: 12},
    {name: 'all safe area insets in a shorthand', style: 'margin:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)', x: 0},
    {name: 'unknown environment value uses fallback', style: 'margin-left:env(unknown-area,32px)', x: 32},
    {name: 'fallback resolves custom properties', style: '--edge:36px;margin-left:env(unknown-area,var(--edge))', x: 36},
    {name: 'fallback resolves nested environment values', style: 'margin-left:env(unknown-area,max(env(safe-area-inset-left),40px))', x: 40},
    {name: 'environment names are case sensitive', style: 'margin-left:env(SAFE-AREA-INSET-LEFT,44px)', x: 44},
    {name: 'unknown environment value invalidates winning declaration', style: 'margin-left:28px;margin-left:env(unknown-area)', x: 0},
];
function fixtureHtml(fixture) {
    return `<!doctype html><style>body{margin:0;background:white}.parent{${fixture.parent || ''}}.box{width:100px;height:80px;${fixture.style}}.ink{width:80px;height:60px;background:red}</style><div class="parent"><div class="box"><div class="ink"></div></div></div>`;
}
function checkEnvironmentValue(assert, fixture, image, rgb) {
    const x = fixture.x, y = fixture.y || 0;
    assert.deepEqual(rgb(image, x + 5, y + 5), [255, 0, 0], fixture.name);
    assert.deepEqual(rgb(image, x + 85, y + 5), [255, 255, 255], fixture.name);
    if (x > 0) assert.deepEqual(rgb(image, x - 5, y + 5), [255, 255, 255], fixture.name);
}
module.exports = {fixtures, fixtureHtml, checkEnvironmentValue};
