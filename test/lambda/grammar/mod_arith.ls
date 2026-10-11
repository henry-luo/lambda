// helper module for types.ls: a public grammar used by a public function
pub grammar arith {
    sum   = prec.left(1, sum, "+", sum) | num
    num   := "\d"+
    space := " "*
}
pub fn sum_tree(text: string) => parse(text, arith)^
