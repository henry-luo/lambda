from pathlib import Path
from shutil import copyfile
from zipfile import ZIP_DEFLATED, ZipFile


source = Path("test/input/xlsx")
target = Path("temp/viewer-xlsx-files")
target.mkdir(parents=True, exist_ok=True)
for name in ("basic.xlsx", "layout-hidden-anchor.xlsx", "bad-case-relationship.xlsx"):
    copyfile(source / name, target / name)
copyfile(source / "external/files-sample-1000rows.xlsx", target / "large.xlsx")
with ZipFile(target / "bundle.zip", "w", ZIP_DEFLATED) as archive:
    archive.write(source / "basic.xlsx", "Inside.XLSX")
