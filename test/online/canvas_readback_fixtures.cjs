const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[
 {name:'color comparison reads the pixels actually painted',body:`
    const colors=['#ff0000','#00ff00','#0000ff'];
    colors.forEach((color,x)=>{ctx.fillStyle=color;ctx.fillRect(x,0,1,1)});
    return {samples:[0,1,2,3].map(x=>pixel(x,0)),equal:pixel(0,0).join(',')===pixel(1,0).join(',')};`,
    expected:{samples:[[255,0,0,255],[0,255,0,255],[0,0,255,255],[0,0,0,0]],equal:false}},
 {name:'negative dimensions normalize the source rectangle without flipping pixels',body:`
    ['red','lime','blue','white'].forEach((color,i)=>{ctx.fillStyle=color;ctx.fillRect(i%2,Math.floor(i/2),1,1)});
    const image=ctx.getImageData(2,2,-2,-2);
    return {width:image.width,height:image.height,data:Array.from(image.data)};`,
    expected:{width:2,height:2,data:[255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255]}},
 {name:'readback outside the bitmap includes transparent padding',body:`
    ctx.fillStyle='red';ctx.fillRect(0,0,1,1);ctx.fillStyle='lime';ctx.fillRect(1,0,1,1);
    return {data:Array.from(ctx.getImageData(-1,0,3,1).data),outside:Array.from(ctx.getImageData(10,10,1,1).data)};`,
    expected:{data:[0,0,0,0,255,0,0,255,0,255,0,255],outside:[0,0,0,0]}},
 {name:'readback ignores the current drawing transform clip and alpha',body:`
    ctx.fillStyle='blue';ctx.fillRect(0,0,1,1);ctx.translate(100,100);
    ctx.beginPath();ctx.rect(0,0,1,1);ctx.clip();ctx.globalAlpha=0;
    return {data:pixel(0,0)};`,expected:{data:[0,0,255,255]}},
 {name:'readback converts premultiplied storage to straight alpha',body:`
    ctx.fillStyle='rgba(0,255,0,0.5)';ctx.fillRect(0,0,1,1);
    const data=pixel(0,0);return {straight:data[0]===0&&data[1]===255&&data[2]===0,alpha:data[3]>=127&&data[3]<=128};`,
    expected:{straight:true,alpha:true}},
 {name:'returned pixel arrays are independent and resizing clears the real bitmap',body:`
    ctx.fillStyle='red';ctx.fillRect(0,0,1,1);
    const a=ctx.getImageData(0,0,1,1),b=ctx.getImageData(0,0,1,1);a.data[0]=5;
    const independent=b.data[0]===255&&pixel(0,0)[0]===255;canvas.width=4;
    return {independent,cleared:pixel(0,0).every(value=>value===0),old:a.data[0]===5&&b.data[0]===255};`,
    expected:{independent:true,cleared:true,old:true}},
 {name:'ImageData has branded readonly accessors and a shared readback method',body:`
    const image=ctx.getImageData(0,0,1,1),descriptor=Object.getOwnPropertyDescriptor(ImageData.prototype,'data');
    const getter=descriptor.get;
    return {instance:image instanceof ImageData,prototype:Object.getPrototypeOf(image)===ImageData.prototype,
        array:image.data instanceof Uint8ClampedArray,tag:Object.prototype.toString.call(image),
        colorSpace:image.colorSpace,pixelFormat:image.pixelFormat,readonly:descriptor.set===undefined,
        sameObject:getter.call(image)===image.data,brand:error(()=>getter.call({})),
        fake:error(()=>getter.call(Object.create(ImageData.prototype))),
        method:ctx.getImageData===CanvasRenderingContext2D.prototype.getImageData,
        methodLength:ctx.getImageData.length,constructorLength:ImageData.length};`,
    expected:{instance:true,prototype:true,array:true,tag:'[object ImageData]',colorSpace:'srgb',pixelFormat:'rgba-unorm8',
        readonly:true,sameObject:true,brand:'TypeError',fake:'TypeError',method:true,methodLength:4,constructorLength:2}},
 {name:'ImageData constructors allocate zeros or retain an existing clamped array',body:`
    const empty=new ImageData(2,1),data=new Uint8ClampedArray([10,20,30,255,40,50,60,255]);
    const image=new ImageData(data,2),explicit=new ImageData(data,1,2);data[0]=99;
    return {empty:empty.width===2&&empty.height===1&&empty.data.length===8&&empty.data.every(x=>x===0),
        shared:image.data===data&&image.data[0]===99,size:[image.width,image.height,explicit.width,explicit.height],
        coercion:new ImageData('2',true).data.length};`,
    expected:{empty:true,shared:true,size:[2,1,1,2],coercion:8}},
 {name:'invalid dimensions storage and enums reject with the correct error types',body:`
    return {zero:error(()=>ctx.getImageData(0,0,0,1)),nan:error(()=>ctx.getImageData(NaN,0,1,1)),
        infinity:error(()=>ctx.getImageData(0,Infinity,1,1)),missing:error(()=>ctx.getImageData(0,0,1)),
        ctorZero:error(()=>new ImageData(0,1)),call:error(()=>ImageData(1,1)),
        storage:error(()=>new ImageData(new Uint8ClampedArray(3),1)),
        row:error(()=>new ImageData(new Uint8ClampedArray(8),3)),
        height:error(()=>new ImageData(new Uint8ClampedArray(8),2,2)),
        settings:error(()=>ctx.getImageData(0,0,1,1,{colorSpace:'invalid'})),
        fraction:ctx.getImageData(-0.8,0,1.9,1).width};`,
    expected:{zero:'IndexSizeError',nan:'TypeError',infinity:'TypeError',missing:'TypeError',ctorZero:'IndexSizeError',
        call:'TypeError',storage:'InvalidStateError',row:'IndexSizeError',height:'IndexSizeError',settings:'TypeError',fraction:1}},
 {name:'subclassing and captured prototypes preserve ownership across collection',body:`
    class Pixels extends ImageData {}const image=new Pixels(1,1),bytes=image.data;bytes[2]=77;
    const Original=ImageData;window.ImageData=function(){throw new Error('replacement must not run')};
    const captured=ctx.getImageData(0,0,1,1);if(typeof gc==='function')gc();
    return {subclass:image instanceof Pixels&&image instanceof Original,kept:image.data===bytes&&image.data[2]===77,
        captured:Object.getPrototypeOf(captured)===Original.prototype&&captured.width===1};`,
    expected:{subclass:true,kept:true,captured:true}},
 {name:'constructor dimensions use unsigned WebIDL conversion and root supplied storage during coercion',body:`
    const data=new Uint8ClampedArray([1,2,3,255]);let calls=0;
    const image=new ImageData(data,{valueOf(){calls++;if(typeof gc==='function')gc();return 4294967297}});
    return {nan:error(()=>new ImageData(NaN,1)),infinity:error(()=>new ImageData(1,Infinity)),
        wrapped:new ImageData(4294967297,4294967297).data.length,
        negative:new ImageData(-4294967295,1).width,calls,retained:image.data===data&&image.width===1&&image.data[2]===3};`,
    expected:{nan:'IndexSizeError',infinity:'IndexSizeError',wrapped:4,negative:1,calls:1,retained:true}},
 {name:'createImageData uses absolute dimensions and clones only the shape',body:`
    const original=ctx.createImageData(-2,3,{colorSpace:'srgb'});original.data[0]=120;
    const copy=ctx.createImageData(original);
    return {size:[original.width,original.height,copy.width,copy.height],fresh:copy.data!==original.data,
        blank:copy.data.every(channel=>channel===0),type:copy instanceof ImageData,
        length:ctx.createImageData.length,shared:ctx.createImageData===CanvasRenderingContext2D.prototype.createImageData};`,
    expected:{size:[2,3,2,3],fresh:true,blank:true,type:true,length:1,shared:true}},
 {name:'putImageData copies straight-alpha bytes and replaces pixels regardless of draw state',body:`
    ctx.fillStyle='red';ctx.fillRect(0,0,4,4);ctx.translate(40,40);
    ctx.beginPath();ctx.rect(0,0,1,1);ctx.clip();ctx.globalAlpha=0;
    const storage=new Uint8ClampedArray([9,9,9,9, 0,255,0,128, 0,0,0,0, 9,9,9,9]);
    const image=new ImageData(storage.subarray(4,12),2);
    ctx.putImageData(image,1,2);image.data.fill(90);
    return {pixels:[pixel(0,2),pixel(1,2),pixel(2,2),pixel(3,2)],length:ctx.putImageData.length};`,
    expected:{pixels:[[255,0,0,255],[0,255,0,128],[0,0,0,0],[255,0,0,255]],length:3}},
 {name:'putImageData normalizes the dirty source rectangle without moving its origin',body:`
    const image=ctx.createImageData(3,3);
    for(let i=0;i<9;i++){image.data[4*i]=20+i;image.data[4*i+3]=255}
    ctx.putImageData(image,1,1,3,3,-2,-2);
    return {values:[[1,1],[2,2],[3,2],[2,3],[3,3]].map(([x,y])=>pixel(x,y)[0])};`,
    expected:{values:[0,24,25,27,28]}},
 {name:'putImageData clips both source and destination with the original row stride',body:`
    const image=ctx.createImageData(3,3);
    for(let i=0;i<9;i++){image.data[4*i]=20+i;image.data[4*i+3]=255}
    ctx.putImageData(image,-1,-1,-1,-1,5,5);
    ctx.putImageData(image,2147483647,2147483647);
    return {values:[[0,0],[1,0],[0,1],[1,1],[2,0]].map(([x,y])=>pixel(x,y)[0])};`,
    expected:{values:[24,25,27,28,0]}},
 {name:'ImageData pixel methods validate their overloads and reject detached storage at the write',body:`
    const image=ctx.createImageData(2,1);
    const check={zero:error(()=>ctx.createImageData(0,2)),missing:error(()=>ctx.createImageData(3)),
        brand:error(()=>ctx.createImageData.call({},1,1)),finite:error(()=>ctx.putImageData(image,Infinity,0)),
        invalidImage:error(()=>ctx.putImageData({},0,0)),
        partial:error(()=>ctx.putImageData(image,0,0,1)),
        detached:error(()=>ctx.putImageData(image,{valueOf(){image.data.buffer.transfer();return 0}},0))};
    return check;`,
    expected:{zero:'IndexSizeError',missing:'TypeError',brand:'TypeError',finite:'TypeError',invalidImage:'TypeError',
        partial:'TypeError',detached:'InvalidStateError'}},
];
function fixtureHtml(fixture){return scriptResultFixture(`
    const canvas=document.createElement('canvas');canvas.width=4;canvas.height=4;document.body.appendChild(canvas);
    const ctx=canvas.getContext('2d');
    function pixel(x,y){return Array.from(ctx.getImageData(x,y,1,1).data)}
    function error(callback){try{callback();return 'none'}catch(exception){return exception.name}}
    const result=(function(){${fixture.body}})();window.__canvasReadbackResult=result;
    console.log('CANVAS_READBACK_DETAILS',JSON.stringify(result));
    check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
    'CANVAS_READBACK_ERROR','CANVAS_READBACK_RESULT');}
module.exports={fixtures,fixtureHtml};
