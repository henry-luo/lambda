"""Prepare only this fixture's local repository and generated document."""
from pathlib import Path
import shutil
import subprocess

root = Path("temp/diff-ui-repository")
if root.exists():
    shutil.rmtree(root)
root.mkdir(parents=True)

def git(*args):
    subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True)

git("-c", "init.templateDir=", "init", "-q")
git("config", "user.name", "Diff UI")
git("config", "user.email", "diff@example.invalid")
(root / "tracked.txt").write_text("original\n")
git("add", "tracked.txt")
git("-c", "commit.gpgSign=false", "commit", "-q", "--no-verify", "-m", "baseline")
(root / "tracked.txt").write_text("staged\n")
git("add", "tracked.txt")
(root / "tracked.txt").write_text("working\n")
(root / "untracked.txt").write_text("untracked\n")
Path("temp/diff-ui-repository.ls").write_text(
    'import diff: lambda.doc.diff\ndiff.repository("temp/diff-ui-repository",{rows:5})^\n'
)
