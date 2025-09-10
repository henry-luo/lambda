# Lambda Script Formal Specifications

This document describes the formal specifications of the Lambda Script language using two complementary approaches: Ott for mathematical specification and K Framework for executable semantics.

## Table of Contents

1. [Overview](#overview)
2. [Ott Specification](#ott-specification)
3. [K Framework Specification](#k-framework-specification)
4. [Comparison of Approaches](#comparison-of-approaches)
5. [Using the Specifications](#using-the-specifications)
6. [Language Features Coverage](#language-features-coverage)
7. [Future Extensions](#future-extensions)

## Overview

Lambda Script is a **pure functional scripting language** designed for data processing and document presentation. To ensure correctness and provide a foundation for implementation, we have developed two formal specifications:

- **Ott Specification** (`Lambda_Spec.ott`): A mathematical specification using Ott's notation for syntax, typing rules, and operational semantics
- **K Framework Specification** (`Lambda_Spec.k`): An executable specification that can be compiled and run to execute Lambda programs

Both specifications cover the complete Lambda Script language including:
- Rich type system with union, intersection, and exclusion types
- Pure functional programming with immutable data structures
- Document processing capabilities through element types
- Comprehensive collection types (lists, arrays, maps, ranges)
- First-class functions and closures
- Pattern matching and destructuring
- Built-in system functions

## Ott Specification

### Purpose and Approach

The Ott specification (`Lambda_Spec.ott`) provides a **mathematical foundation** for Lambda Script using standard formal methods notation. Ott is a tool for writing definitions of programming languages and calculi, designed to be both human-readable and machine-processable.

### Structure

#### Syntax Definition
```
defn BaseType :: BaseType_ ::=
  | null        :: :: Null
  | bool        :: :: Bool
  | int         :: :: Int
  | float       :: :: Float
  | string      :: :: String
  ...
```

The syntax section defines:
- **Base types**: null, bool, int, int64, float, decimal, number, string, symbol, binary, datetime, any, error
- **Type expressions**: Lists, arrays, maps, elements, functions with occurrence operators (?, +, *)
- **Type combinators**: Union (|), intersection (&), exclusion (!)
- **Literals**: All primitive value forms
- **Operators**: Unary and binary operators with proper precedence
- **Expressions**: Complete expression language including let, if-else, for, functions
- **Statements**: Declarations, control flow, type definitions, imports

#### Operational Semantics
```
defn
env |- Expr --> Value :: :: eval :: ''
by

env |- Expr --> Value   env, x = Value |- Expr' --> Value'
------------------------------------------------------------ :: let
env |- let x = Expr , Expr' --> Value'
```

The operational semantics define:
- **Values**: Runtime representation of computed results
- **Environments**: Variable binding contexts
- **Evaluation rules**: How expressions reduce to values
- **Memory management**: Reference counting semantics

#### Type System
```
defn
tenv |- Expr : Type :: :: typing :: ''
by

tenv |- Expr1 : int   tenv |- Expr2 : int
---------------------------------------- :: add_int
tenv |- Expr1 + Expr2 : int
```

The type system includes:
- **Subtyping relations**: Type hierarchy and compatibility
- **Typing rules**: How expressions receive types
- **Well-formedness**: Rules for valid type expressions
- **Type inference**: Implicit type derivation

### Key Features

1. **Mathematical Rigor**: Precise definitions using established formal methods
2. **Human Readable**: Clear notation accessible to language designers and implementers
3. **Tool Support**: Can generate LaTeX documentation and proof assistants
4. **Completeness**: Covers all language constructs systematically
5. **Extensibility**: Easy to add new features while maintaining consistency

### Usage

The Ott specification serves multiple purposes:

- **Language Design**: Provides clarity during language development
- **Implementation Guide**: Serves as authoritative reference for compiler writers
- **Documentation**: Generates precise language documentation
- **Verification**: Foundation for formal proofs about language properties
- **Teaching**: Educational resource for understanding language semantics

## K Framework Specification

### Purpose and Approach

The K Framework specification (`Lambda_Spec.k`) provides an **executable definition** of Lambda Script. K is a rewrite-based executable semantic framework that allows the specification to be compiled into a working interpreter.

### Structure

#### Syntax Module (`LAMBDA-SYNTAX`)
```k
syntax Type ::= BaseType
              | Id
              | "(" Type "," Types ")"
              | "[" Type "]"
              | "{" TypeBindings "}"
              ...
```

Defines the complete concrete syntax of Lambda Script with:
- **Tokens**: Regular expressions for literals (decimal, symbol, binary, datetime)
- **Productions**: BNF-style grammar rules
- **Precedence**: Operator precedence and associativity
- **Attributes**: Parsing annotations and labels

#### Configuration Module (`LAMBDA-CONFIGURATION`)
```k
configuration <lambda>
  <k> $PGM:Program </k>
  <env> .Env </env>
  <heap> .Heap </heap>
  <tenv> .TEnv </tenv>
  <nextRef> 0 </nextRef>
  <output stream="stdout"> .List </output>
</lambda>
```

Defines the runtime state structure:
- **Computation (`<k>`)**: Current expression being evaluated
- **Environment (`<env>`)**: Variable bindings
- **Heap (`<heap>`)**: Mutable references (for arrays/maps)
- **Type Environment (`<tenv>`)**: Type information for checking
- **Output Stream**: For print function and debugging

#### Semantics Module (`LAMBDA-SEMANTICS`)
```k
rule <k> I1:Int + I2:Int => I1 +Int I2 </k>

rule <k> let X:Id = E1:Expr, E2:Expr => E1 ~> freezerLet(X, E2) </k>

syntax KItem ::= freezerLet(Id, Expr)
rule <k> V:Val ~> freezerLet(X:Id, E:Expr) => E </k>
     <env> Env:Env => Env[X <- V] </env>
```

Defines the execution semantics through rewrite rules:
- **Evaluation rules**: How each construct reduces
- **Continuations**: Handling of complex evaluation contexts
- **Environment management**: Variable binding and lookup
- **Built-in functions**: System functions like len, type, print
- **Error handling**: Runtime error detection and reporting

### Key Features

1. **Executable**: Can be compiled with `kompile` and run with `krun`
2. **Complete**: Covers all language features with working implementations
3. **Debuggable**: Supports step-by-step execution and state inspection
4. **Testable**: Can run actual Lambda programs to verify semantics
5. **Tool Generation**: Automatically generates interpreters and debuggers

### Usage

The K specification enables:

- **Rapid Prototyping**: Quick implementation of language changes
- **Testing**: Verification of language behavior on real programs
- **Education**: Interactive exploration of language semantics
- **Implementation**: Foundation for production interpreters
- **Analysis**: Automatic generation of program analysis tools

#### Running Lambda Programs

```bash
# Compile the specification
kompile lambda.k

# Run a Lambda program
krun program.ls

# Debug execution step-by-step
krun program.ls --debugger
```

## Comparison of Approaches

| Aspect | Ott Specification | K Framework Specification |
|--------|------------------|---------------------------|
| **Purpose** | Mathematical foundation | Executable implementation |
| **Notation** | Mathematical/logical | Rewrite rules |
| **Output** | LaTeX, proof assistants | Working interpreter |
| **Verification** | Formal proofs | Testing and validation |
| **Learning Curve** | Moderate (formal methods) | Steep (K Framework) |
| **Maintainability** | High (declarative) | High (executable) |
| **Tool Support** | LaTeX generation | Full toolchain |
| **Performance** | N/A | Reasonable for prototyping |

### Complementary Strengths

The two specifications complement each other:

- **Ott provides mathematical rigor** for theoretical understanding and formal verification
- **K provides executable semantics** for practical validation and tool generation
- **Both ensure consistency** by specifying the same language from different perspectives
- **Cross-validation** helps catch errors and ambiguities in either specification

## Using the Specifications

### For Language Designers

1. **Use Ott** to explore design decisions and their formal implications
2. **Use K** to rapidly prototype and test language features
3. **Cross-reference** both specifications to ensure consistency
4. **Generate documentation** from Ott for mathematical precision

### For Implementers

1. **Start with Ott** to understand the mathematical foundation
2. **Reference K rules** for implementation guidance
3. **Test against K** to validate implementation correctness
4. **Use both** for different aspects (type checking vs. runtime)

### For Researchers

1. **Ott specification** provides foundation for formal analysis
2. **K specification** enables empirical validation
3. **Both together** support comprehensive language study
4. **Extension points** clearly defined in both frameworks

### For Educators

1. **Ott rules** teach formal semantics concepts
2. **K execution** provides hands-on experience
3. **Side-by-side comparison** illustrates different approaches
4. **Real examples** can be run and analyzed

## Language Features Coverage

Both specifications comprehensively cover:

### Core Language
- ✅ Literals (null, bool, int, float, string, symbol, binary, datetime)
- ✅ Variables and environments
- ✅ Let expressions and statements
- ✅ Arithmetic and logical operations
- ✅ Comparison operations
- ✅ Conditional expressions (if-then-else)

### Collections
- ✅ Lists `(1, 2, 3)` - immutable ordered sequences
- ✅ Arrays `[1, 2, 3]` - mutable ordered collections
- ✅ Maps `{key: value}` - key-value mappings
- ✅ Ranges `1 to 10` - numeric sequences
- ✅ Elements `<tag attr: value; content>` - structured markup

### Functions
- ✅ Function definitions (statement and expression forms)
- ✅ Function calls with parameter binding
- ✅ Closures with lexical scoping
- ✅ Higher-order functions
- ✅ Built-in system functions

### Type System
- ✅ Base types (null, bool, int, float, string, etc.)
- ✅ Composite types (lists, arrays, maps, functions)
- ✅ Type operators (optional ?, union |, intersection &)
- ✅ Subtyping relations
- ✅ Type inference and checking

### Control Flow
- ✅ Conditional expressions and statements
- ✅ For loops and comprehensions
- ✅ Pattern matching through destructuring
- ✅ Error handling and propagation

### Advanced Features
- ✅ Module system and imports
- ✅ Type definitions (aliases, entities, objects)
- ✅ Document processing elements
- ✅ Unicode and internationalization support
- ✅ Memory management through reference counting

## Future Extensions

Both specifications are designed for extensibility:

### Planned Language Features
- **Pattern matching**: Explicit pattern matching syntax
- **Async/await**: Asynchronous computation support
- **Macros**: Compile-time code generation
- **FFI**: Foreign function interface

### Specification Enhancements
- **Ott improvements**: 
  - More detailed memory model
  - Concurrency semantics
  - Resource management
- **K improvements**:
  - Performance optimizations
  - Better error messages
  - IDE integration
  - Static analysis passes

### Tool Development
- **Ott-based tools**:
  - Proof assistant integration (Coq, Isabelle)
  - Automated theorem proving
  - Type soundness proofs
- **K-based tools**:
  - Debugger enhancements
  - Profiling support
  - Test case generation
  - Program synthesis

## Conclusion

The dual specification approach using Ott and K Framework provides Lambda Script with:

1. **Mathematical rigor** through formal semantics
2. **Practical validation** through executable specifications
3. **Comprehensive coverage** of all language features
4. **Tool generation** capabilities for development
5. **Educational value** for understanding language design
6. **Implementation guidance** for compiler writers
7. **Research foundation** for formal verification

Together, these specifications ensure that Lambda Script has a solid theoretical foundation while remaining practical and implementable. They serve as living documentation that evolves with the language and provides confidence in its design and implementation.

The specifications are maintained alongside the language implementation to ensure consistency and serve as the authoritative reference for Lambda Script's semantics. They represent a commitment to rigorous language design and provide a foundation for future development and research.
