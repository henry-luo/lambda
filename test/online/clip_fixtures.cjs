const fixtures = [
    {name: 'literal closed inset', clip: 'inset(0 0 100% 0)', points: [[100, 40, 'blue']]},
    {name: 'calculated closed inset', clip: 'inset(0 0 calc((1 - 0) * 100%) 0)', points: [[100, 40, 'blue']]},
    {name: 'variable fallback closes the inset', clip: 'inset(0 0 calc((1 - var(--open,0)) * 100%) 0)', points: [[100, 40, 'blue']]},
    {name: 'variable math keeps a partially open inset', style: '--open:.5', clip: 'inset(0 0 calc((1 - var(--open)) * 100%) 0)',
        points: [[100, 25, 'red'], [100, 75, 'blue']]},
    {name: 'the entire clip can come from a variable', style: '--clip:inset(0 0 50% 0)', clip: 'var(--clip)',
        points: [[100, 25, 'red'], [100, 75, 'blue']]},
    {name: 'inset shorthand percentages use both box axes', clip: 'inset(10%)',
        points: [[15, 50, 'blue'], [25, 50, 'red'], [100, 5, 'blue'], [100, 15, 'red']]},
    {name: 'inset mixes nonlinear math and percentages', clip: 'inset(clamp(5px,10%,20px) min(30px,20%) max(10px,20%) calc(10px + 10%))',
        points: [[10, 50, 'blue'], [40, 50, 'red'], [180, 50, 'blue'], [100, 90, 'blue']]},
    {name: 'font-relative shape lengths use the owner font', style: 'font-size:20px', clip: 'inset(1em)',
        points: [[10, 50, 'blue'], [25, 50, 'red'], [100, 10, 'blue'], [100, 25, 'red']]},
    {name: 'circle radius math uses its diagonal reference', clip: 'circle(calc(20px + 10%) at 50% 50%)',
        points: [[100, 50, 'red'], [130, 50, 'red'], [140, 50, 'blue'], [100, 90, 'blue']]},
    {name: 'ellipse lengths and keyword center share CSS computation', clip: 'ellipse(calc(10% + 20px) calc(10% + 10px) at center)',
        points: [[100, 50, 'red'], [130, 50, 'red'], [150, 50, 'blue'], [100, 75, 'blue']]},
    {name: 'polygon coordinates resolve math on their own axes', clip: 'polygon(calc(10% + 10px) 10%,90% 10%,90% calc(100% - 10px),calc(10% + 10px) calc(100% - 10px))',
        points: [[10, 50, 'blue'], [40, 50, 'red'], [100, 95, 'blue'], [190, 50, 'blue']]},
    {name: 'a script variable mutation refreshes the clip', style: '--open:0', clip: 'inset(0 0 calc((1 - var(--open)) * 100%) 0)',
        script: 'document.getElementById("probe").style.setProperty("--open",".5");',
        points: [[100, 25, 'red'], [100, 75, 'blue']]},
    {name: 'shape lengths respect CSS zoom', style: 'zoom:2', clip: 'inset(calc(10px + 10%))',
        points: [[50, 100, 'blue'], [70, 100, 'red'], [200, 30, 'blue'], [200, 50, 'red'], [390, 100, 'blue']]},
    {name: 'default circle radius is the closest side', clip: 'circle()',
        points: [[100, 50, 'red'], [145, 50, 'red'], [160, 50, 'blue'], [100, 90, 'red']]},
    {name: 'circle percentages use the normalized diagonal', clip: 'circle(50% at 50% 50%)',
        points: [[30, 50, 'red'], [100, 50, 'red'], [190, 50, 'blue']]},
    {name: 'overconstrained insets remain empty', clip: 'inset(70% 80%)', points: [[100, 40, 'blue']]},
    {name: 'invalid math dimensions do not fabricate a clip', clip: 'inset(calc(1s + 10px))', points: [[100, 40, 'red']]},
    {name: 'literal ellipse remains supported', clip: 'ellipse(50px 30px at 100px 50px)',
        points: [[100, 50, 'red'], [140, 50, 'red'], [160, 50, 'blue'], [100, 90, 'blue']]}
];

function fixtureHtml(fixture) {
    return '<!doctype html><style>body{margin:0;background:blue}' +
        '#probe{width:200px;height:100px;background:red;' + (fixture.style || '') +
        ';clip-path:' + fixture.clip + '}</style><div id="probe"></div><script>' +
        (fixture.script || '') + '</script>';
}

module.exports = {fixtures, fixtureHtml};
