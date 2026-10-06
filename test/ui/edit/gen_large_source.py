#!/usr/bin/env python3
"""Write the source-editor stress document (Radiant_Design_Source_Editor §11):
100,000 numbered lines, about 5 MB, into the given path (under ./temp/)."""
import sys

def main(path, lines=100000):
    with open(path, "w") as out:
        for i in range(lines):
            out.write("line %06d: the quick brown fox jumps over the lazy dog\n" % (i + 1))

if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 100000)
