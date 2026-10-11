// shared specified/computed/geometry probes for fixed and automatic repeat fixtures.
function gridRepeatFixture(amounts) {
    amounts.forEach(function(amount,index){
        var grid=document.createElement('div'),child=document.createElement('div'),copy=document.createElement('div');
        grid.id='grid-'+index;grid.className='grid';grid.appendChild(child);document.body.appendChild(grid);
        grid.style.gridTemplateRows='10px';grid.style.gridTemplateColumns='70px';grid.style.gridTemplateColumns=amount;
        copy.id='copy-'+index;copy.style.cssText=grid.style.cssText;copy.style.display='block';document.body.appendChild(copy);
    });
    document.getElementById('check').onclick=function(){
        amounts.forEach(function(amount,index){
            var grid=document.getElementById('grid-'+index),copy=document.getElementById('copy-'+index),style=getComputedStyle(grid),g=grid.getBoundingClientRect(),r=grid.firstChild.getBoundingClientRect();
            var values={specified:grid.style.gridTemplateColumns,used:style.gridTemplateColumns,template:style.gridTemplate,nongrid:getComputedStyle(copy).gridTemplateColumns,nongridTemplate:getComputedStyle(copy).gridTemplate,rect:[r.x-g.x,r.width].join(',')};
            Object.keys(values).forEach(function(field){document.getElementById('report').setAttribute('data-'+index+'-'+field,values[field]);});
        });
    };
}
