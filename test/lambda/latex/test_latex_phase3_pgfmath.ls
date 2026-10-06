import pgfmath: lambda.doc.tikz.pgfmath
import expression: lambda.doc.tikz.expression

let tree = pgfmath.expression_tree("divide(pow(2,3),sqrt(4))")^
let result = expression.evaluate(tree, 0.0)^
"binary PGF function:"; result == 4.0

let nested = pgfmath.expression_tree("pow(divide(3,2),2)")^
"nested PGF function:"; expression.evaluate(nested, 0.0)^ == 2.25
