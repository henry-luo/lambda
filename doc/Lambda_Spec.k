// Lambda Script Language Specification in K Framework
// Author: Henry Luo
// This specification defines the executable semantics of Lambda Script,
// a pure functional scripting language for data processing and document presentation.

requires "domains.md"

module LAMBDA-SYNTAX

  // Base types
  syntax BaseType ::= "null" | "bool" | "int" | "int64" | "float" | "decimal"
                    | "number" | "string" | "symbol" | "binary" | "datetime"
                    | "any" | "error"

  // Type expressions
  syntax Type ::= BaseType
                | Id                                    [klabel(TypeVar)]
                | "(" Type "," Types ")"               [klabel(ListType)]
                | "[" Type "]"                         [klabel(ArrayType)]
                | "{" TypeBindings "}"                 [klabel(MapType)]
                | "<" Id AttrTypes ";" Type ">"       [klabel(ElementType)]
                | "(" Types ")" "->" Type              [klabel(FunctionType)]
                | Type "?"                             [klabel(OptionalType)]
                | Type "+"                             [klabel(OneOrMoreType)]
                | Type "*"                             [klabel(ZeroOrMoreType)]
                | Type "|" Type                        [klabel(UnionType), left]
                | Type "&" Type                        [klabel(IntersectType), left]
                | Type "!" Type                        [klabel(ExcludeType), left]

  syntax Types ::= List{Type, ","}
  syntax TypeBinding ::= Id ":" Type
  syntax TypeBindings ::= List{TypeBinding, ","}
  syntax AttrType ::= Id ":" Type
  syntax AttrTypes ::= List{AttrType, ","}

  // Literals
  syntax Literal ::= "null" | "true" | "false" | "inf" | "nan"
                   | Int | Float | Decimal | String | Symbol | Binary | DateTime

  syntax Decimal ::= r"[0-9]+\.[0-9]*[nN]" [token]
  syntax Symbol ::= r"'[a-zA-Z_][a-zA-Z0-9_-]*" [token]
  syntax Binary ::= r"b'\\x[0-9a-fA-F\\s]+'" | r"b'\\64[A-Za-z0-9+/\\s]+={0,2}'" [token]
  syntax DateTime ::= r"t'[0-9]{4}(-[0-9]{2})?(-[0-9]{2})?([ T][0-9]{2}:[0-9]{2}(:[0-9]{2})?(\.[0-9]{3})?(Z|[+-][0-9]{2}:[0-9]{2})?)?'" [token]

  // Unary operators
  syntax UnaryOp ::= "+" | "-" | "not"

  // Binary operators  
  syntax BinaryOp ::= "+" | "-" | "*" | "/" | "_/" | "%" | "^"     [klabel(ArithOp)]
                    | "==" | "!=" | "<" | "<=" | ">" | ">="        [klabel(CompOp)]
                    | "and" | "or"                                 [klabel(LogicOp)]
                    | "|" | "&" | "!"                             [klabel(SetOp)]
                    | "to" | "is" | "in"                          [klabel(RelOp)]

  // Expressions
  syntax Expr ::= Literal
                | Id                                              [klabel(Variable)]
                | "(" Expr "," Exprs ")"                         [klabel(List)]
                | "[" Exprs "]"                                  [klabel(Array)]
                | "{" MapItems "}"                               [klabel(Map)]
                | "<" Id AttrItems ";" Exprs ">"                [klabel(Element)]
                | UnaryOp Expr                                   [klabel(UnaryExpr)]
                | Expr BinaryOp Expr                             [klabel(BinaryExpr), left]
                | Expr "[" Expr "]"                              [klabel(IndexExpr)]
                | Expr "." Id                                    [klabel(MemberExpr)]
                | Expr "(" Exprs ")"                             [klabel(CallExpr)]
                | "let" Id "=" Expr "," Expr                     [klabel(LetExpr)]
                | "if" "(" Expr ")" Expr "else" Expr             [klabel(IfExpr)]
                | "for" "(" Id "in" Expr ")" Expr                [klabel(ForExpr)]
                | "fn" "(" Params ")" ":" Type "=>" Expr         [klabel(FunctionExpr)]
                | "fn" "(" Params ")" ":" Type "{" Stmts Expr "}" [klabel(FunctionBlock)]
                | "(" Expr ")"                                   [bracket]
                | BaseType                                       [klabel(TypeExpr)]

  syntax Exprs ::= List{Expr, ","}
  syntax MapItem ::= Id ":" Expr | String ":" Expr | Symbol ":" Expr
  syntax MapItems ::= List{MapItem, ","}
  syntax AttrItem ::= Id ":" Expr | String ":" Expr | Symbol ":" Expr | "&" Expr
  syntax AttrItems ::= List{AttrItem, ","}
  syntax Param ::= Id | Id ":" Type
  syntax Params ::= List{Param, ","}

  // Statements
  syntax Stmt ::= "let" Id "=" Expr                              [klabel(LetStmt)]
                | "let" Id ":" Type "=" Expr                     [klabel(LetTypedStmt)]
                | "pub" Id "=" Expr                              [klabel(PubStmt)]
                | "pub" Id ":" Type "=" Expr                     [klabel(PubTypedStmt)]
                | "fn" Id "(" Params ")" ":" Type "{" Stmts Expr "}" [klabel(FunctionStmt)]
                | "fn" Id "(" Params ")" ":" Type "=>" Expr      [klabel(FunctionExprStmt)]
                | "if" Expr "{" Stmts Expr "}" "else" "{" Stmts Expr "}" [klabel(IfStmt)]
                | "for" Id "in" Expr "{" Stmts Expr "}"          [klabel(ForStmt)]
                | "type" Id "=" Type                             [klabel(TypeDefStmt)]
                | "type" Id "<" AttrTypes ";" Type ">"           [klabel(EntityTypeStmt)]
                | "type" Id "{" AttrTypes "}"                    [klabel(ObjectTypeStmt)]
                | "import" Module                                [klabel(ImportStmt)]
                | Expr ";"                                       [klabel(ExprStmt)]

  syntax Stmts ::= List{Stmt, ""}

  syntax Module ::= Id | Id ":" Module | Symbol

  syntax Content ::= Stmts | Expr | Stmts Expr

  syntax Program ::= Content | ImportStmts Content
  syntax ImportStmt ::= "import" Module
  syntax ImportStmts ::= List{ImportStmt, ""}

  // Operator precedence
  syntax priorities ArithOp > CompOp > LogicOp > SetOp > RelOp

endmodule

module LAMBDA-CONFIGURATION

  imports LAMBDA-SYNTAX
  imports DOMAINS

  // Runtime values
  syntax Val ::= Literal
               | ListVal(Vals)
               | ArrayVal(Vals) 
               | MapVal(Map)
               | ElementVal(Id, Map, Vals)
               | ClosureVal(Params, Expr, Env)
               | RangeVal(Int, Int)
               | ErrorVal(String)
               | TypeVal(Type)

  syntax Vals ::= List{Val, ","}

  // Environment for variable bindings
  syntax Env ::= ".Env" | Env "[" Id "<-" Val "]"

  // Heap for mutable references (arrays, maps)
  syntax HeapRef ::= ref(Int)
  syntax Heap ::= ".Heap" | Heap "[" Int "<-" Val "]"

  // Type environment
  syntax TEnv ::= ".TEnv" | TEnv "[" Id "<-" Type "]"

  // Configuration
  configuration <lambda>
    <k> $PGM:Program </k>
    <env> .Env </env>
    <heap> .Heap </heap>
    <tenv> .TEnv </tenv>
    <nextRef> 0 </nextRef>
    <output stream="stdout"> .List </output>
  </lambda>

endmodule

module LAMBDA-SEMANTICS

  imports LAMBDA-CONFIGURATION

  /***************************************************************************/
  /* PROGRAM EXECUTION                                                      */
  /***************************************************************************/

  // Program with imports
  rule <k> ImportStmts:ImportStmts Content:Content => Content </k>
       // Import handling would be implemented here

  // Content evaluation
  rule <k> Stmts:Stmts Expr:Expr => Stmts ~> Expr </k>
  rule <k> Expr:Expr => Expr </k> when notBool isStmts(Expr)

  /***************************************************************************/
  /* LITERAL EVALUATION                                                     */
  /***************************************************************************/

  rule <k> null => null </k>
  rule <k> true => true </k>
  rule <k> false => false </k>
  rule <k> I:Int => I </k>
  rule <k> F:Float => F </k>
  rule <k> S:String => S </k>
  rule <k> Sym:Symbol => Sym </k>
  rule <k> inf => inf </k>
  rule <k> nan => nan </k>

  /***************************************************************************/
  /* VARIABLE LOOKUP                                                        */
  /***************************************************************************/

  rule <k> X:Id => V </k>
       <env> Env:Env[X <- V:Val] </env>

  /***************************************************************************/
  /* LET EXPRESSIONS                                                        */
  /***************************************************************************/

  rule <k> let X:Id = E1:Expr, E2:Expr => E1 ~> freezerLet(X, E2) </k>

  syntax KItem ::= freezerLet(Id, Expr)
  rule <k> V:Val ~> freezerLet(X:Id, E:Expr) => E </k>
       <env> Env:Env => Env[X <- V] </env>

  /***************************************************************************/
  /* STATEMENTS                                                             */
  /***************************************************************************/

  // Let statement
  rule <k> let X:Id = E:Expr => E ~> freezerLetStmt(X) ... </k>

  syntax KItem ::= freezerLetStmt(Id)
  rule <k> V:Val ~> freezerLetStmt(X:Id) => .K </k>
       <env> Env:Env => Env[X <- V] </env>

  // Public statement (same as let but could mark as exported)
  rule <k> pub X:Id = E:Expr => let X = E </k>

  /***************************************************************************/
  /* ARITHMETIC OPERATIONS                                                  */
  /***************************************************************************/

  // Integer arithmetic
  rule <k> I1:Int + I2:Int => I1 +Int I2 </k>
  rule <k> I1:Int - I2:Int => I1 -Int I2 </k>
  rule <k> I1:Int * I2:Int => I1 *Int I2 </k>
  rule <k> I1:Int / I2:Int => I1 /Int I2 </k> requires I2 =/=Int 0
  rule <k> I1:Int _/ I2:Int => I1 divInt I2 </k> requires I2 =/=Int 0
  rule <k> I1:Int % I2:Int => I1 modInt I2 </k> requires I2 =/=Int 0
  rule <k> I1:Int ^ I2:Int => I1 ^Int I2 </k>

  // Float arithmetic  
  rule <k> F1:Float + F2:Float => F1 +Float F2 </k>
  rule <k> F1:Float - F2:Float => F1 -Float F2 </k>
  rule <k> F1:Float * F2:Float => F1 *Float F2 </k>
  rule <k> F1:Float / F2:Float => F1 /Float F2 </k> requires F2 =/=Float 0.0
  rule <k> F1:Float ^ F2:Float => F1 ^Float F2 </k>

  // Mixed arithmetic (promote int to float)
  rule <k> I:Int + F:Float => Int2Float(I) +Float F </k>
  rule <k> F:Float + I:Int => F +Float Int2Float(I) </k>
  rule <k> I:Int - F:Float => Int2Float(I) -Float F </k>
  rule <k> F:Float - I:Int => F -Float Int2Float(I) </k>
  rule <k> I:Int * F:Float => Int2Float(I) *Float F </k>
  rule <k> F:Float * I:Int => F *Float Int2Float(I) </k>
  rule <k> I:Int / F:Float => Int2Float(I) /Float F </k> requires F =/=Float 0.0
  rule <k> F:Float / I:Int => F /Float Int2Float(I) </k> requires I =/=Int 0
  rule <k> I:Int ^ F:Float => Int2Float(I) ^Float F </k>
  rule <k> F:Float ^ I:Int => F ^Float Int2Float(I) </k>

  // String concatenation
  rule <k> S1:String + S2:String => S1 +String S2 </k>

  // Unary operations
  rule <k> + I:Int => I </k>
  rule <k> - I:Int => 0 -Int I </k>
  rule <k> + F:Float => F </k>
  rule <k> - F:Float => 0.0 -Float F </k>

  /***************************************************************************/
  /* COMPARISON OPERATIONS                                                  */
  /***************************************************************************/

  rule <k> I1:Int == I2:Int => I1 ==Int I2 </k>
  rule <k> I1:Int != I2:Int => I1 =/=Int I2 </k>
  rule <k> I1:Int < I2:Int => I1 <Int I2 </k>
  rule <k> I1:Int <= I2:Int => I1 <=Int I2 </k>
  rule <k> I1:Int > I2:Int => I1 >Int I2 </k>
  rule <k> I1:Int >= I2:Int => I1 >=Int I2 </k>

  rule <k> F1:Float == F2:Float => F1 ==Float F2 </k>
  rule <k> F1:Float != F2:Float => F1 =/=Float F2 </k>
  rule <k> F1:Float < F2:Float => F1 <Float F2 </k>
  rule <k> F1:Float <= F2:Float => F1 <=Float F2 </k>
  rule <k> F1:Float > F2:Float => F1 >Float F2 </k>
  rule <k> F1:Float >= F2:Float => F1 >=Float F2 </k>

  rule <k> S1:String == S2:String => S1 ==String S2 </k>
  rule <k> S1:String != S2:String => S1 =/=String S2 </k>

  rule <k> true == true => true </k>
  rule <k> false == false => true </k>
  rule <k> true == false => false </k>
  rule <k> false == true => false </k>

  rule <k> null == null => true </k>

  /***************************************************************************/
  /* LOGICAL OPERATIONS                                                     */
  /***************************************************************************/

  rule <k> true and B:Bool => B </k>
  rule <k> false and _:Bool => false </k>
  rule <k> true or _:Bool => true </k>
  rule <k> false or B:Bool => B </k>
  rule <k> not true => false </k>
  rule <k> not false => true </k>

  /***************************************************************************/
  /* CONDITIONAL EXPRESSIONS                                               */
  /***************************************************************************/

  rule <k> if (true) E1:Expr else _:Expr => E1 </k>
  rule <k> if (false) _:Expr else E2:Expr => E2 </k>

  /***************************************************************************/
  /* COLLECTION OPERATIONS                                                 */
  /***************************************************************************/

  // List construction
  rule <k> (E:Expr, Es:Exprs) => E ~> freezerList(.Vals, Es) </k>
  rule <k> (E:Expr) => ListVal(E) </k>

  syntax KItem ::= freezerList(Vals, Exprs)
  rule <k> V:Val ~> freezerList(Vs:Vals, E:Expr, Es:Exprs) 
           => E ~> freezerList(Vs ListItem(V), Es) </k>
  rule <k> V:Val ~> freezerList(Vs:Vals, .Exprs) 
           => ListVal(Vs ListItem(V)) </k>

  // Array construction (similar to list but mutable)
  rule <k> [Es:Exprs] => Es ~> freezerArray(.Vals) </k>

  syntax KItem ::= freezerArray(Vals)
  rule <k> Vs:Vals ~> freezerArray(AccVs:Vals) => ArrayVal(AccVs Vs) </k>

  // Map construction
  rule <k> {MIs:MapItems} => MIs ~> freezerMap(.Map) </k>

  syntax KItem ::= freezerMap(Map)
  rule <k> (X:Id : E:Expr, MIs:MapItems) ~> freezerMap(M:Map) 
           => E ~> freezerMapItem(X, MIs, M) </k>
  rule <k> (X:Id : E:Expr) ~> freezerMap(M:Map) 
           => E ~> freezerMapItem(X, .MapItems, M) </k>
  rule <k> .MapItems ~> freezerMap(M:Map) => MapVal(M) </k>

  syntax KItem ::= freezerMapItem(Id, MapItems, Map)
  rule <k> V:Val ~> freezerMapItem(X:Id, MIs:MapItems, M:Map) 
           => MIs ~> freezerMap(M[X <- V]) </k>

  /***************************************************************************/
  /* INDEXING AND MEMBER ACCESS                                            */
  /***************************************************************************/

  // List indexing
  rule <k> ListVal(Vs:Vals)[I:Int] => Vs[I] </k> 
       requires I >=Int 0 andBool I <Int size(Vs)

  // Array indexing  
  rule <k> ArrayVal(Vs:Vals)[I:Int] => Vs[I] </k>
       requires I >=Int 0 andBool I <Int size(Vs)

  // Map member access
  rule <k> MapVal(M:Map).X:Id => M[X] </k> requires X in_keys(M)
  rule <k> MapVal(M:Map)[S:String] => M[String2Id(S)] </k> 
       requires String2Id(S) in_keys(M)

  /***************************************************************************/
  /* RANGE OPERATIONS                                                      */
  /***************************************************************************/

  rule <k> I1:Int to I2:Int => RangeVal(I1, I2) </k>

  /***************************************************************************/
  /* FUNCTION OPERATIONS                                                   */
  /***************************************************************************/

  // Function expression
  rule <k> fn (Ps:Params) : _:Type => E:Expr => ClosureVal(Ps, E, Env) </k>
       <env> Env:Env </env>

  // Function call
  rule <k> ClosureVal(Ps:Params, E:Expr, ClosureEnv:Env)(Args:Exprs) 
           => Args ~> freezerCall(Ps, E, ClosureEnv) </k>

  syntax KItem ::= freezerCall(Params, Expr, Env)
  rule <k> Vs:Vals ~> freezerCall(Ps:Params, E:Expr, ClosureEnv:Env) 
           => E </k>
       <env> _ => bindParams(Ps, Vs, ClosureEnv) </env>

  syntax Env ::= bindParams(Params, Vals, Env) [function]
  rule bindParams(.Params, .Vals, Env:Env) => Env
  rule bindParams((X:Id, Ps:Params), (V:Val, Vs:Vals), Env:Env) 
       => bindParams(Ps, Vs, Env[X <- V])

  /***************************************************************************/
  /* FOR LOOPS AND COMPREHENSIONS                                          */
  /***************************************************************************/

  rule <k> for (X:Id in RangeVal(Start:Int, End:Int)) E:Expr 
           => forLoop(X, Start, End, E, .Vals) </k>

  syntax KItem ::= forLoop(Id, Int, Int, Expr, Vals)
  rule <k> forLoop(X:Id, I:Int, End:Int, E:Expr, Acc:Vals) 
           => E ~> freezerForLoop(X, I +Int 1, End, E, Acc) </k>
       <env> Env:Env => Env[X <- I] </env>
       requires I <=Int End

  rule <k> forLoop(_:Id, I:Int, End:Int, _:Expr, Acc:Vals) 
           => ListVal(Acc) </k>
       requires I >Int End

  syntax KItem ::= freezerForLoop(Id, Int, Int, Expr, Vals)
  rule <k> V:Val ~> freezerForLoop(X:Id, NextI:Int, End:Int, E:Expr, Acc:Vals) 
           => forLoop(X, NextI, End, E, Acc ListItem(V)) </k>
       <env> _ => _[X <- NextI -Int 1] </env>

  /***************************************************************************/
  /* TYPE OPERATIONS                                                       */
  /***************************************************************************/

  // Type expressions evaluate to type values
  rule <k> int => TypeVal(int) </k>
  rule <k> float => TypeVal(float) </k>
  rule <k> string => TypeVal(string) </k>
  rule <k> bool => TypeVal(bool) </k>

  /***************************************************************************/
  /* BUILT-IN FUNCTIONS                                                    */
  /***************************************************************************/

  // Length function
  rule <k> len(ListVal(Vs:Vals)) => size(Vs) </k>
  rule <k> len(ArrayVal(Vs:Vals)) => size(Vs) </k>
  rule <k> len(S:String) => lengthString(S) </k>
  rule <k> len(RangeVal(Start:Int, End:Int)) => End -Int Start +Int 1 </k>
       requires End >=Int Start

  // Type function
  rule <k> type(I:Int) => TypeVal(int) </k>
  rule <k> type(F:Float) => TypeVal(float) </k>
  rule <k> type(S:String) => TypeVal(string) </k>
  rule <k> type(true) => TypeVal(bool) </k>
  rule <k> type(false) => TypeVal(bool) </k>
  rule <k> type(null) => TypeVal(null) </k>
  rule <k> type(ListVal(_)) => TypeVal(list) </k>
  rule <k> type(ArrayVal(_)) => TypeVal(array) </k>
  rule <k> type(MapVal(_)) => TypeVal(map) </k>

  // Print function (outputs to stream)
  rule <k> print(V:Val) => null </k>
       <output> ... (.List => ListItem(val2string(V))) </output>

  syntax String ::= val2string(Val) [function]
  rule val2string(I:Int) => Int2String(I)
  rule val2string(F:Float) => Float2String(F)
  rule val2string(S:String) => S
  rule val2string(true) => "true"
  rule val2string(false) => "false"
  rule val2string(null) => "null"

  /***************************************************************************/
  /* ERROR HANDLING                                                        */
  /***************************************************************************/

  // Division by zero
  rule <k> _:Int / 0 => ErrorVal("Division by zero") </k>
  rule <k> _:Float / 0.0 => ErrorVal("Division by zero") </k>

  // Array/List out of bounds
  rule <k> ListVal(Vs:Vals)[I:Int] => ErrorVal("Index out of bounds") </k>
       requires I <Int 0 orBool I >=Int size(Vs)
  rule <k> ArrayVal(Vs:Vals)[I:Int] => ErrorVal("Index out of bounds") </k>
       requires I <Int 0 orBool I >=Int size(Vs)

  // Map key not found
  rule <k> MapVal(M:Map).X:Id => ErrorVal("Key not found") </k> 
       requires notBool (X in_keys(M))

  /***************************************************************************/
  /* HELPER FUNCTIONS                                                      */
  /***************************************************************************/

  syntax Bool ::= isStmts(K) [function]
  rule isStmts(S:Stmt) => true
  rule isStmts(S:Stmt Ss:Stmts) => true
  rule isStmts(_) => false [owise]

  syntax Int ::= size(Vals) [function]
  rule size(.Vals) => 0
  rule size(V:Val, Vs:Vals) => 1 +Int size(Vs)

  syntax Val ::= Vals "[" Int "]" [function]
  rule (V:Val, _:Vals)[0] => V
  rule (V:Val, Vs:Vals)[I:Int] => Vs[I -Int 1] requires I >Int 0

endmodule

module LAMBDA

  imports LAMBDA-SEMANTICS

endmodule
