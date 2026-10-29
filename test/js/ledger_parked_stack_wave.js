function* parked(n) { {let kept = new Array(32).fill(n); yield n; return kept[0];} }
let waves=[];
for(let i=0;i<40;i++){let it=parked(i);it.next();waves.push(it);}
let sum=0;
for(let it of waves) sum+=it.next().value;
console.log(sum);
