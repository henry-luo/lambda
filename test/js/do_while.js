// Do-while loop tests

// Basic do-while: count to 5
var count = 0;
do {
  count = count + 1;
} while (count < 5);

// Do-while runs at least once even with false condition
var ranOnce = 0;
do {
  ranOnce = 1;
} while (false);

// Do-while with accumulator
var sum = 0;
var i = 1;
do {
  sum = sum + i;
  i = i + 1;
} while (i <= 10);

// Do-while with break
var breakAt = 0;
do {
  breakAt = breakAt + 1;
  if (breakAt === 3) {
    break;
  }
} while (breakAt < 100);

const result = {
  count: count,
  ranOnce: ranOnce,
  sum: sum,
  breakAt: breakAt
};
result;
