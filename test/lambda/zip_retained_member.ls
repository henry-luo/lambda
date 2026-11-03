fn retained_member() any^ {
    let archive = input("test/input/zip/sample.docx")^
    content(archive)^[1]
}
let member = retained_member()^
let churn = [for (i in 1 to 200) {index: i, text: string(i)}];
[input(member)^.answer == 42, content(member)^[0].answer == 42, len(churn) == 200]
