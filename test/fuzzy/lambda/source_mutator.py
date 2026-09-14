"""Recorded structural mutations for Lambda fuzz inputs.

Valid-preserving operations only touch declared scalar bindings. Invalid
operations name the invariant they intentionally break, keeping the two input
contracts separate (S16, D1.9, D8.2.5v2).
"""

from __future__ import annotations

from dataclasses import dataclass
import random
import re


@dataclass(frozen=True)
class StructuredMutation:
    source: str
    operation: str
    valid: bool


def mutate_valid(source: str, rng: random.Random) -> StructuredMutation:
    integers = list(re.finditer(r"\b(?:0|[1-9][0-9]*)\b", source))
    if integers:
        match = rng.choice(integers)
        replacement = str(rng.choice((0, 1, 2, 7, 42)))
        return StructuredMutation(
            source[:match.start()] + replacement + source[match.end():],
            "replace-literal", True)
    return StructuredMutation("(" + source.rstrip() + ")\n", "group-program", True)


def mutate_invalid(source: str, rng: random.Random) -> StructuredMutation:
    delimiters = [index for index, char in enumerate(source) if char in ")]}"]
    if delimiters:
        index = rng.choice(delimiters)
        return StructuredMutation(source[:index] + source[index + 1:], "delete-delimiter", False)
    return StructuredMutation(source + "\nfuzz_undefined\n", "append-undefined-name", False)
