const {scriptResultFixture} = require('./script_result_fixture.cjs');

const fixtures = [
    {
        name: 'gradient and context methods enforce their WebIDL brands',
        body: `
            const gradient=ctx.createRadialGradient(0,0,0,10,10,10);
            const add=CanvasGradient.prototype.addColorStop;
            return {
                tag:Object.prototype.toString.call(gradient),
                constructor:error(()=>new CanvasGradient()),
                shared:ctx.createRadialGradient===CanvasRenderingContext2D.prototype.createRadialGradient,
                lengths:[ctx.createRadialGradient.length,add.length],
                context:error(()=>ctx.createRadialGradient.call({},0,0,0,10,10,10)),
                missing:error(()=>ctx.createRadialGradient(0,0,0,1,1)),
                negative:error(()=>ctx.createRadialGradient(0,0,-1,10,10,10)),
                nonfinite:error(()=>ctx.createRadialGradient(0,Infinity,0,10,10,10)),
                forged:error(()=>add.call(Object.create(CanvasGradient.prototype),0,'red')),
                outside:error(()=>add.call(gradient,1.1,'red')),
                badColor:error(()=>add.call(gradient,.4,'not-a-color')),
                nonfiniteStop:error(()=>add.call(gradient,NaN,'red'))
            };`,
        expected: {tag: '[object CanvasGradient]', constructor: 'TypeError', shared: true, lengths: [6, 2],
            context: 'TypeError', missing: 'TypeError', negative: 'IndexSizeError', nonfinite: 'TypeError',
            forged: 'TypeError', outside: 'IndexSizeError', badColor: 'SyntaxError', nonfiniteStop: 'TypeError'},
    },
    {
        name: 'radial stops sort by offset and retain equal-offset order',
        body: `
            const g=ctx.createRadialGradient(4.5,4.5,2,4.5,4.5,10);
            for(const [offset,color] of [[1,'blue'],[.5,'red'],[0,'red'],[.5,'lime']])g.addColorStop(offset,color);
            ctx.fillStyle=g;ctx.fillRect(0,0,28,12);
            return {same:ctx.fillStyle===g,inner:near(pixel(4,4),[255,0,0,255]),
                before:near(pixel(9,4),[255,0,0,255]),after:near(pixel(11,4),[0,191,64,255]),
                outer:near(pixel(18,4),[0,0,255,255])};`,
        expected: {same: true, inner: true, before: true, after: true, outer: true},
    },
    {
        name: 'off-center equal-radius circles only paint the region swept by the circles',
        body: `
            const g=ctx.createRadialGradient(4.5,5.5,2,12.5,5.5,2);
            g.addColorStop(0,'red');g.addColorStop(1,'blue');
            ctx.fillStyle=g;ctx.fillRect(0,0,28,12);
            return {outside:near(pixel(7,9),[0,0,0,0]),midpoint:near(pixel(6,5),[128,0,128,255]),
                end:near(pixel(20,5),[0,0,255,255])};`,
        expected: {outside: true, midpoint: true, end: true},
    },
    {
        name: 'gradient color and alpha interpolate before source-over compositing',
        body: `
            const g=ctx.createRadialGradient(4.5,4.5,2,4.5,4.5,10);
            g.addColorStop(0,'rgba(255,0,0,0.2)');g.addColorStop(1,'rgba(0,0,255,0.8)');
            ctx.fillStyle='black';ctx.fillRect(0,0,28,12);
            ctx.fillStyle=g;ctx.fillRect(0,0,28,12);
            return {midpoint:near(pixel(10,4),[64,0,64,255]),before:near(pixel(4,4),[51,0,0,255]),
                after:near(pixel(18,4),[0,0,204,255])};`,
        expected: {midpoint: true, before: true, after: true},
    },
    {
        name: 'saved gradients retain identity, follow mutations and can be used on a different canvas',
        body: `
            const g=ctx.createRadialGradient(4.5,4.5,2,4.5,4.5,10);
            ctx.fillStyle=g;ctx.fillRect(0,0,28,12);
            const initiallyEmpty=pixel(4,4)[3]===0;
            g.addColorStop(0,'red');
            ctx.save();ctx.fillStyle='lime';ctx.save();ctx.fillStyle='black';
            g.addColorStop(1,'blue');if(typeof gc==='function')gc();
            ctx.restore();ctx.fillRect(0,0,28,12);const nested=near(pixel(4,4),[0,255,0,255]);
            ctx.restore();ctx.restore();ctx.fillRect(0,0,28,12);
            const retained=ctx.fillStyle===g&&near(pixel(4,4),[255,0,0,255])&&near(pixel(18,4),[0,0,255,255]);
            const other=document.createElement('canvas').getContext('2d');
            other.fillStyle=g;other.fillRect(0,0,28,12);
            const otherCanvas=other.fillStyle===g&&near(Array.from(other.getImageData(4,4,1,1).data),[255,0,0,255]);
            ctx.save();canvas.width=canvas.width;ctx.restore();
            return {initiallyEmpty,nested,retained,otherCanvas,reset:ctx.fillStyle==='#000000'};`,
        expected: {initiallyEmpty: true, nested: true, retained: true, otherCanvas: true, reset: true},
    },
    {
        name: 'gradient fill follows the drawing transform, path coverage, clip and alpha',
        body: `
            ctx.translate(60,0);
            const g=ctx.createRadialGradient(4.5,4.5,2,4.5,4.5,6);
            g.addColorStop(0,'red');g.addColorStop(1,'blue');
            ctx.setTransform(2,0,0,1,0,0);ctx.fillStyle=g;ctx.globalAlpha=.5;
            ctx.beginPath();ctx.rect(0,0,12,10);ctx.clip();
            ctx.beginPath();ctx.rect(1,1,20,8);ctx.fill();
            return {inner:near(pixel(8,4),[255,0,0,128]),outer:near(pixel(22,4),[0,0,255,128]),
                clipped:pixel(24,4)[3]===0,outsidePath:pixel(8,0)[3]===0};`,
        expected: {inner: true, outer: true, clipped: true, outsidePath: true},
    },
    {
        name: 'identical circles are transparent and radial styles paint strokes and text',
        body: `
            const empty=ctx.createRadialGradient(5,5,2,5,5,2);empty.addColorStop(0,'red');
            ctx.fillStyle=empty;ctx.fillRect(0,0,28,12);
            const degenerate=pixel(2,2)[3]===0;
            const red=ctx.createRadialGradient(0,0,0,0,0,8);red.addColorStop(0,'red');
            ctx.strokeStyle=red;ctx.lineWidth=2;ctx.strokeRect(2,2,8,6);
            const rectangle=near(pixel(2,4),[255,0,0,255])&&pixel(6,5)[3]===0;
            ctx.beginPath();ctx.moveTo(14,2);ctx.lineTo(20,2);ctx.stroke();
            const path=near(pixel(17,2),[255,0,0,255]);
            ctx.fillStyle=red;ctx.font='10px sans-serif';ctx.fillText('H',20,11);
            const bytes=ctx.getImageData(20,3,8,9).data;
            const text=Array.from(bytes).some((value,i)=>i%4===0&&value>200&&bytes[i+3]>0);
            return {degenerate,rectangle,path,text};`,
        expected: {degenerate: true, rectangle: true, path: true, text: true},
    },
];

function fixtureHtml(fixture) {
    return scriptResultFixture(`
        const canvas=document.createElement('canvas');
        canvas.width=28;canvas.height=12;document.body.appendChild(canvas);
        const ctx=canvas.getContext('2d');
        function pixel(x,y){return Array.from(ctx.getImageData(x,y,1,1).data)}
        function near(actual,expected){return actual.every((value,i)=>Math.abs(value-expected[i])<=2)}
        function error(callback){try{callback();return 'none'}catch(exception){return exception.name}}
        const result=(function(){${fixture.body}})();
        console.log('CANVAS_RADIAL_DETAILS',JSON.stringify(result));
        check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
        'CANVAS_RADIAL_ERROR', 'CANVAS_RADIAL_RESULT');
}

module.exports = {fixtures, fixtureHtml};
