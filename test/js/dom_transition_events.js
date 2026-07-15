var transition = new TransitionEvent('transitionend', {
  propertyName: 'opacity', elapsedTime: 0.25, pseudoElement: '::before'
});
console.log(transition.type);
console.log(transition.propertyName);
console.log(transition.elapsedTime);
console.log(transition.pseudoElement);
console.log(transition instanceof TransitionEvent);
console.log(transition instanceof Event);
var animation = new AnimationEvent('animationiteration', {
  animationName: 'pulse', elapsedTime: 0.5
});
console.log(animation.animationName + ':' + animation.elapsedTime);
console.log(animation instanceof AnimationEvent);
