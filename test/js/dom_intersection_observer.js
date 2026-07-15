var count = 0;
new IntersectionObserver(function (entries) {
  count++;
  console.log('intersection:' + count + ':' + entries[0].isIntersecting + ':' + (entries[0].intersectionRatio > 0));
}, { threshold: [0, 0.5, 1] }).observe(document.getElementById('box'));
document.getElementById('box').getBoundingClientRect();
console.log('sync:' + count);
