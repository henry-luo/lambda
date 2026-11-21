const lateResourceCases = ['parsed', 'scripted', 'detached', 'picture', 'failed', 'chain', 'plain', 'blocked-sibling', 'zero-window'];

function lateResourceHtml(mode, origin) {
    const image = origin + '/image.svg?case=' + mode;
    const attributes = mode === 'picture' ? 'src="' + origin + '/unused.svg"' : 'src="' + image + '"';
    const constructed = mode === 'scripted' || mode === 'detached';
    const markup = constructed ? '' : (mode === 'picture'
        ? '<picture><source srcset="' + image + '"><img id="photo" ' + attributes + '></picture>'
        : '<img id="photo" ' + attributes + '>');
    const blocked = mode === 'blocked-sibling'
        ? '<img src="' + origin + '/image.svg?case=held" style="left:300px">' : '';
    const script = mode === 'plain' ? '' : `<script>
        const mode=${JSON.stringify(mode)};
        const photo=${constructed ? 'new Image' : 'document.getElementById("photo")'};
        let loads=0,errors=0,decoded=0,reported=false;
        let observedTime=performance.now();
        setTimeout(function(){
            observedTime=performance.now();
            requestAnimationFrame(function(){observedTime=performance.now();
                requestAnimationFrame(function(){observedTime=performance.now()})
            });
        },1000);
        function done() {
            if(reported || loads!==(mode==='failed'?0:mode==='chain'?2:1) ||
                errors!==(mode==='failed'?1:0) || decoded!==(mode==='blocked-sibling'?0:1)) return;
            reported=true;
            document.getElementById('status').style.background='green';
            console.log('LATE_RESOURCE_RESULT',JSON.stringify({loads,errors,decoded,
                complete:photo.complete,width:photo.naturalWidth,
                frameHeld:performance.now()===observedTime}));
        }
        function decode() {
            photo.decode().then(function(){decoded=1;done()},function(error){
                if(mode==='failed' && error.name==='EncodingError') {decoded=1;done()}
            });
        }
        photo.onload=function(){
            loads++;
            if(mode==='chain' && loads===1) {
                photo.src=${JSON.stringify(image + '&second=1')};decode();
            } else done();
        };
        photo.onerror=function(){errors++;done()};
        ${constructed ? 'photo.id="photo";photo.src=' + JSON.stringify(image) + ';' +
            (mode === 'scripted' ? 'document.body.appendChild(photo);' : '') : ''}
        if(mode!=='chain' && mode!=='blocked-sibling') decode();
        </script>`;
    return '<!doctype html><style>body{margin:0;background:white}' +
        '#status{width:80px;height:80px;background:' + (mode === 'plain' ? 'green' : 'red') + '}' +
        'img{position:absolute;left:100px;top:0;width:80px;height:80px}</style>' +
        '<div id="status"></div>' + markup + blocked + script;
}

function serveLateResource(request, response) {
    const url = new URL(request.url, 'http://fixture.invalid');
    response.setHeader('Cache-Control', 'no-store');
    setTimeout(() => {
        if (url.searchParams.get('case') === 'failed') response.writeHead(404).end();
        else response.writeHead(200, {'Content-Type':'image/svg+xml'}).end(
            '<svg xmlns="http://www.w3.org/2000/svg" width="80" height="80"><rect width="80" height="80" fill="blue"/></svg>');
    }, url.searchParams.get('case') === 'held' ? 2000 : url.searchParams.has('second') ? 40 : 650);
}

module.exports = {lateResourceCases, lateResourceHtml, serveLateResource};
