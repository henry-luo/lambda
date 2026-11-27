var target = document.getElementById('target');
var effect = new KeyframeEffect(target, [
  { opacity: '0' },
  { opacity: '1' }
], { duration: 500, easing: 'linear' });
var animation = new Animation(effect);

console.log(typeof KeyframeEffect, effect instanceof KeyframeEffect);
console.log(typeof Animation, animation instanceof Animation);
// The constructor leaves currentTime unresolved until playback or a seek.
console.log(typeof animation.play, typeof animation.pause, animation.currentTime === null);
animation.play();
animation.currentTime = 125;
console.log(animation.currentTime);
console.log('WEB_ANIMATION_CONSTRUCTORS_DONE');
