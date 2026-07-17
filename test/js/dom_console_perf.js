console.log('console:', 'visible', 2);

var perfStart = performance.now();
setTimeout(function () {
  var perfAfterTimer = performance.now();
  console.log('timer-monotonic:', perfAfterTimer >= perfStart);
  console.log('timer-spaced:', perfAfterTimer - perfStart >= 5);
  console.log('time-origin:', performance.timeOrigin > 1000000000000);
  console.log('epoch-consistent:', Math.abs(Date.now() - (performance.timeOrigin + perfAfterTimer)) < 1000);

  requestAnimationFrame(function (timestamp) {
    var nowAtFrame = performance.now();
    console.log('raf-monotonic:', timestamp >= perfStart);
    console.log('raf-shared-origin:', Math.abs(timestamp - nowAtFrame) < 100);
    console.log('CONSOLE_PERF_DONE');
  });
}, 15);
