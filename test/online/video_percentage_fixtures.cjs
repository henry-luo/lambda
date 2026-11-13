const poster = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='200' height='100'%3E%3Crect width='200' height='100' fill='red'/%3E%3C/svg%3E";
const fixtures = [
    {name: 'video percentage height in an auto parent uses its natural ratio', height: 160},
    {name: 'a smaller cyclic percentage also becomes auto', media: 'height:50%', height: 160},
    {name: 'explicit auto height control', media: 'height:auto', height: 160},
    {name: 'a definite parent resolves the percentage height', parent: 'height:240px', height: 240},
    {name: 'a definite parent resolves a smaller percentage', parent: 'height:240px', media: 'height:50%', height: 120, outerHeight: 240},
    {name: 'a definite zero parent retains zero video height', parent: 'height:0', height: 0},
    {name: 'a cyclic video without natural dimensions retains its default height', noPoster: true, height: 150},
    {name: 'an explicit preferred ratio overrides the natural ratio', media: 'aspect-ratio:1', height: 320},
    {name: 'an auto preferred ratio retains the natural ratio', media: 'aspect-ratio:auto 1', height: 160},
    {name: 'border-box without decorations retains the natural ratio', media: 'box-sizing:border-box', height: 160},
    {name: 'content-box decorations stay outside the natural ratio', media: 'padding:10px;border:2px solid black', height: 184},
    {name: 'border-box decorations stay outside the natural ratio', media: 'padding:10px;border:2px solid black;box-sizing:border-box', height: 172},
    {name: 'the neighboring image percentage-height path remains intact', tag: 'img', height: 160},
];

function fixtureHtml(fixture) {
    const tag = fixture.tag || 'video';
    const attributes = fixture.noPoster ? '' : ' ' + (tag === 'img' ? 'src' : 'poster') + '="' + poster + '"';
    return '<!doctype html><html><head><style>html,body{margin:0;background:white}' +
        '#frame{width:320px;' + (fixture.parent || '') + '}' +
        '#media{display:block;width:100%;height:100%;background:red;' + (fixture.media || '') + '}' +
        '#band{height:10px;background:lime}</style></head><body><div id="frame">' +
        '<' + tag + ' id="media"' + attributes + '>' + (tag === 'img' ? '' : '</video>') +
        '</div><div id="band"></div></body></html>';
}

function witnesses(fixture) {
    const outer = fixture.outerHeight ?? fixture.height;
    const points = [[10, outer + 4, [0, 255, 0]], [10, outer + 15, [255, 255, 255]]];
    if (!fixture.noPoster && fixture.height > 0) {
        points.push([100, fixture.height - 5, [255, 0, 0]]);
        if (fixture.height < outer) points.push([100, fixture.height + 4, [255, 255, 255]]);
    }
    return points;
}

module.exports = {fixtures, fixtureHtml, witnesses};
