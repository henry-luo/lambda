// Source and intensity are Lambda view state; this adapter only controls native playback.
const play = document.getElementById('play');
let playing = true;
let stages = [], hosts = [];
function playbackHosts() {
    const current = Array.from(document.querySelectorAll('scene3d'));
    // a Lambda view rebuild retargets its timelines; adapters must follow the new DOM nodes.
    if (current.some((stage, index) => stage !== stages[index])) {
        stages = current;
        hosts = current.map(stage => new RadiantAnimationHost(stage));
    }
    return hosts;
}
function playback(enabled) {
    playbackHosts().forEach(host => host.operate(12, enabled ? 2 : 0, 'effect-cycle'));
    playing = enabled;
    play.textContent = enabled ? 'Pause' : 'Play';
    play.setAttribute('aria-pressed', String(enabled));
    document.getElementById('playback-status').textContent = enabled ? '● In motion' : '○ Paused';
}
play.addEventListener('click', () => playback(!playing));
document.getElementById('restart').addEventListener('click', () => {
    playbackHosts().forEach(host => host.operate(12, 1, 0));
    if (playing) playback(true);
});
