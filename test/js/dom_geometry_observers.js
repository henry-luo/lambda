var box = document.getElementById('box');
var resizeCount = 0;
var intersectionCount = 0;
var resizeObserver = new ResizeObserver(function (entries) {
  resizeCount++;
  console.log('RO:' + Math.round(entries[0].contentRect.width) + 'x' + Math.round(entries[0].contentRect.height));
});
var intersectionObserver = new IntersectionObserver(function (entries) {
  intersectionCount++;
  console.log('IO:' + entries[0].isIntersecting + ':' + (entries[0].intersectionRatio > 0));
});
resizeObserver.observe(box);
intersectionObserver.observe(box);
box.style.width = '140px';
box.getBoundingClientRect();
console.log('OBS_SYNC:' + resizeCount + ':' + intersectionCount);
