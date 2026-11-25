// Compare the public parser with snapshots captured from the native provider.
let cases = input("test/lambda/latex/fixtures/tex_native_oracle.json", "json")^;
let checked = [for (item in cases) {
    let result = parse(item.source, {type:item.kind, ini:item.ini})^;
    {source:item.source, matches:format(result, 'json')^ == item.expected}
}];
{cases:len(checked), failures:[for (item in checked where not item.matches) item]}
