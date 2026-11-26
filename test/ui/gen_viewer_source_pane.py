from pathlib import Path
import sys


root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
(root / "wide.css").write_text("/* " + "wide " * 700 + "*/\n/* last line */\n", encoding="utf-8")
rows = ["" if line == 2 else f"source row {line:02d}" for line in range(1, 22)]
(root / "short.mark").write_text("\n".join(rows) + "\n", encoding="utf-8")
(root / "empty.txt").write_text("", encoding="utf-8")
(root / "notes.md").write_text("# Notes\n", encoding="utf-8")
rows = [f"short row {line:03d}" for line in range(1, 231)]
rows[59] = "\tmedium: " + "m" * 140
rows[149] = "wider: " + "w" * 280
(root / "progressive.txt").write_text("\n".join(rows) + "\n", encoding="utf-8")
