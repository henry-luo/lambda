zig cc -I lambda/tree-sitter/lib/include lambda/tree-sitter/libtree-sitter.a \
  lambda/tree-sitter-lambda/src/parser.c lambda/lambda-parser.c\
  lib/strbuf.c lib/strview.c lib/arraylist.c \
  lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
  src/transpile.c src/build_ast.c \
  -o transpile.exe -fms-extensions