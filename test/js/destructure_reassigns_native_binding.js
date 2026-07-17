function scaled(source) {
  let x = 1;
  let y = 1;
  true && (({ scaleX: x, scaleY: y } = source), 0);
  return [x, y];
}

console.log('destructure-native:', scaled({ scaleX: 1.5, scaleY: 2.25 }).join(' '));
