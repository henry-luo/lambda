const cookieExpiryCases=[
    {name:'epoch',attributes:'Expires=Thu, 01 Jan 1970 00:00:00 GMT',remaining:false},
    {name:'before-epoch',attributes:'Expires=Wed, 31 Dec 1969 23:59:59 GMT',remaining:false},
    {name:'session',attributes:'',remaining:true},
    {name:'future',attributes:'Expires=Fri, 01 Jan 2100 00:00:00 GMT',remaining:true},
    {name:'max-age-keeps',attributes:'Max-Age=3600; Expires=Thu, 01 Jan 1970 00:00:00 GMT',remaining:true},
    {name:'max-age-deletes',attributes:'Max-Age=0; Expires=Fri, 01 Jan 2100 00:00:00 GMT',remaining:false},
];
function cookieExpiryHtml(fixture){
    const name='expiry_'+fixture.name.replaceAll('-','_');
    return `<!doctype html><style>body{margin:0}#state{width:80px;height:80px;background:red}</style><div id=state></div><script>
    document.cookie='session_control=kept;Path=/';
    document.cookie='${name}=-3;Expires=Fri, 01 Jan 2100 00:00:00 GMT;Path=/';
    var supported=document.cookie.indexOf('${name}=-3')>=0;
    document.cookie=${JSON.stringify(name+'=-3;Path=/;'+fixture.attributes)};
    window.probeResult={supported:supported,remaining:document.cookie.indexOf('${name}=-3')>=0,session:document.cookie.indexOf('session_control=kept')>=0};
    document.getElementById('state').style.background=window.probeResult.remaining===${fixture.remaining}?'lime':'red';
    console.log('COOKIE_EXPIRY_RESULT',JSON.stringify(window.probeResult));
    </script>`;
}
module.exports={cookieExpiryCases,cookieExpiryHtml};
