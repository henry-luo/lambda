const gradientDoubleStopCases = [];
for (const [kind, prelude] of [
    ['linear', 'to bottom'], ['radial', 'ellipse at center'], ['conic', 'from 90deg at center'],
]) {
    for (const repeating of [false, true]) {
        for (const explicit of [false, true]) {
            gradientDoubleStopCases.push({
                name:(repeating ? 'repeating-' : '') + kind + (explicit ? '-prelude' : ''),
                fn:(repeating ? 'repeating-' : '') + kind + '-gradient',
                prelude:explicit ? prelude + ',' : '',
            });
        }
    }
}

function gradientDoubleStopHtml(fixture, expanded = false) {
    const stops = expanded ? '#010a14 0%,#010a14 100%' : '#010a14 0% 100%';
    return '<!doctype html><style>html,body{margin:0;background:white}' +
        '#fill{width:160px;height:160px;background:' + fixture.fn + '(' + fixture.prelude + stops + ')}' +
        '</style><div id="fill"></div>';
}

module.exports = {gradientDoubleStopCases, gradientDoubleStopHtml};
