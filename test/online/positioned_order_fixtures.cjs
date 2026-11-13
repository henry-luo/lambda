const fixtures = [
    {name: 'early absolute background precedes later relative content'},
    {name: 'later absolute background covers earlier relative content', reverse: true, covered: true},
    {name: 'explicit zero absolute context preserves tree order', background: 'z-index:0'},
    {name: 'explicit zero relative context preserves tree order', foreground: 'z-index:0'},
    {name: 'later absolute auto paints over earlier explicit zero context', foreground: 'z-index:0', reverse: true, covered: true},
    {name: 'sticky foreground shares the zero-level sequence', foreground: 'position:sticky;top:40px'},
    {name: 'positioned inline foreground keeps generic view dispatch', inline: true},
    {name: 'absolute descendant collected through an earlier static wrapper', nested: true},
    {name: 'absolute descendant collected through a later static wrapper', nested: true, reverse: true, covered: true},
    {name: 'positive absolute context remains above later relative content', background: 'z-index:1', covered: true},
    {name: 'negative absolute context remains behind relative content', background: 'z-index:-1'},
    {name: 'a static parent still paints relative children', noBackground: true, parent: 'position:static'},
    {name: 'a parent overflow clip still clips its foreground', parent: 'height:100px;overflow:hidden', clipped: true},
    {name: 'an opacity context paints its foreground only once', foreground: 'opacity:.5', blended: true},
    {name: 'deep absolute ancestry before a relative sibling', nestedDepth: 80},
    {name: 'deep absolute ancestry after a relative sibling', nestedDepth: 80, reverse: true, covered: true},
];

function fixtureHtml(fixture) {
    let background = '<div id="background"></div>';
    if (fixture.nested) background = '<div class="wrapper">' + background + '</div>';
    if (fixture.nestedDepth) background = '<div class="wrapper">'.repeat(fixture.nestedDepth) +
        background + '</div>'.repeat(fixture.nestedDepth);
    if (fixture.noBackground) background = '';
    const foreground = fixture.inline
        ? '<span id="foreground"><i id="marker"></i></span>'
        : '<div id="foreground"><i id="marker"></i></div>';
    return '<!doctype html><style>html,body{margin:0;background:white}' +
        '#hero{position:relative;width:600px;height:400px;font-size:0;line-height:0;' + (fixture.parent || '') + '}' +
        '.wrapper{height:0}#background{position:absolute;inset:0;background:navy;' + (fixture.background || '') + '}' +
        '#foreground{position:relative;left:40px;top:40px;width:100px;height:100px;' + (fixture.foreground || '') + '}' +
        '#marker{display:inline-block;vertical-align:top;width:100px;height:100px;background:lime}' +
        '</style><section id="hero">' + (fixture.reverse ? foreground + background : background + foreground) + '</section>';
}

function witnesses(fixture) {
    return [[60, 60, fixture.covered ? [0, 0, 128] : fixture.blended ? [0, 128, 64] : [0, 255, 0]],
        [60, 120, fixture.clipped ? [255, 255, 255] : fixture.covered ? [0, 0, 128] : fixture.blended ? [0, 128, 64] : [0, 255, 0]]];
}

module.exports = {fixtures, fixtureHtml, witnesses};
