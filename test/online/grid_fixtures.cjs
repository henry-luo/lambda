const fixtures = [
    {name: 'full percentage uses its grid area', style: 'width:100%', width: 548, peerX: 612},
    {name: 'half percentage does not enlarge the track', style: 'width:50%', width: 274, peerX: 612},
    {name: 'calculated percentage uses its final area', style: 'width:calc(100% - 0px)', width: 548, peerX: 612},
    {name: 'percentage math uses the item font', style: 'font-size:20px;width:calc(100% - 1em)', width: 528, peerX: 612},
    {name: 'substituted math retains its percentage dependency',
        style: '--fill:50%;font-size:20px;width:calc(var(--fill) + 1em)', width: 294, peerX: 612},
    {name: 'automatic width retains normal stretching', style: 'width:auto', width: 548, peerX: 612},
    {name: 'definite width retains its intrinsic floor', style: 'width:600px', width: 600, peerX: 664},
    {name: 'authored minimum remains effective', style: 'width:100%;min-width:300px', width: 548, peerX: 612},
    {name: 'authored maximum constrains the used item', style: 'width:100%;max-width:300px', width: 300, peerX: 612},
    {name: 'percentage maximum uses its final area', style: 'width:auto;max-width:50%', width: 274, peerX: 612}
];

function fixtureHtml(fixture) {
    return '<!doctype html><style>body{margin:0}.grid{display:grid;width:1160px;' +
        'grid-template-columns:1fr 1fr;gap:64px}.column{display:flex;flex-direction:column;' +
        'align-items:flex-start;background:red;' + fixture.style + '}' +
        '.copy{width:160px;height:80px}.picture{width:560px;height:80px;margin-right:-180px;background:blue}' +
        '</style><div class="grid"><div id="probe" class="column"><div class="copy"></div></div>' +
        '<div id="peer"><div class="picture"></div></div></div><script>' +
        'console.log("GRID_GEOMETRY",document.getElementById("probe").getBoundingClientRect().width,' +
        'document.getElementById("peer").getBoundingClientRect().x);</script>';
}

module.exports = {fixtures, fixtureHtml};
