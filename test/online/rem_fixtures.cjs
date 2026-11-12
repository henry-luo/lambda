const fixtures = [
    {name: 'root clamp publishes its computed rem basis', style: '', font: 14},
    {name: 'root rem font does not become the descendant initial basis', style: 'font-size:.75rem', font: 12},
    {name: 'larger root rem font enlarges descendant lengths', style: 'font-size:2rem', font: 32},
    {name: 'root variable font publishes its rem basis', style: '--size:.875rem;font-size:var(--size)', font: 14},
    {name: 'root mixed math publishes its rem basis', style: 'font-size:calc(.5rem + 6px)', font: 14},
    {name: 'root shorthand publishes its rem basis', style: 'font:.875rem/1.5 Arial', font: 14},
    {name: 'literal root font keeps its existing basis', style: 'font-size:14px', font: 14},
    {name: 'root rem basis remains unzoomed', style: 'font-size:.875rem;zoom:2', font: 14, zoom: 2}
];

function fixtureHtml(fixture) {
    return '<!doctype html><html style="' + fixture.style + '"><style>' +
        'html{font-size:clamp(.875rem,.25rem + .8333vw,1rem);padding:1rem;box-sizing:border-box}' +
        'body{margin:0}#probe{width:10rem;height:2rem;background:red}' +
        '#math{width:calc(2rem + 10px);height:1rem;background:blue}' +
        '#label{font:4rem/1 Arial;height:1em;width:1rem;background:green}' +
        '</style><body><div id="probe"></div><div id="math"></div><div id="label"></div><script>' +
        'console.log("REM_GEOMETRY",document.getElementById("probe").getBoundingClientRect().width,' +
        'document.getElementById("math").getBoundingClientRect().width,' +
        'document.getElementById("label").getBoundingClientRect().height);</script></body></html>';
}

module.exports = {fixtures, fixtureHtml};
