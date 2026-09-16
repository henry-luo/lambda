function scaleAndSum(data, limit) {
  let scale = 2;
  let total = 0;
  for (let i = 0; i < limit; i++) {
    data[i] = data[i] + 0.5;
    total += scale * data[i];
  }
  return total;
}

let typed = new Float64Array(2);
typed[0] = 1;
typed[1] = 2;
console.log('typed:' + scaleAndSum(typed, 2));

let indirect = scaleAndSum;
console.log('ordinary:' + indirect([1], 1));

