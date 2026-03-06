// For-of loop tests

// for-of over array of numbers
var sum = 0;
var nums = [10, 20, 30];
for (var n of nums) {
  sum = sum + n;
}

// for-of over array of strings
var words = "";
var items = ["hello", "world", "test"];
for (var w of items) {
  words = words + w + ",";
}

// for-of with single element
var single = 0;
for (var s of [42]) {
  single = single + s;
}

// for-of with empty array
var empty = 0;
for (var e of []) {
  empty = empty + 1;
}

const result = {
  sum: sum,
  words: words,
  single: single,
  empty: empty
};
result;
