(0, eval)(require('fs').readFileSync('test/js/nouislider.min.js', 'utf8'));

var sliderElement = document.getElementById('slider');
var sliderUpdates = 0;
noUiSlider.create(sliderElement, {
  start: 25,
  orientation: matchMedia('(min-width: 1px)').matches ? 'horizontal' : 'vertical',
  range: { min: 0, max: 100 }
});
sliderElement.noUiSlider.on('update', function () { sliderUpdates++; });
sliderElement.noUiSlider.set(75);

console.log('nouislider:value:', sliderElement.noUiSlider.get());
console.log('nouislider:update:', sliderUpdates > 0);
console.log('nouislider:aria:', sliderElement.querySelector('[role="slider"]').getAttribute('aria-valuenow'));
