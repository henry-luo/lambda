# Python Support in Lambda

Lambda can run Python scripts via the `py` command:

```bash
./lambda.exe py script.py
```

Python source is compiled to native machine code through Tree-sitter parsing, AST construction, and MIR JIT compilation. All Python values are represented as Lambda `Item` values — there is no conversion boundary between the two runtimes.

This document lists every Python language feature and its support status.

---

## Supported Features

### Data Types

| Type | Status | Notes |
|------|--------|-------|
| `int` | **Full** | 56-bit inline; overflows to 64-bit heap |
| `float` | **Full** | 64-bit IEEE 754 double |
| `bool` | **Full** | `True`/`False` |
| `None` | **Full** | |
| `str` | **Full** | Interned via name pool; escape sequences supported |
| `list` | **Full** | Lambda `Array`, dynamic growth |
| `tuple` | **Full** | Stored as `Array` (immutable semantics not enforced) |
| `dict` | **Full** | Lambda `Map` with `ShapeEntry` chain |
| `set` | **Partial** | Stored as list — no deduplication or set operations |
| `bytes` / `bytearray` | **Not supported** | |
| `complex` | **Not supported** | |
| `frozenset` | **Not supported** | |

### Literals

| Literal | Status | Notes |
|---------|--------|-------|
| Integers (decimal) | **Full** | `42`, `-7` |
| Integers (hex/oct/bin) | **Full** | `0xff`, `0o77`, `0b101` |
| Floats | **Full** | `3.14`, `1e-5` |
| Strings | **Full** | Single/double quotes, escape sequences (`\n`, `\t`, `\\`, etc.) |
| Triple-quoted strings | **Parsed** | Newlines preserved |
| Raw strings (`r"..."`) | **Partial** | Parsed but raw escape handling may be incomplete |
| Byte strings (`b"..."`) | **Not supported** | |
| F-strings | **Partial** | Basic interpolation `f"{x}"` works; format specs `f"{x:.2f}"` and `f"{x=}"` not supported |
| `True` / `False` / `None` | **Full** | |

### Operators

| Category | Operators | Status |
|----------|-----------|--------|
| Arithmetic | `+`, `-`, `*`, `/`, `//`, `%`, `**` | **Full** — Python semantics (floor div rounds toward −∞, modulo has sign of divisor) |
| Unary | `-x`, `+x`, `~x` | **Full** |
| Bitwise | `&`, `\|`, `^`, `<<`, `>>` | **Full** |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` | **Full** |
| Identity | `is`, `is not` | **Full** |
| Membership | `in`, `not in` | **Full** — works on lists, strings, dicts |
| Boolean | `and`, `or`, `not` | **Full** — value-returning with short-circuit |
| Chained comparison | `a < b < c` | **Full** — proper short-circuit evaluation |
| Ternary | `x if cond else y` | **Full** |
| Augmented assignment | `+=`, `-=`, `*=`, `/=`, `//=`, `%=`, `**=`, `&=`, `\|=`, `^=`, `<<=`, `>>=` | **Full** — variables and subscripts |
| Matmul | `@`, `@=` | **Not supported** — operator parsed but no-op |
| Walrus | `:=` | **Not supported** |
| String `%` formatting | `"hello %s" % name` | **Stub** — returns left operand unchanged |

### Numeric Semantics

| Behavior | Status |
|----------|--------|
| True division `/` always returns float | **Full** |
| Floor division `//` rounds toward −∞ | **Full** |
| Modulo `%` result has sign of divisor | **Full** |
| Negative exponents return float | **Full** |
| Integer overflow → float promotion | **Full** |
| String repetition `"ab" * 3` | **Full** |
| List repetition `[1] * 3` | **Full** |
| List concatenation `[1] + [2]` | **Full** |
| String concatenation `"a" + "b"` | **Full** |

### Variables & Assignment

| Feature | Status | Notes |
|---------|--------|-------|
| Simple assignment `x = 5` | **Full** | |
| Multiple targets `a = b = 5` | **Full** | Walks targets list |
| Tuple unpacking `a, b = 1, 2` | **Full** | |
| Subscript assignment `a[0] = 5` | **Full** | |
| Attribute assignment `obj.x = 5` | **Full** | |
| Augmented subscript `a[0] += 1` | **Full** | |
| Starred unpacking `a, *rest = [1,2,3]` | **Not supported** | Parsed but not transpiled |
| Slice assignment `a[1:3] = [4,5]` | **Not supported** | |

### Control Flow

| Feature | Status | Notes |
|---------|--------|-------|
| `if` / `elif` / `else` | **Full** | Arbitrary nesting |
| `while` loop | **Full** | With `break` / `continue` |
| `for` loop | **Full** | Index-based iteration over lists/ranges |
| `for` with tuple unpacking | **Full** | `for a, b in pairs:` |
| `break` / `continue` | **Full** | |
| `pass` | **Full** | |
| `for...else` | **Not supported** | |
| `while...else` | **Not supported** | |
| `match` / `case` (3.10+) | **Not supported** | |

### Functions

| Feature | Status | Notes |
|---------|--------|-------|
| `def` with positional params | **Full** | Up to 16 parameters |
| `return` with value | **Full** | Implicit `return None` at end |
| Nested function definitions | **Full** | Basic scoping |
| Forward references | **Full** | Functions collected in pre-pass (callable before definition) |
| Default parameter values | **Not supported** | Parsed but defaults not emitted |
| Keyword arguments in calls | **Not supported** | Parsed but silently skipped |
| `*args` parameter | **Not supported** | |
| `**kwargs` parameter | **Not supported** | |
| `*args` unpacking in calls | **Not supported** | |
| `**kwargs` unpacking in calls | **Not supported** | |
| Closures (variable capture) | **Not supported** | Infrastructure exists but not wired up |
| Recursion | **Full** | |

### Built-in Functions (29 implemented)

| Function | Status | Notes |
|----------|--------|-------|
| `print(...)` | **Partial** | Positional args only; `sep=`/`end=` not supported |
| `len(x)` | **Full** | Strings, lists, dicts |
| `type(x)` | **Full** | Returns `<class 'int'>` etc. |
| `isinstance(x, t)` | **Stub** | Parsed but simplified |
| `int(x)` | **Full** | Numeric/string conversion |
| `float(x)` | **Full** | Numeric/string conversion |
| `str(x)` | **Full** | Converts any value to string |
| `bool(x)` | **Full** | Python truthiness rules |
| `abs(x)` | **Full** | Int and float |
| `range(...)` | **Full** | 1/2/3 args, negative step supported |
| `min(...)` | **Full** | Variadic |
| `max(...)` | **Full** | Variadic |
| `sum(iterable)` | **Full** | |
| `sorted(iterable)` | **Partial** | No `key=` or `reverse=` arguments |
| `reversed(iterable)` | **Full** | Returns new list |
| `enumerate(iterable)` | **Full** | Returns list of `(i, val)` tuples |
| `zip(...)` | **Full** | |
| `map(f, iterable)` | **Full** | |
| `filter(f, iterable)` | **Full** | |
| `ord(c)` | **Full** | Character → int |
| `chr(n)` | **Full** | Int → character |
| `input(prompt)` | **Full** | Reads from stdin |
| `repr(x)` | **Full** | |
| `hash(x)` | **Full** | |
| `id(x)` | **Full** | |
| `list(iterable)` | **Full** | |
| `dict(...)` | **Stub** | Constructor not fully implemented |
| `set(iterable)` | **Stub** | No real set type |
| `tuple(iterable)` | **Full** | |

**Not implemented:** `all`, `any`, `bin`, `oct`, `hex`, `divmod`, `pow` (3-arg), `callable`, `compile`, `eval`, `exec`, `dir`, `vars`, `getattr`, `setattr`, `delattr`, `hasattr`, `globals`, `locals`, `ascii`, `bytes`, `bytearray`, `complex`, `frozenset`, `format`, `iter`, `next`, `open`, `property`, `staticmethod`, `classmethod`, `super`, `round`, `slice`.

### String Methods (17 implemented)

| Method | Status |
|--------|--------|
| `upper()` | **Full** |
| `lower()` | **Full** |
| `strip()` | **Full** |
| `lstrip()` | **Full** |
| `rstrip()` | **Full** |
| `split(sep=None)` | **Full** — whitespace or explicit separator |
| `join(iterable)` | **Full** |
| `replace(old, new)` | **Full** |
| `find(sub)` | **Full** — returns -1 on miss |
| `startswith(prefix)` | **Full** |
| `endswith(suffix)` | **Full** |
| `count(sub)` | **Full** |
| `isdigit()` | **Full** |
| `isalpha()` | **Full** |
| `title()` | **Full** |
| `capitalize()` | **Full** |
| `format(...)` | **Stub** — returns string unchanged |

**Not implemented:** `casefold`, `center`, `encode`, `expandtabs`, `index`, `isalnum`, `isdecimal`, `islower`, `isnumeric`, `isprintable`, `isspace`, `istitle`, `isupper`, `ljust`, `maketrans`, `partition`, `rfind`, `rindex`, `rjust`, `rpartition`, `rsplit`, `splitlines`, `swapcase`, `translate`, `zfill`.

### List Methods (11 implemented)

| Method | Status |
|--------|--------|
| `append(x)` | **Full** |
| `extend(iterable)` | **Full** |
| `insert(i, x)` | **Full** |
| `pop(i=-1)` | **Full** |
| `remove(x)` | **Full** |
| `index(x)` | **Full** |
| `count(x)` | **Full** |
| `sort()` | **Full** — insertion sort, no `key=`/`reverse=` |
| `reverse()` | **Full** |
| `copy()` | **Full** |
| `clear()` | **Full** |

### Dict Methods (7 implemented)

| Method | Status | Notes |
|--------|--------|-------|
| `keys()` | **Full** | Returns list |
| `values()` | **Full** | Returns list |
| `items()` | **Full** | Returns list of tuples |
| `get(key, default=None)` | **Full** | |
| `update(other)` | **Full** | |
| `pop(key)` | **Partial** | Returns value but may not remove from shape |
| `clear()` | **Full** | |

**Not implemented:** `copy`, `fromkeys`, `popitem`, `setdefault`.

### Truthiness

Follows Python's truthiness rules exactly:

| Value | Result |
|-------|--------|
| `None` | `False` |
| `False` | `False` |
| `0`, `0.0` | `False` |
| `""` | `False` |
| `[]` | `False` |
| `{}` | `False` |
| Everything else | `True` |

### Scoping

| Feature | Status | Notes |
|---------|--------|-------|
| Local scope | **Full** | Assignment creates in current scope |
| Module (global) scope | **Full** | Top-level variables |
| Variable lookup (inner → outer) | **Full** | LEGB chain walk |
| `global` declaration | **Parsed** | Scope analysis only — no runtime effect |
| `nonlocal` declaration | **Parsed** | Scope analysis only — no runtime effect |

### Assertions

| Feature | Status |
|---------|--------|
| `assert condition` | **Full** — raises AssertionError on failure |
| `assert condition, message` | **Full** |

### Subscripting & Indexing

| Feature | Status |
|---------|--------|
| List indexing `a[0]` | **Full** |
| Negative indexing `a[-1]` | **Full** |
| Dict key access `d["key"]` | **Full** |
| String indexing `s[0]` | **Full** |
| Subscript assignment `a[0] = x` | **Full** |
| Dict key assignment `d["k"] = v` | **Full** |
| Slicing `a[1:3]` | **Not supported** |
| Slice with step `a[::2]` | **Not supported** |
| Slice assignment `a[1:3] = [4,5]` | **Not supported** |

---

## Not Supported

### Comprehensions & Generators

All comprehension types are parsed into AST nodes but have no MIR code generation:

- List comprehensions: `[x*2 for x in lst]`
- Dict comprehensions: `{k: v for k, v in items}`
- Set comprehensions: `{x*2 for x in lst}`
- Generator expressions: `(x*2 for x in lst)`

### Lambda Expressions

`lambda x: x*2` — parsed into `PY_AST_NODE_LAMBDA` but not transpiled.

### Classes

Class definitions are parsed (name, bases, body, decorators) but not transpiled:

- `class` definitions
- Instance creation / constructors
- Inheritance
- Static / class methods
- Properties
- Operator overloading (`__add__`, `__len__`, etc.)
- `super()`

### Exception Handling

| Feature | Status |
|---------|--------|
| `assert` | Supported (see above) |
| `raise` | Parsed but not transpiled |
| `try` / `except` | Parsed but not transpiled |
| `finally` | Parsed but not transpiled |
| `except ExceptionType as e` | Parsed but not transpiled |
| Exception chaining (`from`) | Not supported |

The runtime has exception infrastructure (`py_raise`, `py_check_exception`, `py_clear_exception`) but the transpiler does not emit `try`/`except` blocks.

### Import System

`import` and `from...import` are parsed but not transpiled. There is no module system.

### Decorators

Decorator syntax is parsed (decorator expressions linked to functions/classes) but decorators are not applied at runtime.

### With Statement

`with` statements are parsed into AST but not transpiled. No context manager protocol (`__enter__`/`__exit__`).

### Delete

`del` is parsed and has a transpiler case but is currently a no-op.

### Async / Concurrency

Not parsed or supported:

- `async def` / `await`
- `yield` / `yield from` / generators
- `async with` / `async for`

### Advanced Argument Handling

| Feature | Status |
|---------|--------|
| Default parameters `def f(x=5)` | Parsed but defaults not emitted |
| Keyword arguments `f(a=1)` | Parsed but silently skipped |
| `*args` parameter | Not supported |
| `**kwargs` parameter | Not supported |
| `*args` in calls | Not supported |
| `**kwargs` in calls | Not supported |
| Positional-only `/` | Not supported |
| Keyword-only `*` | Not supported |

### Slicing

No slice operations are supported — `a[1:3]`, `a[::-1]`, `a[::2]` all return null.

### Advanced String Features

| Feature | Status |
|---------|--------|
| F-string format specs `f"{x:.2f}"` | Not supported |
| F-string `=` debug `f"{x=}"` | Not supported |
| `str.format()` with substitution | Stub — returns string unchanged |
| `%` formatting `"hello %s" % name` | Stub — returns left string |

### Collection Unpacking

| Feature | Status |
|---------|--------|
| Starred unpacking `a, *rest = lst` | Not supported |
| List spread `[*a, *b]` | Not supported |
| Dict spread `{**d1, **d2}` | Not supported |

### Other Missing Features

- `match` / `case` (Python 3.10+)
- Type annotations (parsed but ignored — no enforcement)
- Closures with variable capture (infrastructure exists but not wired up)
- Multiple return values (works via tuple unpacking)
- Nested comprehensions
- `__name__`, `__doc__`, and introspection
- `global`/`nonlocal` runtime enforcement
- `for...else` / `while...else`

---

## Summary

| Category | Supported | Partial | Not Supported |
|----------|-----------|---------|---------------|
| **Literals & types** | int, float, str, bool, None, list, tuple, dict | set (as list), f-strings | bytes, complex, frozenset |
| **Operators** | All arithmetic, bitwise, comparison, boolean, chained | `%` formatting (stub) | `@` matmul, `:=` walrus |
| **Control flow** | if/elif/else, for, while, break/continue, pass | | for-else, while-else, match/case |
| **Functions** | def, return, nested, recursion, forward refs | print (no kwargs) | defaults, *args, **kwargs, closures, lambda |
| **Builtins** | 29 functions | sorted (no key=), isinstance (stub) | ~35 more standard builtins |
| **String methods** | 16 working | format (stub) | ~25 more methods |
| **List methods** | 11 working | sort (no key=) | — |
| **Dict methods** | 7 working | pop (may not remove) | copy, fromkeys, popitem, setdefault |
| **Indexing** | list, dict, string, negative | | slicing |
| **Comprehensions** | — | — | list, dict, set, generator |
| **Classes** | — | — | All OOP features |
| **Exceptions** | assert | | try/except/finally, raise |
| **Imports** | — | — | All |
| **Async** | — | — | All |
