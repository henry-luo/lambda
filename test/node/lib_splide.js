(0, eval)(require('fs').readFileSync('test/js/splide.min.js', 'utf8'));

var moved = [];
var carousel = new Splide('#splide', {
  arrows: false,
  pagination: false,
  speed: 20,
  drag: true
});
carousel.on('moved', function (index, previous) {
  moved.push(previous + '>' + index);
});
carousel.mount();
console.log('splide:initial: ' + carousel.index + ' ' + document.querySelector('.splide__slide.is-active').textContent);
carousel.go('>');
setTimeout(function () {
  document.querySelector('.splide__list').dispatchEvent(new Event('transitionend'));
}, 30);
setTimeout(function () {
  console.log('splide:moved: ' + carousel.index + ' ' + moved.join(','));
  console.log('splide:active: ' + document.querySelector('.splide__slide.is-active').textContent);
}, 80);
