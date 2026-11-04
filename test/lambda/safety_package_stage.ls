// D11.2.4: portable inactive image policy and exact captured member bytes.
import stage: lambda.security.package_stage
let first = stage.begin(input("test/safety/stage/package.zip", 'binary')^)^
let second = stage.advance(first)^
let third = stage.advance(second)^
let fourth = stage.advance(third)^
let done = stage.advance(fourth)^;
[stage.path_key("Data/hello world.txt")^,
 [for (filename in ["../x", "/x", "a//x", "a/./x", "C:/x", "a\\x", "x:stream", "nul.txt",
    "COM1", "LPT9.log", "a.", "a ", "con .txt", ".lambda/grants", "a/.lambda-stage-x", "é.txt"])
    stage.path_key(filename) ^ { 'denied' }],
 first.action.path, first.action.directory, second.action.path, third.action.path,
 third.action.executable, third.action.bytes == binary("package\n"), fourth.action.path,
 len(fourth.action.bytes), done.action.kind, done.action.entries, done.action.bytes,
 stage.advance(done) ^ { 'denied' }]
