const fixtures = [
    {name: 'media and after parenthesis', type: 'media', condition: '(min-width:768px)and (max-width:1279px)', matches: true},
    {name: 'media or after parenthesis', type: 'media', condition: '(max-width:767px)or (min-width:768px)', matches: true},
    {name: 'nested compact conditions', type: 'media', condition: '((min-width:768px)and (color))or (orientation:portrait)', matches: true},
    {name: 'supports and after parenthesis', type: 'supports', condition: '(display:grid)and (color:red)', matches: true},
    {name: 'nested supports conditions', type: 'supports', condition: '((display:grid)and (color:red))or (display:bogus)', matches: true},
    {name: 'and function remains invalid', type: 'media', condition: '(min-width:768px)and(max-width:1279px)', matches: false},
];
function fixtureHtml(fixture) {
    return `<!doctype html><style>body{margin:0}.target{width:80px;height:80px;background:red}
@${fixture.type}${fixture.condition}{.target{background:lime}}</style><div class="target"></div>`;
}
module.exports = {fixtures, fixtureHtml};
