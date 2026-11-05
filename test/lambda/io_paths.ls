import paths: lambda.io.path

[
    paths.absolute("../b/./c//","/a/z")^ == "/a/b/c",
    paths.absolute("../../../../","/a/b")^ == "/",
    paths.absolute("~/Documents","/demo","/home/alice")^ == "/home/alice/Documents",
    paths.absolute("~","/demo","/home/alice")^ == "/home/alice",
    paths.absolute("space # and ü.txt","/demo")^ == "/demo/space # and ü.txt",
    paths.absolute("~other/file","relative/base","/home/alice")^ == "/relative/base/~other/file",
    paths.absolute("/a/../b","/ignored")^ == "/b"
]
