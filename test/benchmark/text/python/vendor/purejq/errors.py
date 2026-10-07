"""Exception types used across purejq."""
from __future__ import annotations


class JqParseError(Exception):
    """Raised when a jq program fails to lex or parse."""


class JqError(Exception):
    """A jq runtime error. Carries an arbitrary JSON value (usually a string)."""

    def __init__(self, value):
        self.value = value
        super().__init__(self._message())

    def _message(self):
        if isinstance(self.value, str):
            return self.value
        try:
            from .encoder import encode
            return encode(self.value) + " (not a string)"
        except Exception:
            return repr(self.value)


class JqBreak(Exception):
    """Internal: control-flow for label/break. Never escapes a program."""

    def __init__(self, token):
        self.token = token


class Halt(Exception):
    """Raised by halt/halt_error. Carries the exit code and optional payload."""

    def __init__(self, code=0, payload=None):
        self.code = code
        self.payload = payload
        super().__init__("halt")
