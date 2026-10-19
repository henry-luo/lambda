// Keeps the film in step with the soundtrack. The Lambda sound control bumps data-epoch
// each time it starts the score at 0; this rewinds the native clip and the CSS captions.
const viewport = document.getElementById('film-view');
const stage = document.querySelector('.stage');
const host = new RadiantAnimationHost(viewport);
let epoch = null;

function follow() {
    const sound = document.getElementById('sound');
    const current = sound ? sound.getAttribute('data-epoch') : null;
    if (current !== null && current !== epoch) {
        epoch = current;
        host.operate(12, 1, 0);         // seek the scene clip to 0
        host.operate(12, 2, 'film');    // and keep it playing
        // swapping every animation-name restarts the captions, HUD and era marker.
        stage.classList.toggle('replay');
    }
    requestAnimationFrame(follow);
}
requestAnimationFrame(follow);
viewport.setAttribute('data-ready', 'true');
