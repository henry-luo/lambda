// v11 B8: Labeled statements - break/continue with labels

// Test 1: labeled break from nested for loops
var result1 = 0;
outer: for (var i = 0; i < 5; i++) {
    for (var j = 0; j < 5; j++) {
        if (j === 2) break outer;
        result1 = result1 + 1;
    }
}
console.log(result1);

// Test 2: labeled continue on outer loop
var result2 = 0;
loop1: for (var i = 0; i < 3; i++) {
    for (var j = 0; j < 3; j++) {
        if (j === 1) continue loop1;
        result2 = result2 + 1;
    }
}
console.log(result2);

// Test 3: labeled break from while inside for
var result3 = 0;
search: for (var i = 0; i < 3; i++) {
    var k = 0;
    while (k < 3) {
        if (i === 1 && k === 1) break search;
        result3 = result3 + 1;
        k++;
    }
}
console.log(result3);

// Test 4: labeled break on a block (non-loop)
var result4 = "start";
block1: {
    result4 = "inside";
    if (true) break block1;
    result4 = "unreachable";
}
console.log(result4);

// Test 5: labeled break from switch inside labeled loop
var result5 = [];
outer2: for (var i = 0; i < 3; i++) {
    switch (i) {
        case 1:
            result5.push("break-outer");
            break outer2;
        default:
            result5.push(i);
            break;
    }
}
console.log(JSON.stringify(result5));

var result = {
    test1_labeled_break: result1,
    test2_labeled_continue: result2,
    test3_while_in_for: result3,
    test4_block_label: result4,
    test5_switch_in_loop: result5
};
