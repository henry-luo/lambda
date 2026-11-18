const fixtures = [
    {name: 'feature_query', style: 'background:red', extra:
        '@supports(color:color-mix(in lab,red,red)){#box{background:lime}}', expected: [0, 255, 0]},
    {name: 'lab_mix_same_color', style: 'background:color-mix(in lab,red,red)', expected: [255, 0, 0]},
    {name: 'lab_mix_gray', style: 'background:color-mix(in lab,white,black)', expected: [119, 119, 119]},
    {name: 'lab_gray', style: 'background:lab(50% 0 0)', expected: [119, 119, 119]},
    {name: 'lab_mix_alpha', style: 'background:color-mix(in lab,red,transparent)', expected: [128, 0, 0]},
];

function fixtureHtml(fixture) {
    return `<!doctype html>
<style>body{margin:0;background:black}#box{width:200px;height:100px;${fixture.style}}
${fixture.extra || ''}</style><div id=box></div>`;
}

module.exports = {fixtures, fixtureHtml};
