import x: lambda.doc.xlsx
let path="test/input/xlsx/basic.xlsx"
let parsed=x.convert(path)^
let zip=input(path,"zip")^
let dated=x.convert("test/input/xlsx/date1904.xlsx")^;
[
    x.load(zip)^==parsed,
    parsed.sheets[0].cells[1].value=="Price",
    parsed.sheets[0].cells[4].value==12.5,
    parsed.sheets[0].cells[5].value==true,
    parsed.sheets[0].cells[8].style.format=="date",
    dated.sheets[0].cells[8].style.format=="date1904",
    match (x.convert("test/input/xlsx/bad-shared-string.xlsx") ^ { ^ }) {case error: true default: false},
    match (x.convert("test/input/xlsx/bad-relationship.xlsx") ^ { ^ }) {case error: true default: false},
    match (x.convert("https://example.invalid/test.xlsx") ^ { ^ }) {case error: true default: false},
    match (x.convert("test/input/docx/minimal.docx") ^ { ^ }) {case error: true default: false}
]
