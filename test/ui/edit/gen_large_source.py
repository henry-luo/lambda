#!/usr/bin/env python3
"""Write a source-editor stress document (Radiant_Design_Source_Editor §11)
into the given path (under ./temp/).

    gen_large_source.py PATH [LINES]              numbered plain lines (~5.7 MB at 100,000)
    gen_large_source.py PATH LINES --markdown     Markdown sections with every construct
"""
import sys

MARKDOWN_UNIT = [
    "## Section {n}",
    "",
    "Some **bold** text and *em* and `code` with a [link](http://x.y/{n}).",
    "",
    "- item **one**",
    "- item two",
    "",
    "```js",
    "let x = {n};",
    "```",
    "",
    "> quoted line {n}",
    "",
]

def main(path, lines=100000, markdown=False):
    with open(path, "w") as out:
        if markdown:
            n = 0
            written = 0
            while written < lines:
                for row in MARKDOWN_UNIT:
                    out.write(row.format(n=n) + "\n")
                    written += 1
                n += 1
        else:
            for i in range(lines):
                out.write("line %06d: the quick brown fox jumps over the lazy dog\n" % (i + 1))

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(args[0], int(args[1]) if len(args) > 1 else 100000, "--markdown" in sys.argv)
