// PTH67v2: every committed version stays readable for the evaluation. Forty
// autocommits (PTH63v2) on one document retain forty versions, which spill
// across several retention chunks (4, 8, 16, 32 slots).

let d = temp('retain', {n: 0, rows: [{id: 0}]});
let r0 = temp.'retain'#rows;
put temp.'retain'#n = 1;
put temp.'retain'#n = 2;
put temp.'retain'#n = 3;
put temp.'retain'#n = 4;
put temp.'retain'#n = 5;
put temp.'retain'#n = 6;
put temp.'retain'#n = 7;
put temp.'retain'#n = 8;
put temp.'retain'#n = 9;
put temp.'retain'#n = 10;
put temp.'retain'#n = 11;
put temp.'retain'#n = 12;
put temp.'retain'#n = 13;
put temp.'retain'#n = 14;
put temp.'retain'#n = 15;
put temp.'retain'#n = 16;
put temp.'retain'#n = 17;
put temp.'retain'#n = 18;
put temp.'retain'#n = 19;
put temp.'retain'#n = 20;
put temp.'retain'#n = 21;
put temp.'retain'#n = 22;
put temp.'retain'#n = 23;
put temp.'retain'#n = 24;
put temp.'retain'#n = 25;
put temp.'retain'#n = 26;
put temp.'retain'#n = 27;
put temp.'retain'#n = 28;
put temp.'retain'#n = 29;
put temp.'retain'#n = 30;
put temp.'retain'#n = 31;
put temp.'retain'#n = 32;
put temp.'retain'#n = 33;
put temp.'retain'#n = 34;
put temp.'retain'#n = 35;
put temp.'retain'#n = 36;
put temp.'retain'#n = 37;
put temp.'retain'#n = 38;
put temp.'retain'#n = 39;
put temp.'retain'#n = 40;
temp.'retain'#n;
// the first version is still the one `d` holds
d;
// an untouched subtree keeps its node identity across every commit (PTH43v2)
r0;
(r0 === temp.'retain'#rows);
&r0
