const path=require('node:path');
const {pathToFileURL}=require('node:url');
const {rgb}=require('./render_fixture.cjs');
const video=pathToFileURL(path.resolve(__dirname,'../media/test_video.mp4')).href;
const poster='data:image/svg+xml;base64,'+Buffer.from('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="180"><rect width="320" height="180" fill="blue"/></svg>').toString('base64');
const videoFirstFrameCases=[
    {name:'paused'}, {name:'autoplay',autoplay:true},
    {name:'paused with poster',poster:true},
    {name:'autoplay with poster',autoplay:true,poster:true},
    {name:'scripted play with poster',scripted:true,poster:true},
    {name:'unavailable video with poster',autoplay:true,poster:true,unavailable:true},
];

function videoFirstFrameHtml(fixture){
    return `<!doctype html><style>body{margin:0;background:white}video{display:block;width:320px;height:180px}</style>
        <video id="movie" src="${fixture.unavailable?video+'.missing':video}" preload="auto"
            ${fixture.autoplay?'autoplay':''} ${fixture.autoplay||fixture.scripted?'muted':''}
            ${fixture.poster?'poster="'+poster+'"':''}></video>
        ${fixture.scripted?'<script>window.addEventListener("load",function(){document.getElementById("movie").play().catch(function(error){console.log("VIDEO_PLAY_REJECTED",error.name,error.message)})})</script>':''}`;
}

function checkVideoFirstFrame(assert,fixture,image){
    if(fixture.poster&&(!fixture.autoplay&&!fixture.scripted||fixture.unavailable)){
        assert.deepEqual(rgb(image,20,20),[0,0,255]);
        return;
    }
    // the repository's first frame has a green landscape; a placeholder is grayscale.
    let green=0;
    for(let y=0;y<180;y++)for(let x=0;x<320;x++){
        const [r,g,b]=rgb(image,x,y);
        if(g>r+15&&g>b+15)green++;
    }
    assert.ok(green>320*180*.05,`expected landscape pixels, found ${green}`);
}

module.exports={videoFirstFrameCases,videoFirstFrameHtml,checkVideoFirstFrame};
