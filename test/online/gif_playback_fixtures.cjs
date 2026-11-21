const gifPlaybackCases = [
    {name:'initial', plays:0, ms:0, color:[255, 0, 0]},
    {name:'skip-two-loops', plays:0, ms:650, color:[255, 0, 0]},
    {name:'skip-three-loops', plays:0, ms:950, color:[255, 0, 0]},
    {name:'partial-loop', plays:0, ms:1050, color:[0, 0, 255]},
    {name:'play-once', plays:1, ms:650, color:[0, 0, 255]},
    {name:'repeat-once', plays:2, ms:350, color:[255, 0, 0]},
    {name:'repeat-completed', plays:2, ms:650, color:[0, 0, 255]},
];

function gifPlaybackHtml(plays) {
    // one red pixel for 100ms, then blue for 200ms; the extension stores repeats after the initial play.
    const header = Buffer.from('47494638396101000100800000ff00000000ff', 'hex');
    const repeat = Buffer.from('21ff0b4e45545343415045322e300301000000', 'hex');
    repeat.writeUInt16LE(plays ? plays - 1 : 0, 16);
    function frame(delay, pixel) {
        const bytes = Buffer.from('21f90404000000002c0000000001000100000202440100', 'hex');
        bytes.writeUInt16LE(delay, 4);
        bytes[20] = pixel ? 0x4c : 0x44;
        return bytes;
    }
    const encoded = Buffer.concat([header, ...(plays === 1 ? [] : [repeat]), frame(10, 0), frame(20, 1), Buffer.from([0x3b])]);
    return '<!doctype html><style>body{margin:0}img{display:block;width:80px;height:80px;image-rendering:pixelated}</style>' +
        '<img src="data:image/gif;base64,' + encoded.toString('base64') + '">';
}

module.exports = {gifPlaybackCases, gifPlaybackHtml};
