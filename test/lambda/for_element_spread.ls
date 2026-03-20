// Test: for-loop and spreadable arrays inside element content
// Verifies that for-expressions produce individual children, not nested arrays

let items = [<b; "one">, <b; "two">, <b; "three">]

"=== for-loop in element content ==="
let el = <div; for child in items { child }>
len(el)
type(el[0])
name(el[0])
name(el[1])
name(el[2])

"=== for-loop with transform ==="
let el2 = <ul; for x in ["a", "b", "c"] { <li; x> }>
len(el2)
name(el2[0])
el2[0][0]
el2[1][0]
el2[2][0]

"=== for-loop with filter ==="
let el3 = <div; for x in ["1", "2", "3", "4", "5"] { if (int(x) % 2 == 0) { <span; x> } }>
len(el3)
el3[0][0]
el3[1][0]

"=== nested for-loops ==="
let el5 = <div; for row in [["a", "b"], ["c", "d"]] {
    <tr; for col in row { <td; col> }>
}>
len(el5)
len(el5[0])
len(el5[1])
el5[0][0][0]
el5[1][1][0]

"=== empty for-loop ==="
let el6 = <div; for x in [] { <p; x> }>
len(el6)

"=== format preserves structure ==="
format(<div; for x in ["a", "b"] { <b; x> }>, 'xml')
