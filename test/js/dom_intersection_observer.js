var count = 0;
console.log('entry-interface:' + typeof IntersectionObserverEntry + ':' +
  typeof IntersectionObserverEntry.prototype);
new IntersectionObserver(function (entries) {
  count++;
  console.log('intersection:' + count + ':' + entries[0].isIntersecting + ':' +
    (entries[0].intersectionRatio > 0) + ':' +
    (entries[0] instanceof IntersectionObserverEntry));
}, { threshold: [0, 0.5, 1] }).observe(document.getElementById('box'));
document.getElementById('box').getBoundingClientRect();
console.log('sync:' + count);
