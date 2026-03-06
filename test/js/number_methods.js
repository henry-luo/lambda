// Number methods tests

// toFixed on float
var n1 = 3.14159;
var tf1 = n1.toFixed(2);

// toFixed on integer
var n2 = 42;
var tf2 = n2.toFixed(3);

// toFixed with 0 digits
var n3 = 2.7;
var tf3 = n3.toFixed(0);

// toString on number
var n4 = 123;
var ts1 = n4.toString();

const result = {
  tf1: tf1,
  tf2: tf2,
  tf3: tf3,
  ts1: ts1
};
result;
