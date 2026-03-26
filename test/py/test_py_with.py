# Test: Python `with` statement (Phase B)
# Tests: __enter__/__exit__, as-target, exception suppression, nested with

# ── Basic context manager ────────────────────────────────────────────────────
class ManagedResource:
    def __init__(self, name):
        self.name = name
        self.active = False

    def __enter__(self):
        self.active = True
        print("open " + self.name)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.active = False
        print("close " + self.name)
        return False   # don't suppress exceptions

with ManagedResource("db") as r:
    print("using " + r.name)
    print(r.active)

print(r.active)

# ── Context manager without `as` ─────────────────────────────────────────────
class Logger:
    def __enter__(self):
        print("log start")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        print("log end")
        return False

with Logger():
    print("logging")

# ── Exception suppression ────────────────────────────────────────────────────
class Suppressor:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        # suppress if an exception occurred
        if exc_type is not None:
            print("suppressed exception")
            return True
        return False

with Suppressor():
    raise ValueError("ignored")

print("after suppressed exception")

# ── Exception NOT suppressed ──────────────────────────────────────────────────
class Passthrough:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        print("__exit__ called")
        return False  # don't suppress

caught = False
try:
    with Passthrough():
        raise RuntimeError("not suppressed")
except RuntimeError:
    caught = True

print("exception propagated: " + str(caught))

# ── Nested with statements ────────────────────────────────────────────────────
class Counter:
    def __init__(self, label):
        self.label = label
        self.entered = 0

    def __enter__(self):
        self.entered += 1
        print("enter " + self.label)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        print("exit " + self.label)
        return False

with Counter("outer") as outer:
    with Counter("inner") as inner:
        print("both open")
        print(outer.entered)
        print(inner.entered)

# ── __enter__ return value bound to target ───────────────────────────────────
class ValueProvider:
    def __enter__(self):
        return 42

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

with ValueProvider() as v:
    print(v)

# ── Class with super() and with ──────────────────────────────────────────────
class BaseCtx:
    def __init__(self, tag):
        self.tag = tag

    def __enter__(self):
        print("base enter " + self.tag)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        print("base exit " + self.tag)
        return False

class ExtCtx(BaseCtx):
    def __enter__(self):
        print("ext enter")
        return super().__enter__()

    def __exit__(self, exc_type, exc_val, exc_tb):
        print("ext exit")
        return super().__exit__(exc_type, exc_val, exc_tb)

with ExtCtx("tag1") as ctx:
    print("in extended " + ctx.tag)
