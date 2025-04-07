zig cc -I lambda-parser/tree-sitter/lib/include lambda-parser/tree-sitter/libtree-sitter.a \
  lambda-parser/tree-sitter-lambda/src/parser.c lambda-parser/lambda-parser.c\
  lib/strbuf.c src/transpile.c src/infer_type.c \
  -o transpile.exe -fms-extensions