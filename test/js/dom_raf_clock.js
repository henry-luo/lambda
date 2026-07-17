// Headless frame draining advances every browser-visible animation clock.
var frameCount = 0;
var firstTimestamp = 0;
var firstPerformance = 0;
var firstDate = 0;

function onFrame(timestamp) {
    frameCount++;
    if (frameCount === 1) {
        firstTimestamp = timestamp;
        firstPerformance = performance.now();
        firstDate = Date.now();
    }
    if (frameCount < 4) {
        requestAnimationFrame(onFrame);
        return;
    }
    console.log('frames:' + frameCount);
    console.log('timestamp-advanced:' + (timestamp - firstTimestamp >= 49));
    console.log('performance-advanced:' + (performance.now() - firstPerformance >= 49));
    console.log('date-advanced:' + (Date.now() - firstDate >= 49));
}

requestAnimationFrame(onFrame);
