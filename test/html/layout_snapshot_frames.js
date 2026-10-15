// SN1: readiness is explicit; ordinary recurring animation keeps its bounded sample.
var snapshot_box = document.getElementById('result');
var snapshot_steps = 0;
var snapshot_limit = Number(document.documentElement.getAttribute('data-frames'));
snapshot_box.setAttribute('data-cancelled', 'safe');
if (document.documentElement.getAttribute('data-cancel') === 'yes') {
    var cancelled_frame = requestAnimationFrame(function () {
        snapshot_box.setAttribute('data-cancelled', 'ran');
    });
    cancelAnimationFrame(cancelled_frame);
}
function snapshot_step() {
    snapshot_steps++;
    snapshot_box.style.width = (10 + snapshot_steps) + 'px';
    if (snapshot_limit === 0 || snapshot_steps < snapshot_limit) {
        requestAnimationFrame(snapshot_step);
    } else {
        document.documentElement.classList.remove('reftest-wait');
    }
}
addEventListener('load', function () { requestAnimationFrame(snapshot_step); });
