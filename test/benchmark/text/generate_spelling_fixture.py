#!/usr/bin/env python3
"""Generate prose and its independently annotated spelling issues."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
PARAGRAPHS = (
    "The document contains a few [mispelled] words. This [speling] test checks [teh] results for a Lambda editor.\n",
    "A careful reader can find [erors] in this report, and the team will review the final version.\n",
    "We write clear notes about the project, but a [sentance] may contain an [unusuall] word.\n",
    "The editor doesn't change the original text; it shows [mistaks] so people can correct them.\n",
    "A good example includes common words, capital letters, and an [incorect] name in the paragraph.\n",
)

parts = []
issues = []
position = 0
for _ in range(90):
    for paragraph in PARAGRAPHS:
        cursor = 0
        for match in re.finditer(r"\[([a-z]+)\]", paragraph):
            prefix = paragraph[cursor:match.start()]
            word = match.group(1)
            parts.extend((prefix, word))
            position += len(prefix)
            issues.append({"word": word, "start": position, "end": position + len(word)})
            position += len(word)
            cursor = match.end()
        suffix = paragraph[cursor:]
        parts.append(suffix)
        position += len(suffix)
(ROOT / "spelling_document.txt").write_text("".join(parts), encoding="utf-8")
(ROOT / "spelling_expected.json").write_text(json.dumps(issues, separators=(",", ":")) + "\n", encoding="utf-8")
