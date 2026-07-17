(0, eval)(require('fs').readFileSync('test/js/gsap.min.js', 'utf8'));

var box = document.getElementById('box');
var timelineOrder = [];

gsap.to(box, {
  opacity: 0.5,
  duration: 0.04,
  onComplete: function () {
    console.log('gsap:tween:complete');
    console.log('gsap:opacity:' + getComputedStyle(box).opacity);

    var state = { value: 0 };
    gsap.timeline({
      onComplete: function () {
        console.log('gsap:timeline:' + timelineOrder.join('>') + ':' + state.value);
        gsap.ticker.sleep();
      }
    })
      .to(state, {
        value: 1,
        duration: 0.02,
        onComplete: function () { timelineOrder.push('first'); }
      })
      .to(state, {
        value: 2,
        duration: 0.02,
        onComplete: function () { timelineOrder.push('second'); }
      });
  }
});
