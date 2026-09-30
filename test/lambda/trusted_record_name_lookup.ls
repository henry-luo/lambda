// D3.2.4v4: a trusted record read through any keeps name lookup semantics;
// a spread still applies its later writer after the declared record field.
type Pair = {left: int, right: int, third: int, fourth: int, fifth: int}
let pair: Pair = {left: 11, right: 22, third: 3, fourth: 4, fifth: 5}
let dynamic: any = pair
let spread: any = {right: 33, *:pair};
[dynamic.left, dynamic.right, dynamic.fifth, dynamic.absent,
 spread.left, spread.right, spread.fifth, spread.absent]
