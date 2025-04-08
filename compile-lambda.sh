zig cc -I lambda-parser/tree-sitter/lib/include lambda-parser/tree-sitter/libtree-sitter.a \
  lambda-parser/tree-sitter-lambda/src/parser.c lambda-parser/lambda-parser.c\
  lib/strbuf.c lib/arraylist.c \
  lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
  src/transpile.c src/infer_type.c \
  -o transpile.exe -fms-extensions