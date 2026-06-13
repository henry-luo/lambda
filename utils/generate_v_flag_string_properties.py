#!/usr/bin/env python3
"""
Js54 P11: Generate string-valued property tables for the /v flag.

Extracts matchStrings from the test262 property-escapes string-property test
files and emits a C header. Each property becomes an array of UTF-8 byte
sequences; the rewriter expands \\p{StringProperty} under /v into an
alternation over these entries.

Source: test/js262/test/built-ins/RegExp/property-escapes/generated/strings/
Output: lambda/js/js_regex_string_properties.inc
"""
import os, re, sys, json

PROPS = [
    "Basic_Emoji",
    "Emoji_Keycap_Sequence",
    "RGI_Emoji",
    "RGI_Emoji_Flag_Sequence",
    "RGI_Emoji_Modifier_Sequence",
    "RGI_Emoji_Tag_Sequence",
    "RGI_Emoji_ZWJ_Sequence",
]

SRC_DIR = "test/js262/test/built-ins/RegExp/property-escapes/generated/strings"
OUT = "lambda/js/js_regex_string_properties.inc"

def decode_jsstr(literal):
    """Decode a JS string literal (without the surrounding quotes) to a
    sequence of codepoints. Handles \\uXXXX, \\u{H...}, \\xHH escapes."""
    out = []
    i = 0
    while i < len(literal):
        c = literal[i]
        if c != '\\':
            out.append(ord(c))
            i += 1
            continue
        if i + 1 >= len(literal):
            raise ValueError("trailing backslash")
        nx = literal[i+1]
        if nx == 'u':
            if i + 2 < len(literal) and literal[i+2] == '{':
                end = literal.index('}', i+3)
                out.append(int(literal[i+3:end], 16))
                i = end + 1
            else:
                out.append(int(literal[i+2:i+6], 16))
                i += 6
        elif nx == 'x':
            out.append(int(literal[i+2:i+4], 16))
            i += 4
        elif nx in '\\"\'':
            out.append(ord(nx))
            i += 2
        elif nx == 'n':
            out.append(0x0A); i += 2
        elif nx == 'r':
            out.append(0x0D); i += 2
        elif nx == 't':
            out.append(0x09); i += 2
        else:
            raise ValueError(f"unknown escape: \\{nx}")
    return out

def cp_to_utf8_bytes(cp):
    """Convert codepoint to a list of UTF-8 bytes."""
    if cp < 0x80:
        return [cp]
    if cp < 0x800:
        return [0xC0 | (cp >> 6), 0x80 | (cp & 0x3F)]
    if cp < 0x10000:
        return [0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)]
    return [0xF0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3F),
            0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)]

def parse_match_strings(path):
    with open(path, 'r') as f:
        content = f.read()
    m = re.search(r'matchStrings:\s*\[([^\]]+)\]', content, re.DOTALL)
    if not m:
        return []
    body = m.group(1)
    strs = []
    for line in body.split('\n'):
        line = line.strip().rstrip(',').strip()
        if not line:
            continue
        # strip surrounding quotes
        if line.startswith('"') and line.endswith('"'):
            literal = line[1:-1]
        else:
            continue
        try:
            cps = decode_jsstr(literal)
        except Exception as e:
            print(f"  WARN: skip line: {line!r} ({e})", file=sys.stderr)
            continue
        strs.append(cps)
    return strs

def emit_c_string_literal(byts):
    """Emit byte sequence as a C string literal with octal escapes for
    non-ASCII bytes and standard escapes for special chars."""
    s = '"'
    for b in byts:
        if b == ord('"') or b == ord('\\'):
            s += '\\' + chr(b)
        elif 0x20 <= b < 0x7F:
            s += chr(b)
        else:
            s += f'\\x{b:02X}""'  # closing-and-reopening to disable hex run-on
            # Note: trailing "" creates a separate adjacent literal, which
            # ISO C concatenates. This prevents \x09abc being read as \x9ABC.
    s += '"'
    # collapse common "" "" runs back together where safe
    return s

def main():
    out_path = OUT
    properties = {}
    for prop in PROPS:
        path = os.path.join(SRC_DIR, prop + ".js")
        if not os.path.exists(path):
            print(f"missing: {path}", file=sys.stderr)
            sys.exit(1)
        strs = parse_match_strings(path)
        print(f"  {prop}: {len(strs)} match strings")
        properties[prop] = strs

    with open(out_path, 'w') as f:
        f.write("/*\n")
        f.write(" * Js54 P11 — Generated /v-flag string property tables.\n")
        f.write(" * DO NOT EDIT MANUALLY. Regenerate via:\n")
        f.write(" *   python3 utils/generate_v_flag_string_properties.py\n")
        f.write(" * Source: test262 RegExp/property-escapes/generated/strings/<prop>\n")
        f.write(" * Unicode version: 16.0.0 (pinned by upstream test262).\n")
        f.write(" *\n")
        f.write(" * Each entry is a UTF-8 byte sequence + length. The /v\n")
        f.write(" * rewriter emits these as an alternation of literals.\n")
        f.write(" */\n\n")
        f.write("struct JsVFlagStringProperty {\n")
        f.write("    const char* name;\n")
        f.write("    const char* const* strings;     // UTF-8, NUL-terminated; longest-first\n")
        f.write("    const int* string_lens;         // byte lengths excluding NUL\n")
        f.write("    int count;\n")
        f.write("};\n\n")

        for prop, strs in properties.items():
            tag = prop.lower()
            # convert to UTF-8 byte sequences and sort longest-first
            seqs = []
            for cps in strs:
                bs = []
                for cp in cps:
                    bs.extend(cp_to_utf8_bytes(cp))
                seqs.append(bytes(bs))
            # de-dupe while preserving order, then sort by length desc
            seen = set()
            unique = []
            for s in seqs:
                if s not in seen:
                    seen.add(s)
                    unique.append(s)
            unique.sort(key=lambda x: -len(x))

            f.write(f"// {prop}: {len(unique)} unique sequences\n")
            f.write(f"static const char* const js_v_strs_{tag}[] = {{\n")
            for seq in unique:
                lit = '"'
                for b in seq:
                    if b == 0x22:
                        lit += '\\"'
                    elif b == 0x5C:
                        lit += '\\\\'
                    elif 0x20 <= b < 0x7F:
                        lit += chr(b)
                    else:
                        # Use \x and split with empty string to terminate the
                        # hex sequence (C concatenates adjacent string literals)
                        lit += f'\\x{b:02X}""'
                # strip trailing empty literal if present
                if lit.endswith('""'):
                    lit = lit[:-2]
                lit += '"'
                f.write(f"    {lit},\n")
            f.write("};\n\n")
            f.write(f"static const int js_v_lens_{tag}[] = {{\n    ")
            lens = ', '.join(str(len(s)) for s in unique)
            # wrap every 16
            parts = [str(len(s)) for s in unique]
            for i in range(0, len(parts), 16):
                f.write(', '.join(parts[i:i+16]))
                if i + 16 < len(parts):
                    f.write(",\n    ")
            f.write("\n};\n\n")

        f.write("static const JsVFlagStringProperty js_v_string_properties[] = {\n")
        for prop in PROPS:
            tag = prop.lower()
            f.write(f'    {{"{prop}", js_v_strs_{tag}, js_v_lens_{tag}, '
                    f'(int)(sizeof(js_v_strs_{tag})/sizeof(js_v_strs_{tag}[0]))}},\n')
        f.write("};\n\n")
        f.write("static const int js_v_string_property_count =\n")
        f.write("    (int)(sizeof(js_v_string_properties)/sizeof(js_v_string_properties[0]));\n")

    print(f"\nGenerated {out_path}")
    # report sizes
    total = sum(len(s) for prop in properties for s in properties[prop])
    print(f"Total bytes (codepoints sum): {total}")

if __name__ == "__main__":
    main()
