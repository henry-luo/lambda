const fixtures = [
    {name: 'named area constrains the absolute child before content layout', placement: 'grid-area:photo', box: [122, 12, 200, 200]},
    {name: 'numeric area also constrains descendant percentages', placement: 'grid-column:2/3;grid-row:1/3', box: [122, 12, 200, 200]},
    {name: 'percentage insets use the resolved area', placement: 'grid-area:photo;inset:10%', box: [142, 32, 160, 160]},
    {name: 'explicit sizes retain the normal positioned constraint equation', placement: 'grid-area:photo;width:80px;height:50px', box: [122, 12, 80, 50]},
    {name: 'maximum constraints remain effective inside the area', placement: 'grid-area:photo;max-width:150px;max-height:100px', box: [122, 12, 150, 100]},
    {name: 'area sizing retains child padding and borders', placement: 'grid-area:photo;padding:5px;border:2px solid black', box: [122, 12, 200, 200], decoration: 7},
    {name: 'calculated and percentage preferred sizes use the area', placement: 'grid-area:photo;width:calc(100% - 20px);height:50%', box: [122, 12, 180, 100]},
    {name: 'a static grid does not replace the outer containing block', placement: 'grid-column:2/3;grid-row:1/3', grid: 'position:static', box: [0, 0, 600, 400]},
    {name: 'a grid does not replace the viewport fixed containing block', placement: 'position:fixed;grid-column:2/3;grid-row:1/3', box: [0, 0, 1200, 800]}
];

function fixtureHtml(fixture) {
    return '<!doctype html><style>body{margin:0}#outer{position:relative;width:600px;height:400px}' +
        '#grid{display:grid;position:relative;width:300px;height:200px;padding:10px 20px;border:2px solid black;' +
        'grid-template-columns:100px 200px;grid-template-rows:80px 120px;' +
        'grid-template-areas:"copy photo" "copy photo";' + (fixture.grid || '') + '}' +
        '#subject{position:absolute;inset:0;background:red;' + fixture.placement + '}' +
        '#inner{width:100%;height:100%;background:lime}</style>' +
        '<div id="outer"><div id="grid"><div id="subject"><div id="inner"></div></div></div></div>' +
        '<script>var subject=document.getElementById("subject").getBoundingClientRect();' +
        'var inner=document.getElementById("inner").getBoundingClientRect();' +
        'console.log("GRID_ABSOLUTE_GEOMETRY",JSON.stringify([subject.x,subject.y,subject.width,subject.height,' +
        'inner.x,inner.y,inner.width,inner.height]));</script>';
}

module.exports = {fixtures, fixtureHtml};
