const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures}=require('./render_fixture.cjs');
const render=renderFixtures('balance-heading');

for(const direction of ['block','row','column','grid-parent','flex-parent'])for(const balanced of [false,true])for(const spacing of [0,2]){
    test(direction+' heading '+(balanced?'balances constrained text':'retains ordinary wrapping')+' with tracking '+spacing,async()=>{
        const nested=direction.endsWith('-parent');
        const parent=direction==='block'?'display:block':
            'display:flex;flex-direction:'+(nested?'column':direction)+';align-items:center;justify-content:center';
        const source='<!doctype html><style>body{margin:0;background:white;font-family:Arial}'+
            'section{width:1140px;margin:auto;'+parent+'}'+
            'h1{font-size:64px;line-height:96%;font-weight:500;text-align:center;'+
            'max-width:900px;margin:0;letter-spacing:'+spacing+'px;text-wrap:'+(balanced?'balance':'wrap')+'}'+
            'main{display:'+(direction==='grid-parent'?'grid':'flex')+';flex-direction:column}'+
            '</style>'+(nested?'<main>':'')+'<section><h1>Superpowers, everywhere you work</h1></section>'+(nested?'</main>':'');
        const {image}=await render(direction+'-'+balanced+'-'+spacing,source);
        const inkWidth=(top,bottom)=>{
            let left=image.width,right=-1;
            for(let y=top;y<bottom;y++)for(let x=0;x<image.width;x++){
                const offset=(y*image.width+x)*4;
                if(image.data[offset]<100&&image.data[offset+1]<100&&image.data[offset+2]<100){
                    left=Math.min(left,x);right=Math.max(right,x);
                }
            }
            return right-left+1;
        };
        const first=inkWidth(0,50),second=inkWidth(65,115);
        assert.ok(balanced?first<500&&second>550:first>700&&second<350,
            'line ink widths '+first+', '+second);
    });
}
