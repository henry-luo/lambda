const fixtures = [
    {name: 'leading nested separators', text: '<span><i>Built</i></span><span> <i>for</i></span><span> <i>Finance</i></span>'},
    {name: 'trailing nested separators', text: '<span><i>Built</i> </span><span><i>for</i> </span><span><i>Finance</i></span>'},
    {name: 'adjacent edge spaces collapse once', text: '<span><i>Built</i> </span><span> <i>for</i> </span><span> <i>Finance</i></span>'},
    {name: 'plain text control', text: 'Built for Finance'},
    {name: 'atomic edge whitespace stays local', text: '<i> Built </i> <i> for </i> <i> Finance </i>'},
    {name: 'comment-separated text retains isolated spaces', text: 'Built<!-- --> <!-- -->for<!-- --> <!-- -->Finance'},
    {name: 'text before comments retains trailing spaces', text: 'Built <!-- -->for <!-- -->Finance'},
];
function fixtureHtml(fixture, balanced) {
    return '<!doctype html><style>body{margin:0;background:white;font-family:Arial}' +
        'section{width:1000px;margin:auto;display:flex;flex-direction:column;align-items:center}' +
        'h1{margin:0;width:max-content;max-width:100%;font-size:120px;font-weight:400;' +
        'letter-spacing:-.07em;line-height:108px;text-wrap:' + (balanced?'balance':'wrap') + '}' +
        'i{font-style:normal;overflow-wrap:anywhere;max-width:100%;display:inline-block}' +
        'footer{height:8px;background:#00ff00}</style><section><h1>' + fixture.text + '</h1></section><footer></footer>';
}
module.exports = {fixtures, fixtureHtml};
