// S4.1.2, S4.2.5 and S7.10.5v3.
let signs = sign([nan, -3, inf])
let base = 9007199254740991
let powers = [base, -base] ** 3
let sample = [1, nan, 3]
let bad = [1, "bad"]
let large = 1e100m;
[round(decimal.nan) != round(decimal.nan), round(decimal.inf) == decimal.inf,
 round(large) == large, floor(decimal.inf) == decimal.inf,
 ceil(-decimal.inf) == -decimal.inf, trunc(decimal.inf) == decimal.inf];
[sign(nan) != sign(nan), signs[0] != signs[0], signs[1] == -1, signs[2] == 1];
[base ** 2 == inf, (-base) ** 3 == -inf, 3 ** 33 == 5559060566555523,
 powers[0] == inf, powers[1] == -inf, math.pow(base, 2) == inf,
 math.pow(2, -1) == 0.5];
[math.norm(sample) != math.norm(sample), math.median(sample) != math.median(sample),
 math.variance(sample) != math.variance(sample), math.deviation(sample) != math.deviation(sample),
 math.quantile(sample, 0.5) != math.quantile(sample, 0.5)];
[math.sqrt(bad) is error, sign(bad) is error, clip(bad, 0, 3) is error,
 math.norm("bad") is error, math.median("bad") is error, math.variance("bad") is error,
 math.median([nan, "bad"]) is error, math.median([]) == null,
 math.median([2, null, 4], true) == 3]
