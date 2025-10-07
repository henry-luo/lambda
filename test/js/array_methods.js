// Array Methods Test

// Test data
var numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
var words = ["hello", "world", "javascript", "lambda"];

// Map - transform each element
var squared = numbers.map(function(x) {
    return x * x;
});

var lengths = words.map(function(word) {
    return word.length;
});

// Filter - select elements that match a condition
var evens = numbers.filter(function(x) {
    return x % 2 === 0;
});

var longWords = words.filter(function(word) {
    return word.length > 5;
});

// Reduce - accumulate values
var sum = numbers.reduce(function(acc, x) {
    return acc + x;
}, 0);

var product = numbers.reduce(function(acc, x) {
    return acc * x;
}, 1);

var concatenated = words.reduce(function(acc, word) {
    return acc + " " + word;
}, "");

// Find maximum using reduce
var max = numbers.reduce(function(acc, x) {
    return x > acc ? x : acc;
}, numbers[0]);

// Count occurrences using reduce
var letterCounts = words.reduce(function(acc, word) {
    for (var i = 0; i < word.length; i++) {
        var letter = word[i];
        if (acc[letter]) {
            acc[letter] = acc[letter] + 1;
        } else {
            acc[letter] = 1;
        }
    }
    return acc;
}, {});

// ForEach - side effects
var sideEffectResult = [];
numbers.forEach(function(x) {
    if (x % 3 === 0) {
        sideEffectResult.push(x);
    }
});

// Chaining array methods
var processedNumbers = numbers
    .filter(function(x) { return x > 3; })
    .map(function(x) { return x * 2; })
    .reduce(function(acc, x) { return acc + x; }, 0);

// Complex data processing
var students = [
    { name: "Alice", grade: 85 },
    { name: "Bob", grade: 92 },
    { name: "Charlie", grade: 78 },
    { name: "Diana", grade: 96 }
];

var passingStudents = students.filter(function(student) {
    return student.grade >= 80;
});

var averageGrade = students.reduce(function(acc, student) {
    return acc + student.grade;
}, 0) / students.length;

var topStudent = students.reduce(function(acc, student) {
    return student.grade > acc.grade ? student : acc;
}, students[0]);

// Return comprehensive test results
{
    squared: squared,
    lengths: lengths,
    evens: evens,
    longWords: longWords,
    sum: sum,
    product: product,
    concatenated: concatenated,
    max: max,
    letterCounts: letterCounts,
    sideEffectResult: sideEffectResult,
    processedNumbers: processedNumbers,
    passingStudents: passingStudents,
    averageGrade: averageGrade,
    topStudent: topStudent
};
