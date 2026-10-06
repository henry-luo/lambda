"""Command-line interface, mirroring the common subset of jq's flags."""
from __future__ import annotations

import argparse
import json
import sys

from . import Program
from .encoder import encode, encode_pretty
from .errors import Halt, JqError, JqParseError

try:  # optional accelerator (pip install purejq[speed]); never required
    import orjson as _orjson
except ImportError:
    _orjson = None


def _iter_json(text):
    if _orjson is not None:
        try:
            yield _orjson.loads(text)
            return
        except Exception:
            pass  # multi-document stream, NaN, etc.: use the stdlib decoder
    dec = json.JSONDecoder()
    i = 0
    n = len(text)
    while i < n:
        while i < n and text[i] in " \t\r\n":
            i += 1
        if i >= n:
            return
        value, i = dec.raw_decode(text, i)
        yield value


_WS = " \t\r\n"
_STRUCT = set("{}[]:,")
_DELIM = set("{}[]:, \t\r\n")


def _tokenize(fileobj, chunk_size):
    """Yield JSON tokens incrementally from a file object. Structural tokens
    are the characters {}[]:, ; scalar tokens are ('val', parsed_value)."""
    buf = ""
    pos = 0
    eof = False
    while True:
        while pos < len(buf) and buf[pos] in _WS:
            pos += 1
        if pos >= len(buf):
            if eof:
                return
            buf, pos = "", 0
            chunk = fileobj.read(chunk_size)
            if chunk == "":
                eof = True
            else:
                buf += chunk
            continue
        ch = buf[pos]
        if ch in _STRUCT:
            yield ch
            pos += 1
            continue
        if ch == '"':
            end = _string_end(buf, pos)
            while end is None:
                if eof:
                    raise ValueError("unterminated string")
                chunk = fileobj.read(chunk_size)
                if chunk == "":
                    eof = True
                else:
                    buf += chunk
                end = _string_end(buf, pos)
            yield ("val", json.loads(buf[pos:end]))
            pos = end
            continue
        # number or literal: read until a delimiter, pulling more data if the
        # token might continue past the current buffer
        end = pos
        while True:
            while end < len(buf) and buf[end] not in _DELIM:
                end += 1
            if end < len(buf) or eof:
                break
            chunk = fileobj.read(chunk_size)
            if chunk == "":
                eof = True
            else:
                buf += chunk
        yield ("val", json.loads(buf[pos:end]))
        pos = end


def _string_end(buf, pos):
    """Index just past the closing quote of the string starting at buf[pos],
    or None if the string isn't fully buffered yet."""
    i = pos + 1
    n = len(buf)
    while i < n:
        c = buf[i]
        if c == "\\":
            if i + 1 >= n:
                return None
            i += 2
        elif c == '"':
            return i + 1
        else:
            i += 1
    return None


def _stream_events(fileobj, chunk_size=1 << 16):
    """Emit jq --stream events for every top-level value, incrementally.

    Each leaf yields [path, value]; closing a non-empty array/object yields
    [path_to_its_last_element]; an empty array/object is itself a leaf. Memory
    stays bounded by the path depth plus one chunk, so arbitrarily large
    documents stream in roughly constant space.
    """
    # frame = [kind, slot] where kind is "arr" (slot=next index) or
    # "obj" (slot=current key, or None before the first key)
    stack = []
    expect_key = False

    def below_path():
        return [f[1] for f in stack]

    def advance_parent():
        if stack and stack[0 - 1][0] == "arr":
            stack[-1][1] += 1

    for tok in _tokenize(fileobj, chunk_size):
        if tok == "{":
            stack.append(["obj", None])
            expect_key = True
        elif tok == "[":
            stack.append(["arr", 0])
        elif tok == ":":
            pass
        elif tok == ",":
            if stack and stack[-1][0] == "obj":
                expect_key = True
        elif tok == "}" or tok == "]":
            kind, slot = stack.pop()
            empty = (kind == "arr" and slot == 0) or (kind == "obj" and slot is None)
            if empty:
                yield [below_path(), [] if kind == "arr" else {}]
            else:
                last = slot - 1 if kind == "arr" else slot
                yield [below_path() + [last]]
            advance_parent()
        else:  # ("val", value)
            value = tok[1]
            if expect_key:
                stack[-1][1] = value
                expect_key = False
            else:
                yield [[f[1] for f in stack], value]
                advance_parent()
    if stack:
        raise ValueError("Unfinished JSON term at EOF")


def main(argv=None):
    from . import __version__
    ap = argparse.ArgumentParser(
        prog="purejq", description="purejq - a pure Python implementation of jq")
    ap.add_argument("-V", "--version", action="version",
                    version="purejq %s" % __version__)
    ap.add_argument("program", nargs="?", default=None, help="jq filter to run")
    ap.add_argument("files", nargs="*", help="input files (default: stdin)")
    ap.add_argument("-n", "--null-input", action="store_true",
                    help="use null as the single input value")
    ap.add_argument("-r", "--raw-output", action="store_true",
                    help="output strings without JSON quotes")
    ap.add_argument("-j", "--join-output", action="store_true",
                    help="like -r but without trailing newlines")
    ap.add_argument("-c", "--compact-output", action="store_true",
                    help="compact instead of pretty-printed output")
    ap.add_argument("-s", "--slurp", action="store_true",
                    help="read all inputs into a single array")
    ap.add_argument("--stream", action="store_true",
                    help="parse input in streaming form ([path, leaf] events), "
                         "for huge files in constant memory")
    ap.add_argument("-R", "--raw-input", action="store_true",
                    help="read each line of input as a string (with -s, the whole input)")
    ap.add_argument("-S", "--sort-keys", action="store_true",
                    help="sort object keys in the output")
    ap.add_argument("-a", "--ascii-output", action="store_true",
                    help="escape non-ASCII characters as \\uXXXX")
    ap.add_argument("--indent", type=int, default=2, metavar="N",
                    help="number of spaces for indentation (0 = compact)")
    ap.add_argument("--tab", action="store_true",
                    help="indent with tabs instead of spaces")
    ap.add_argument("-e", "--exit-status", action="store_true",
                    help="set exit status by the last output value")
    ap.add_argument("-f", "--from-file", metavar="FILE",
                    help="read the filter from a file")
    ap.add_argument("--arg", nargs=2, action="append", default=[],
                    metavar=("NAME", "VALUE"), help="bind $NAME to a string value")
    ap.add_argument("--argjson", nargs=2, action="append", default=[],
                    metavar=("NAME", "JSON"), help="bind $NAME to a JSON value")
    args = ap.parse_args(argv)

    if args.from_file:
        with open(args.from_file) as f:
            source = f.read()
        if args.program is not None:
            args.files.insert(0, args.program)
    elif args.program is not None:
        source = args.program
    else:
        ap.error("no filter given")

    try:
        prog = Program(source)
    except JqParseError as e:
        print("purejq: error: %s" % e, file=sys.stderr)
        return 3

    vars = {}
    for name, value in args.arg:
        vars[name] = value
    for name, value in args.argjson:
        try:
            vars[name] = json.loads(value)
        except ValueError as e:
            print("purejq: invalid JSON for --argjson %s: %s" % (name, e), file=sys.stderr)
            return 2

    if args.stream:
        # Constant-memory path: never materialize the whole input.
        reader = _ChainedReader(args.files) if args.files else sys.stdin
        events = _stream_events(reader)
        try:
            if args.null_input:
                runs = [(None, events)]
            elif args.slurp:
                runs = [(list(events), iter(()))]
            else:
                shared = events if not isinstance(events, list) else iter(events)
                runs = _consume(shared)
            return _emit(prog, runs, args, vars)
        except ValueError as e:
            print("purejq: invalid JSON input: %s" % e, file=sys.stderr)
            return 2

    if args.files:
        text = "".join(open(f).read() for f in args.files)
    elif not args.null_input:
        text = sys.stdin.read()
    else:
        text = ""

    if args.raw_input:
        if args.slurp:
            values = [text]
        else:
            values = text.split("\n")
            if values and values[-1] == "":  # trailing newline isn't a record
                values.pop()
    else:
        try:
            values = list(_iter_json(text))
        except ValueError as e:
            print("purejq: invalid JSON input: %s" % e, file=sys.stderr)
            return 2

    if args.null_input:
        runs = [(None, iter(values))]
    elif args.slurp:
        # -s reads everything as one input: a string under -R, else an array.
        slurped = values[0] if args.raw_input else values
        runs = [(slurped, iter(()))]
    else:
        # Each value is one program run; `input`/`inputs` consume the rest.
        shared = iter(values)
        runs = _consume(shared)

    return _emit(prog, runs, args, vars)


class _ChainedReader:
    """Minimal .read(size) over a sequence of files, opened lazily."""

    def __init__(self, paths):
        self._paths = iter(paths)
        self._cur = None

    def read(self, size):
        while True:
            if self._cur is None:
                try:
                    self._cur = open(next(self._paths))
                except StopIteration:
                    return ""
            chunk = self._cur.read(size)
            if chunk:
                return chunk
            self._cur.close()
            self._cur = None


def _emit(prog, runs, args, vars):
    sort_keys = args.sort_keys
    ascii_out = args.ascii_output
    compact = args.compact_output or args.indent == 0
    last = None
    had_output = False
    code = 0
    out = sys.stdout
    try:
        for value, inputs in runs:
            for result in prog.run(value, inputs=inputs, vars=vars):
                last = result
                had_output = True
                if (args.raw_output or args.join_output) and isinstance(result, str):
                    out.write(result)
                elif compact:
                    out.write(encode(result, sort_keys=sort_keys, ascii=ascii_out))
                else:
                    out.write(encode_pretty(result, indent=args.indent,
                                            sort_keys=sort_keys, ascii=ascii_out,
                                            tab=args.tab))
                if not args.join_output:
                    out.write("\n")
    except JqError as e:
        print("purejq: error: %s" % e, file=sys.stderr)
        code = 5
    except Halt as h:
        if h.payload is not None:
            if isinstance(h.payload, str):
                sys.stderr.write(h.payload)
            else:
                sys.stderr.write(encode(h.payload) + "\n")
        code = h.code
    except ValueError as e:  # malformed JSON surfaced lazily by --stream
        print("purejq: invalid JSON input: %s" % e, file=sys.stderr)
        code = 2
    except BrokenPipeError:
        return 0

    if args.exit_status and code == 0:
        if not had_output:
            code = 4
        elif last is None or last is False:
            code = 1
    return code


def _consume(shared):
    for v in shared:
        yield v, shared


if __name__ == "__main__":
    sys.exit(main())
