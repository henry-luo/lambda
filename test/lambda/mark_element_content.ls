// S2.6.1v2/S2.6.2: parsed element content keeps bools, symbols, bare words,
// numbers and nested elements exactly as a literal does; only null and ""
// drop. The reader once lost a leading bool or symbol child to its attribute
// lookahead, and split `trueish` into `true` + 'ish'. In Lambda source a bare
// word in content is an identifier, so the literal spells those symbols quoted.
let doc = input('./test/input/element_content.mark', 'mark')^;
let lit = <root
  <flag true>
  <flag false>
  <sym 'quoted symbol'>
  <word 'bare_word'>
  <num 1.5>
  <neg -3>
  <poison nan; inf; -inf>
  <nothing null>
  <empty "">
  <mixed true; 'sym'; -2.5; <inner false>; "text"; null; 42>
  <attrs v:true, w:'sym', n:nan, false>
  <leading true; "text">
  <trailing "text"; true>
  <words 'trueish'; 'nullable'; 'infinite'; 'nanometer'; 'falsey'>
  <quoted 'quoted name':1, 'sym'>
>;
// nan is unequal to itself (S4.2.1), so the shape spells it as a string
fn child_shapes(e) => [for (c in content(e)) [name(c), [for (x in content(c)) [type(x), if (x is nan) "nan" else x]]]];
[
  child_shapes(doc) == child_shapes(lit), len(content(doc)) == len(content(lit)),
  doc[0][0] == true, doc[1][0] == false, doc[2][0] == 'quoted symbol', doc[3][0] == 'bare_word',
  doc[6][1] == inf, doc[6][2] == -inf, doc[6][1] is float, doc[6][0] is nan,
  len(content(doc[7])) == 0, len(content(doc[8])) == 0,
  doc[10].v == true, doc[10].w == 'sym', doc[10].n is nan, doc[10][0] == false,
  doc[14]['quoted name'] == 1,
  format(doc, 'mark') == format(lit, 'mark')
]
child_shapes(doc)
