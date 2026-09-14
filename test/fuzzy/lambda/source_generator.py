"""Small stateful Lambda source producer for semantic fuzzing.

The producer intentionally starts from grammar.js-compatible constructs, while
scope records prevent accidental unresolved identifiers. It does not claim that
Tree-sitter decides the language: parser_differential.py adjudicates any
grammar/C-parser discrepancy under D8.1.2v3 before a candidate is semantic.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import random


@dataclass
class Scope:
    values: list[str] = field(default_factory=list)
    functions: list[tuple[str, int]] = field(default_factory=list)

    def declare_value(self, name: str) -> None:
        self.values.append(name)

    def declare_function(self, name: str, arity: int) -> None:
        self.functions.append((name, arity))


@dataclass(frozen=True)
class GeneratedSource:
    source: str
    family: str
    trace: tuple[str, ...]
    valid: bool


class LambdaSourceGenerator:
    """Generates bounded, deterministic programs with declaration knowledge."""

    def __init__(self, rng: random.Random):
        self.rng = rng
        self.serial = 0
        self.scope = Scope()

    def _name(self, stem: str) -> str:
        name = f"fuzz_{stem}_{self.serial}"
        self.serial += 1
        return name

    def _integer(self) -> str:
        return str(self.rng.choice((0, 1, 2, 3, 7, 11, 42, 127)))

    def _array(self) -> str:
        return "[" + ", ".join(self._integer() for _ in range(self.rng.randint(2, 6))) + "]"

    def generate_valid(self) -> GeneratedSource:
        families = (
            self._arithmetic,
            self._function_call,
            self._closure,
            self._array_pipeline,
            self._map_path,
            self._conditional,
            self._query,
            self._typed_binding,
        )
        return self.rng.choice(families)()

    def generate_invalid(self) -> GeneratedSource:
        value = self._name("invalid")
        kind = self.rng.choice(("missing-close", "empty-group", "missing-arrow"))
        if kind == "missing-close":
            return GeneratedSource(
                f"let {value} = ({self._integer()} + {self._integer()}\n",
                "invalid-syntax", ("delete-closing-delimiter",), False)
        if kind == "empty-group":
            return GeneratedSource(
                f"let {value} = ()\n",
                "invalid-syntax", ("replace-expression-with-empty-group",), False)
        return GeneratedSource(
            f"fn {value}(value) value + {self._integer()}\n",
            "invalid-syntax", ("delete-function-arrow",), False)

    def _arithmetic(self) -> GeneratedSource:
        left = self._name("left")
        right = self._name("right")
        self.scope.declare_value(left)
        self.scope.declare_value(right)
        return GeneratedSource(
            f"let {left} = {self._integer()}\n"
            f"let {right} = {self._integer()}\n"
            f"{left} * {right} + {left}\n",
            "arithmetic", ("declare-value", "binary-expression"), True)

    def _function_call(self) -> GeneratedSource:
        function = self._name("add")
        left = self._name("left")
        right = self._name("right")
        self.scope.declare_function(function, 2)
        self.scope.declare_value(left)
        self.scope.declare_value(right)
        return GeneratedSource(
            f"fn {function}(left, right) => left + right\n"
            f"let {left} = {self._integer()}\n"
            f"let {right} = {self._integer()}\n"
            f"{function}({left}, {right})\n",
            "function", ("declare-function", "resolved-call"), True)

    def _closure(self) -> GeneratedSource:
        maker = self._name("make_adder")
        closure = self._name("closure")
        self.scope.declare_function(maker, 1)
        self.scope.declare_value(closure)
        return GeneratedSource(
            f"fn {maker}(base) {{\n"
            "  (value) => base + value\n"
            "}\n"
            f"let {closure} = {maker}({self._integer()})\n"
            f"{closure}({self._integer()})\n",
            "closure", ("declare-function", "capture", "resolved-call"), True)

    def _array_pipeline(self) -> GeneratedSource:
        values = self._name("values")
        self.scope.declare_value(values)
        return GeneratedSource(
            f"let {values} = {self._array()}\n"
            f"sum({values} |> ~ * {self._integer()})\n",
            "pipeline", ("declare-array", "pipe-current-item", "sysfunc-sum"), True)

    def _map_path(self) -> GeneratedSource:
        record = self._name("record")
        self.scope.declare_value(record)
        return GeneratedSource(
            f"let {record} = {{value: {self._integer()}, nested: {{other: {self._integer()}}}}}\n"
            f"{record}.value + {record}.nested.other\n",
            "map-path", ("declare-map", "member-path"), True)

    def _conditional(self) -> GeneratedSource:
        selected = self._name("selected")
        left = self._integer()
        right = self._integer()
        self.scope.declare_value(selected)
        return GeneratedSource(
            f"let {selected} = if ({left} > {right}) {left} else {right}\n"
            f"{selected}\n",
            "conditional", ("if-expression",), True)

    def _query(self) -> GeneratedSource:
        values = self._name("values")
        selected = self._name("selected")
        self.scope.declare_value(values)
        self.scope.declare_value(selected)
        return GeneratedSource(
            f"let {values} = {self._array()}\n"
            f"let {selected} = for (value in {values} where value > 1) value * 2\n"
            f"sum({selected})\n",
            "query", ("for-binding", "where-clause", "sysfunc-sum"), True)

    def _typed_binding(self) -> GeneratedSource:
        value = self._name("typed")
        self.scope.declare_value(value)
        return GeneratedSource(
            f"let {value}: int = {self._integer()}\n"
            f"{value} + 1\n",
            "typed-binding", ("type-annotation", "representation-variant"), True)
