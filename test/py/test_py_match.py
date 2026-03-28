# Test match/case pattern matching (Phase B)

# --- 1. Literal patterns ---
def test_literal(x):
    match x:
        case 1:
            return "one"
        case 2:
            return "two"
        case 3:
            return "three"
        case _:
            return "other"

print(test_literal(1))   # one
print(test_literal(2))   # two
print(test_literal(3))   # three
print(test_literal(99))  # other

# --- 2. Capture patterns ---
def test_capture(x):
    match x:
        case 0:
            return "zero"
        case n:
            return "captured: " + str(n)

print(test_capture(0))   # zero
print(test_capture(42))  # captured: 42

# --- 3. Wildcard pattern ---
def test_wildcard(x):
    match x:
        case 1:
            return "one"
        case _:
            return "wildcard"

print(test_wildcard(1))   # one
print(test_wildcard(99))  # wildcard

# --- 4. OR (union) patterns ---
def test_or(x):
    match x:
        case 1 | 2 | 3:
            return "small"
        case 4 | 5 | 6:
            return "medium"
        case _:
            return "large"

print(test_or(2))    # small
print(test_or(5))    # medium
print(test_or(100))  # large

# --- 5. String literal patterns ---
def greet(lang):
    match lang:
        case "en":
            return "Hello"
        case "es":
            return "Hola"
        case "fr":
            return "Bonjour"
        case _:
            return "Unknown"

print(greet("en"))   # Hello
print(greet("es"))   # Hola
print(greet("fr"))   # Bonjour
print(greet("de"))   # Unknown

# --- 6. Sequence (list) patterns ---
def test_seq(lst):
    match lst:
        case []:
            return "empty"
        case [x]:
            return "one: " + str(x)
        case [x, y]:
            return "two: " + str(x) + ", " + str(y)
        case [x, y, *rest]:
            return "many: " + str(x) + ", " + str(y) + ", rest=" + str(len(rest))

print(test_seq([]))           # empty
print(test_seq([42]))         # one: 42
print(test_seq([1, 2]))       # two: 1, 2
print(test_seq([1, 2, 3, 4])) # many: 1, 2, rest=2

# --- 7. Guard clauses ---
def test_guard(x):
    match x:
        case n if n < 0:
            return "negative"
        case n if n == 0:
            return "zero"
        case n if n > 0:
            return "positive"

print(test_guard(-5))  # negative
print(test_guard(0))   # zero
print(test_guard(7))   # positive

# --- 8. AS patterns ---
def test_as(x):
    match x:
        case [1, y] as pair:
            return "matched as pair, y=" + str(y)
        case _:
            return "no match"

print(test_as([1, 99]))  # matched as pair, y=99
print(test_as([2, 99]))  # no match

# --- 9. Boolean and None literals ---
def test_bool_none(x):
    match x:
        case True:
            return "true"
        case False:
            return "false"
        case None:
            return "none"
        case _:
            return "other"

print(test_bool_none(True))   # true
print(test_bool_none(False))  # false
print(test_bool_none(None))   # none
print(test_bool_none(42))     # other

# --- 10. Nested sequence patterns ---
def test_nested(lst):
    match lst:
        case [[a, b], c]:
            return "nested: a=" + str(a) + " b=" + str(b) + " c=" + str(c)
        case _:
            return "no match"

print(test_nested([[1, 2], 3]))  # nested: a=1 b=2 c=3
print(test_nested([1, 2, 3]))    # no match

# --- 11. Class patterns ---
class Point:
    __match_args__ = ("x", "y")
    def __init__(self, x, y):
        self.x = x
        self.y = y

def classify_point(p):
    match p:
        case Point(x=0, y=0):
            return "origin"
        case Point(x=0, y=y):
            return "y-axis at " + str(y)
        case Point(x=x, y=0):
            return "x-axis at " + str(x)
        case Point(x=x, y=y):
            return "(" + str(x) + ", " + str(y) + ")"

print(classify_point(Point(0, 0)))   # origin
print(classify_point(Point(0, 5)))   # y-axis at 5
print(classify_point(Point(3, 0)))   # x-axis at 3
print(classify_point(Point(1, 2)))   # (1, 2)

# --- 12. Mapping patterns ---
def process_event(event):
    match event:
        case {"action": "buy", "item": item}:
            return "buying " + str(item)
        case {"action": "sell", "item": item, "qty": qty}:
            return "selling " + str(qty) + " of " + str(item)
        case {"action": action}:
            return "unknown action: " + str(action)

print(process_event({"action": "buy", "item": "apple"}))                        # buying apple
print(process_event({"action": "sell", "item": "banana", "qty": 3}))            # selling 3 of banana
print(process_event({"action": "donate", "item": "carrot"}))                    # unknown action: donate
