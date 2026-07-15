var box = document.getElementById('box');
var count = 0;
new ResizeObserver(function (entries) {
  count++;
  console.log('resize:' + count + ':' + Math.round(entries[0].contentRect.width) + 'x' + Math.round(entries[0].contentRect.height));
}).observe(box);
box.style.width = '140px';
box.getBoundingClientRect();
console.log('sync:' + count);
